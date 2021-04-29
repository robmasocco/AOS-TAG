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

#include "include/aos-tag.h"
#include "include/aos-tag_dev-driver.h"
#include "include/aos-tag_types.h"

extern int tag_drv_major;

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
    return 0;
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
    vfree(filp->private_data);
    filp->private_data = NULL;
    return 0;
}
