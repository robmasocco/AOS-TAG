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
 * @brief Source code file for the module's system calls.
 *
 * @author Roberto Masocco
 *
 * @date April 10, 2021
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

#include "include/aos-tag_defs.h"
#include "include/aos-tag_types.h"
#include "include/aos-tag_syscalls.h"

int aos_tag_get(int key, int cmd, int perm) {
    // TODO Debug.
    printk(KERN_INFO "%s: tag_get called with (%d, %d, %d).\n", MODNAME,
        key, cmd, perm);
    return 0;
}

int aos_tag_rcv(int tag, int lvl, char *buf, size_t size) {
    // TODO Debug.
    printk(KERN_INFO "%s: tag_receive called with (%d, %d, 0x%px, %lu).\n",
        MODNAME, tag, lvl, buf, size);
    return 0;
}

int aos_tag_snd(int tag, int lvl, char *buf, size_t size) {
    // TODO Debug.
    printk(KERN_INFO "%s: tag_send called with (%d, %d, 0x%px, %lu).\n",
        MODNAME, tag, lvl, buf, size);
    return 0;
}

int aos_tag_ctl(int tag, int cmd) {
    // TODO Debug.
    printk(KERN_INFO "%s: tag_ctl called with (%d, %d).\n", MODNAME, tag, cmd);
    return 0;
} 
