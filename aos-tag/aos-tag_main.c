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

#include "splay-trees_int-keys/splay-trees_int-keys.h"

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

/* MODULE PARAMETERS */
/* tag_get system call number. */
int tag_get_nr = 0;
module_param(tag_get_nr, int, S_IRUGO);
MODULE_PARAM_DESC(tag_get_nr, "tag_get syscall number.");

/* tag_receive system call number. */
int tag_receive_nr = 0;
module_param(tag_receive_nr, int, S_IRUGO);
MODULE_PARAM_DESC(tag_receive_nr, "tag_receive syscall number.");

/* tag_send system call number. */
int tag_send_nr = 0;
module_param(tag_send_nr, int, S_IRUGO);
MODULE_PARAM_DESC(tag_send_nr, "tag_send syscall number.");

/* tag_ctl system call number. */
int tag_ctl_nr = 0;
module_param(tag_ctl_nr, int, S_IRUGO);
MODULE_PARAM_DESC(tag_ctl_nr, "tag_ctl syscall number.");

/* Device driver major number. */
int tag_drv_major = 0;
module_param(tag_drv_major, int, S_IRUGO);
MODULE_PARAM_DESC(tag_drv_major, "Device driver major number.");

/* Max number of active instances. */
unsigned long max_tags = __MAX_TAGS_DFL;
module_param(max_tags, ulong, S_IRUGO);
MODULE_PARAM_DESC(max_tags, "Max number of active instances.");

/* Max message size. */
unsigned long max_msg_sz = __MAX_MSG_SZ_DFL;
module_param(max_msg_sz, ulong, S_IRUGO);
MODULE_PARAM_DESC(max_msg_sz, "Max message size for all instances.");

/* SYSTEM CALLS STUBS */
/* tag_get kernel level stub. */
__SYSCALL_DEFINEx(3, _tag_get, int, key, int, cmd, int, perm) {
    return aos_tag_get(key, cmd, perm);
}

/* tag_receive kernel level stub. */
__SYSCALL_DEFINEx(4, _tag_rcv, int, tag, int, lvl, char*, buf, size_t, size) {
    return aos_tag_rcv(tag, lvl, buf, size);
}

/* tag_send kernel level stub. */
__SYSCALL_DEFINEx(4, _tag_snd, int, tag, int, lvl, char*, buf, size_t, size) {
    return aos_tag_snd(tag, lvl, buf, size);
}

/* tag_ctl kernel level stub. */
__SYSCALL_DEFINEx(2, _tag_ctl, int, tag, int, cmd) {
    return aos_tag_ctl(tag, cmd);
}

/* Required module's reference. */
struct module *scth_mod;

/* GLOBAL MODULE VARIABLES */
/* Shared instances BST-dictionary. */
SplayIntTree *shared_bst = NULL;
DECLARE_RWSEM(shared_bst_lock);

/* Instances array and related bitmask. */
tag_ptr_t *tags_list = NULL;
tag_bitmask *tags_mask = NULL;
