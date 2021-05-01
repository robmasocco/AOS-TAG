/**
 * This is free software.
 * You can redistribute it and/or modify this file under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 * 
 * This file is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this file; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
 */
/**
 * @brief Source code file for the module's char device driver.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date April 10, 2021
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/rwsem.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/compiler.h>

#include "include/aos-tag.h"
#include "include/aos-tag_dev-driver.h"
#include "include/aos-tag_types.h"

/* Magic number: expected maximum status file line length, given its content. */
#define __STAT_LINE_LEN 64

/* As LEN, but considering the null terminator. */
#define __STAT_LINE_SZ (__STAT_LINE_LEN + 1)

extern int tag_drv_major;
extern unsigned int max_tags;
extern tag_ptr_t *tags_list;

/* Status device file size and data. */
typedef struct _tag_stat_t {
    size_t stat_len;
    char *stat_data;
} tag_stat_t;

/* Instance status snapshot raw data buffer. */
typedef struct _tag_snap_t {
    unsigned char valid : 1;
    int key;
    kuid_t c_euid;
    unsigned long readers_cnts[__NR_LEVELS];
} tag_snap_t;

/* AOS-TAG service character device driver. */
struct file_operations tag_fops = {
    .owner = THIS_MODULE,
    .open = aos_tag_open,
    .read = aos_tag_read,
    .write = aos_tag_write,
    .unlocked_ioctl = aos_tag_ioctl,
    .release = aos_tag_release
};

/* Character device structure. */
struct cdev tag_cdev;

/* Device structure and number. */
struct device *tag_dev;
dev_t tag_status_dvn;

/* Device class in sysfs. */
struct class *tag_status_cls;

/**
 * @brief Opens a new session for the device file. 
 * Takes a snapshot of the status of the system by scanning the instances array. 
 * Then, creates a fake text file in kernel memory: the data for the current 
 * session.
 *
 * @param inode Device file inode.
 * @param file Device file struct.
 * @return 0, or error code for errno.
 */
int aos_tag_open(struct inode *inode, struct file *filp) {
    char *write_ptr;
    tag_stat_t *new_stat;
    tag_snap_t *snaps;
    unsigned int tag, valid_cnt = 0;
    size_t new_stat_sz;
    // Consistency checks.
    if ((inode == NULL) || (filp == NULL)) return -EINVAL;
    // Allocate memory for the new objects.
    new_stat = (tag_stat_t *)kzalloc(sizeof(tag_stat_t), GFP_KERNEL);
    if (new_stat == NULL) return -ENOMEM;
    snaps = (tag_snap_t *)kzalloc(max_tags * sizeof(tag_snap_t), GFP_KERNEL);
    if (snaps == NULL) {
        kfree(new_stat);
        return -ENOMEM;
    }
    // First pass: linear scan of the instance array to get a snapshot of the
    // current status of the service.
    // Note that, being this a snapshot, we don't grab any lock, and don't care
    // about race conditions at all.
    for (tag = 0; tag < max_tags; tag++) {
        tag_t *curr_tag;
        unsigned int lvl;
        if (down_read_trylock(&(tags_list[tag].snd_rwsem)) == 0) {
            // Instance is being created or removed AKA busy: we are too late.
            snaps[tag].valid = 0x0;
            continue;
        }
        curr_tag = tags_list[tag].ptr;
        if (curr_tag == NULL) {
            // Instance not present.
            up_read(&(tags_list[tag].snd_rwsem));
            snaps[tag].valid = 0x0;
            continue;
        }
        // Get instance and levels status.
        snaps[tag].valid = 0x1;
        valid_cnt++;
        snaps[tag].key = curr_tag->key;
        snaps[tag].c_euid.val = curr_tag->creator_euid.val;
        for (lvl = 0; lvl < __NR_LEVELS; lvl++)
            // By adding the two presence counters we get the total number
            // of waiting threads: those that are still copying a message
            // and those that were too late for the last one, which are all
            // threads currently waiting for a message on this level.
            snaps[tag].readers_cnts[lvl] =
                (curr_tag->lvl_conds)[lvl]._pres_count[0] +
                (curr_tag->lvl_conds)[lvl]._pres_count[1];
        asm volatile ("sfence" ::: "memory");
        up_read(&(tags_list[tag].snd_rwsem));
    }
    // Second pass: build the fake text file contents.
    // Compute text length.
    new_stat_sz = valid_cnt * __NR_LEVELS * __STAT_LINE_LEN;
    if (new_stat_sz == 0) {
        // This will result in an immediate EOF.
        new_stat->stat_data = NULL;
        new_stat->stat_len = 0;
    } else {
        char new_line[__STAT_LINE_SZ];
        int chars;
        unsigned int lvl;
        // Allocate (a lot of) memory for the fake text file.
        write_ptr = (char *)vzalloc(new_stat_sz);
        if (write_ptr == NULL) {
            kfree(snaps);
            kfree(new_stat);
            return -ENOMEM;
        }
        new_stat->stat_data = write_ptr;
        // "Print" lines.
        for (tag = 0; tag < max_tags; tag++) {
            if (!(snaps[tag].valid)) continue;
            for (lvl = 0; lvl < __NR_LEVELS; lvl++) {
                memset(new_line, 0, __STAT_LINE_SZ);
                chars = scnprintf(new_line, __STAT_LINE_SZ,
                                  "%u\t%d\t%u\t%u\t%lu\n",
                                  tag,
                                  snaps[tag].key,
                                  snaps[tag].c_euid.val,
                                  lvl,
                                  snaps[tag].readers_cnts[lvl]);
                if (unlikely(!chars)) {
                    printk(KERN_ERR "%s: Failed to \"print\" line in file.\n",
                           MODNAME);
                    kfree(snaps);
                    vfree(new_stat->stat_data);
                    kfree(new_stat);
                    return -EFAULT;
                }
                memcpy(write_ptr, new_line, chars);
                write_ptr += chars;
                new_stat->stat_len += chars;
            }
        }
    }
    // Set session data and we're done.
    kfree(snaps);
    filp->private_data = (void *)new_stat;
    asm volatile ("sfence" ::: "memory");
    return 0;
}

