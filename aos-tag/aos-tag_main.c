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
 * @brief Main source code file for the AOS-TAG module.
 *
 * @author Roberto Masocco
 *
 * @date April 10, 2021
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/rwsem.h>
#include <linux/errno.h>
#include <linux/version.h>

#include "scth/include/scth.h"

#include "include/aos-tag_types.h"
#include "include/aos-tag_syscalls.h"
#include "include/aos-tag_defs.h"
#include "include/aos-tag_dev-driver.h"

#include "utils/aos-tag_bitmask.h"

/* This module only works for kernels equal or later than 4.17. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
#error "This module requires kernel >= 4.17."
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roberto Masocco <robmasocco@gmail.com>");
MODULE_DESCRIPTION("Tag-based IPC service.");
MODULE_INFO(name, "aos-tag");
MODULE_INFO(OS, "Linux");
MODULE_VERSION("1.0");
