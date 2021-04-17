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
 * @brief Main source file for the "System Call Table Hacker" kernel module.
 *        See other source files for more information.
 *
 * @author Roberto Masocco
 *
 * @date March 8, 2021
 */
/**
 * WARNING: Module locking is not explicitly dealt with here, you'll have to
 *          include it in your code.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/version.h>

#include "include/scth.h"

#define MODNAME "SCTH"

/* This module only works for kernels equal or later than 4.17. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
#error "This module requires kernel >= 4.17."
#endif

extern int nr_sysnis;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roberto Masocco <robmasocco@gmail.com>");
MODULE_DESCRIPTION("Discovers and hacks the system call table.");
MODULE_INFO(name, "scth");
MODULE_INFO(OS, "Linux");
MODULE_VERSION("1.0");

/* Module initialization routine. */
int init_module(void) {
    void **table_addr;
    table_addr = scth_finder();
    if (table_addr == NULL) {
        printk(KERN_ERR "%s: Shutdown...\n", MODNAME);
        return -EFAULT;
    }
    printk(KERN_INFO "%s: Ready, %d available entries.\n", MODNAME, nr_sysnis);
    return 0;
}

/* Module cleanup routine. */
void cleanup_module(void) {
    scth_cleanup();
    printk(KERN_INFO "%s: Shutdown...\n", MODNAME);
}
