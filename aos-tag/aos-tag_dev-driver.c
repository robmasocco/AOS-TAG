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
 * @author Roberto Masocco
 *
 * @date April 10, 2021
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/vmalloc.h>
#include <linux/cred.h>
#include <linux/types.h>

#include "include/aos-tag.h"
#include "include/aos-tag_dev-driver.h"
#include "include/aos-tag_types.h"

/* Magic number: expected maximum status file line length, given its content. */
#define __STAT_LINE_SZ (54 + 1)  // Remember the null terminator.

extern int tag_drv_major;

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
    ssize_t copied;
    tag_stat_t *stat;
    char *file_buf;
    size_t file_sz;
    // Consistency checks.
    if ((filp == NULL) || (buf == NULL) || (off == NULL) || (*off < 0) ||
        (size == 0))
        return -EINVAL;
    file_buf = stat->stat_data;
    file_sz = stat->stat_len;
    // Check for EOF condition.
    if (*off >= file_sz) return 0;
    // TODO
    // Update file offset and we're done.
    *off += copied;
    return copied;
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
    vfree(stat);
    return 0;
}