/**
 * @brief Read operation: returns some content from the fake text file.
 *
 * @param file Device file struct.
 * @param buf Userspace buffer address.
 * @param size Size of the aforementioned buffer.
 * @param off Offset at which to start reading.
 * @return Number of bytes read, or error code for errno.
 */
ssize_t aos_tag_read(struct file *filp, char *buf, size_t size, loff_t *off) {
    tag_stat_t *stat;
    char *file_buf;
    size_t file_sz;
    unsigned long to_copy, copied;
    // Consistency checks.
    if ((filp == NULL) || (buf == NULL) || (off == NULL) || (*off < 0) ||
        (size == 0))
        return -EINVAL;
    stat = (tag_stat_t *)(filp->private_data);
    file_buf = stat->stat_data;
    file_sz = stat->stat_len;
    // Check for EOF condition.
    if ((*off >= file_sz) || (stat->stat_len == 0))
        return 0;  // This SHOULD be interpreted as EOF.
    // Determine the correct amount of data to copy.
    // Didn't know that this macro existed but many thanks to its author.
    to_copy = min((unsigned long)size, file_sz - (unsigned long)*off);
    copied = to_copy - copy_to_user(buf, file_buf + *off, to_copy);
    if (copied == 0) return -EFAULT;
    // Update file offset and we're done.
    *off += copied;
    return (ssize_t)copied;
}

/**
 * @brief Write operation: a nop.
 *
 * @param file Device file struct.
 * @param buf Userspace buffer address.
 * @param size Size of the aforementioned buffer.
 * @param off Offset to write to.
 * @return Number of bytes written, or error code for errno.
 */
ssize_t aos_tag_write(struct file *filp, const char *buf, size_t size,
                      loff_t *off) {
    return -EPERM;
}

/**
 * @brief I/O control: a nop.
 *
 * @param file Device file struct.
 * @param cmd Command to execute.
 * @param param Parameter for the aforementioned command execution.
 * @return 0, or error code for errno.
 */
long aos_tag_ioctl(struct file *filp, unsigned int cmd, unsigned long param) {
    return -EPERM;
}

/**
 * @brief When the last session is closed, releases the fake file.
 *
 * @param inode Device file inode.
 * @param file Device file struct.
 * @return 0, or error code for errno.
 */
int aos_tag_release(struct inode *inode, struct file *filp) {
    tag_stat_t *stat;
    if (filp == NULL) return -EINVAL;
    stat = (tag_stat_t *)(filp->private_data);
    filp->private_data = NULL;
    stat->stat_len = 0;
    vfree(stat->stat_data);
    kfree(stat);
    return 0;
}
