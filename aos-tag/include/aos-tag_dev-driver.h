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
 * @brief Declarations of functions and structures of the module's dev driver.
 *
 * @author Roberto Masocco
 *
 * @date April 10, 2021
 */

#ifndef _AOS_TAG_DEVDRIVER_H
#define _AOS_TAG_DEVDRIVER_H

#include <linux/fs.h>
#include <linux/types.h>

#define STATUS_DEVFILE "aos_tag_status"

int aos_tag_open(struct inode *inode, struct file *filp);
int aos_tag_release(struct inode *inode, struct file *filp);
ssize_t aos_tag_read(struct file *filp, char *buf, size_t size, loff_t *off);
ssize_t aos_tag_write(struct file *filp, const char *buf, size_t size,
                      loff_t *off);
long aos_tag_ioctl(struct file *filp, unsigned int cmd, unsigned long param);

#endif
