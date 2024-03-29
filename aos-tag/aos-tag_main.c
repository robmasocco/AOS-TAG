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
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date April 10, 2021
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/rwsem.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/compiler.h>

#include "scth/include/scth.h"

#include "include/aos-tag.h"
#include "include/aos-tag_types.h"
#include "include/aos-tag_syscalls.h"
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
MODULE_PARM_DESC(tag_get_nr, "tag_get syscall number.");

/* tag_receive system call number. */
int tag_receive_nr = 0;
module_param(tag_receive_nr, int, S_IRUGO);
MODULE_PARM_DESC(tag_receive_nr, "tag_receive syscall number.");

/* tag_send system call number. */
int tag_send_nr = 0;
module_param(tag_send_nr, int, S_IRUGO);
MODULE_PARM_DESC(tag_send_nr, "tag_send syscall number.");

/* tag_ctl system call number. */
int tag_ctl_nr = 0;
module_param(tag_ctl_nr, int, S_IRUGO);
MODULE_PARM_DESC(tag_ctl_nr, "tag_ctl syscall number.");

/* Device driver major number. */
int tag_drv_major = 0;
module_param(tag_drv_major, int, S_IRUGO);
MODULE_PARM_DESC(tag_drv_major, "Device driver major number.");

/* Max number of active instances. */
unsigned int max_tags = __MAX_TAGS_DFL;
module_param(max_tags, uint, S_IRUGO);
MODULE_PARM_DESC(max_tags, "Max number of active instances.");

/* Max message size. */
unsigned int max_msg_sz = __MAX_MSG_SZ_DFL;
module_param(max_msg_sz, uint, S_IRUGO);
MODULE_PARM_DESC(max_msg_sz, "Max message size for all instances.");

/* SYSTEM CALLS STUBS */
/* tag_get kernel level stub. */
__SYSCALL_DEFINEx(3, _tag_get, int, key, int, cmd, int, perm) {
    int ret;
    if (!try_module_get(THIS_MODULE)) return -ENOSYS;
    ret = aos_tag_get(key, cmd, perm);
    module_put(THIS_MODULE);
    return ret;
}

/* tag_receive kernel level stub. */
__SYSCALL_DEFINEx(4, _tag_rcv, int, tag, int, lvl, char*, buf, size_t, size) {
    int ret;
    if (!try_module_get(THIS_MODULE)) return -ENOSYS;
    ret = aos_tag_rcv(tag, lvl, buf, size);
    module_put(THIS_MODULE);
    return ret;
}

/* tag_send kernel level stub. */
__SYSCALL_DEFINEx(4, _tag_snd, int, tag, int, lvl, char*, buf, size_t, size) {
    int ret;
    if (!try_module_get(THIS_MODULE)) return -ENOSYS;
    ret = aos_tag_snd(tag, lvl, buf, size);
    module_put(THIS_MODULE);
    return ret;
}

/* tag_ctl kernel level stub. */
__SYSCALL_DEFINEx(2, _tag_ctl, int, tag, int, cmd) {
    int ret;
    if (!try_module_get(THIS_MODULE)) return -ENOSYS;
    ret = aos_tag_ctl(tag, cmd);
    module_put(THIS_MODULE);
    return ret;
}

extern struct file_operations tag_fops;
extern struct cdev tag_cdev;
extern struct device *tag_dev;
extern dev_t tag_status_dvn;
extern struct class *tag_status_cls;

/* Required module's reference. */
struct module *scth_mod;

/* GLOBAL MODULE VARIABLES */
/* Shared instances BST-dictionary. */
SplayIntTree *shared_bst = NULL;
DECLARE_RWSEM(shared_bst_lock);

/* Instances array and related bitmask. */
tag_ptr_t *tags_list = NULL;
tag_bitmask *tags_mask = NULL;

/**
 * @brief Routine to set access permissions for device files through sysfs's 
 * interface.
 *
 * @param dev Device file structure to set permissions for.
 * @param mode Permissions to set for the new device.
 * @return Operation result as pointer.
 */
static char *tag_devnode(struct device *dev, umode_t *mode) {
    // Consistency check.
    if (!mode) return NULL;
    *mode = S_IRUGO | S_IWUGO;
    return NULL;
}

/**
 * @brief Module initialization routine. 
 * Initializes the module's data and internal structures, and 
 * requests system calls installation. 
 * See notes for a detailed description of the operations performed.
 *
 * @return Operation result: 0 or error code.
 */
int init_module(void) {
    unsigned int i;
    int ret;
    // Consistency check on module parameters.
    if (max_tags < __MAX_TAGS_DFL) max_tags = __MAX_TAGS_DFL;
    if (max_msg_sz < __MAX_MSG_SZ_DFL) max_msg_sz = __MAX_MSG_SZ_DFL;
    // Lock the SCTH module.
    mutex_lock(&module_mutex);
    scth_mod = find_module("scth");
    if (!try_module_get(scth_mod)) {
        mutex_unlock(&module_mutex);
        printk(KERN_ERR "%s: SCTH module not found.\n", MODNAME);
        return -EPERM;
    }
    mutex_unlock(&module_mutex);
    // Create BST dictionary.
    shared_bst = create_splay_int_tree();
    if (unlikely(shared_bst == NULL)) {
        printk(KERN_ERR "%s: Failed to create BST dictionary.\n", MODNAME);
        module_put(scth_mod);
        return -ENOMEM;
    }
    // Create the tags bitmask.
    tags_mask = TAG_MASK_CREATE(max_tags);
    if (unlikely(tags_mask == NULL)) {
        printk(KERN_ERR "%s: Failed to create tags bitmask.\n", MODNAME);
        module_put(scth_mod);
        delete_splay_int_tree(shared_bst);
        return -ENOMEM;
    }
    // Create the tags list.
    tags_list = (tag_ptr_t *)kzalloc(sizeof(tag_ptr_t) * max_tags, GFP_KERNEL);
    if (unlikely(tags_list == NULL)) {
        printk(KERN_ERR "%s: Failed to create tags list.\n", MODNAME);
        module_put(scth_mod);
        delete_splay_int_tree(shared_bst);
        TAG_MASK_FREE(tags_mask);
        return -ENOMEM;
    }
    for (i = 0; i < max_tags; i++) {
        tags_list[i].ptr = NULL;
        init_rwsem(&(tags_list[i].rcv_rwsem));
        init_rwsem(&(tags_list[i].snd_rwsem));
    }
    // Initialize and register device driver.
    cdev_init(&tag_cdev, &tag_fops);
    tag_drv_major = __register_chrdev(0, 0, 1, __DRVNAME, &tag_fops);
    if (tag_drv_major < 0) {
        printk(KERN_ERR "%s: Failed to register char device.\n", MODNAME);
        module_put(scth_mod);
        delete_splay_int_tree(shared_bst);
        TAG_MASK_FREE(tags_mask);
        kfree(tags_list);
        return tag_drv_major;
    }
    // Must create kobjects in /sys/class before doing stuff in /dev, also
    // to get access permissions right.
    tag_status_cls = class_create(THIS_MODULE, __STAT_DEVFILE);
    if (IS_ERR(tag_status_cls)) {
        printk(KERN_ERR "%s: Failed to create status device class.\n", MODNAME);
        __unregister_chrdev(tag_drv_major, 0, 1, __DRVNAME);
        module_put(scth_mod);
        delete_splay_int_tree(shared_bst);
        TAG_MASK_FREE(tags_mask);
        kfree(tags_list);
        return -EPERM;
    }
    tag_status_cls->devnode = tag_devnode;
    // Create device file in /dev.
    tag_status_dvn = MKDEV(tag_drv_major, 0);
    tag_dev = device_create(tag_status_cls, NULL, tag_status_dvn,
                            NULL, __STAT_DEVFILE);
    if (IS_ERR(tag_dev)) {
        printk(KERN_ERR "%s: Failed to create device file %s.\n",
               MODNAME, __STAT_DEVFILE);
        class_destroy(tag_status_cls);
        __unregister_chrdev(tag_drv_major, 0, 1, __DRVNAME);
        module_put(scth_mod);
        delete_splay_int_tree(shared_bst);
        TAG_MASK_FREE(tags_mask);
        kfree(tags_list);
        return -EPERM;
    }
    // Device goes live.
    ret = cdev_add(&tag_cdev, tag_status_dvn, 1);
    if (ret < 0) {
        printk(KERN_ERR "%s: Failed to add char device.\n", MODNAME);
        device_destroy(tag_status_cls, tag_status_dvn);
        class_destroy(tag_status_cls);
        __unregister_chrdev(tag_drv_major, 0, 1, __DRVNAME);
        module_put(scth_mod);
        delete_splay_int_tree(shared_bst);
        TAG_MASK_FREE(tags_mask);
        kfree(tags_list);
        return ret;
    }
    // Install the new system calls.
    tag_get_nr = scth_hack(__x64_sys_tag_get);
    tag_receive_nr = scth_hack(__x64_sys_tag_rcv);
    tag_send_nr = scth_hack(__x64_sys_tag_snd);
    tag_ctl_nr = scth_hack(__x64_sys_tag_ctl);
    if ((tag_get_nr == -1) ||
        (tag_receive_nr == -1) ||
        (tag_send_nr == -1) ||
        (tag_ctl_nr == -1)) {
        if (tag_get_nr != -1) scth_unhack(tag_get_nr);
        if (tag_receive_nr != -1) scth_unhack(tag_receive_nr);
        if (tag_send_nr != -1) scth_unhack(tag_send_nr);
        if (tag_ctl_nr != -1) scth_unhack(tag_ctl_nr);
        printk(KERN_ERR "%s: Failed to install system calls.\n", MODNAME);
        module_put(scth_mod);
        delete_splay_int_tree(shared_bst);
        TAG_MASK_FREE(tags_mask);
        kfree(tags_list);
        cdev_del(&tag_cdev);
        device_destroy(tag_status_cls, tag_status_dvn);
        class_destroy(tag_status_cls);
        __unregister_chrdev(tag_drv_major, 0, 1, __DRVNAME);
        return -EPERM;
    }
    printk(KERN_INFO "%s: Initialization completed successfully.\n", MODNAME);
    printk(KERN_INFO "%s: tag_get installed at entry no. %d.\n",
           MODNAME, tag_get_nr);
    printk(KERN_INFO "%s: tag_receive installed at entry no. %d.\n",
           MODNAME, tag_receive_nr);
    printk(KERN_INFO "%s: tag_send installed at entry no. %d.\n",
           MODNAME, tag_send_nr);
    printk(KERN_INFO "%s: tag_ctl installed at entry no. %d.\n",
           MODNAME, tag_ctl_nr);
    printk(KERN_INFO "%s: Device driver registered with major number: %d.\n",
           MODNAME, tag_drv_major);
    return 0;
}

/**
 * @brief Module cleanup routine. 
 * Undoes all that init_module did, in reverse.
 */
void cleanup_module(void) {
    unsigned int i = 0;
    // Restore the system call table and release the SCTH module.
    scth_unhack(tag_get_nr);
    scth_unhack(tag_receive_nr);
    scth_unhack(tag_send_nr);
    scth_unhack(tag_ctl_nr);
    module_put(scth_mod);
    cdev_del(&tag_cdev);
    device_destroy(tag_status_cls, tag_status_dvn);
    class_destroy(tag_status_cls);
    __unregister_chrdev(tag_drv_major, 0, 1, __DRVNAME);
    // Scan the tags list, releasing leftovers.
    for (; i < max_tags; i++) {
        tag_t *curr_tag;
        curr_tag = tags_list[i].ptr;
        if (curr_tag != NULL) {
            unsigned int j = 0;
            for (; j < __NR_LEVELS; j++) {
                char *curr_buf;
                curr_buf = (curr_tag->msg_bufs)[j];
                if (curr_buf != NULL) kfree(curr_buf);
            }
            kfree(curr_tag);
        }
    }
    kfree(tags_list);
    TAG_MASK_FREE(tags_mask);
    delete_splay_int_tree(shared_bst);
    printk(KERN_INFO "%s: Shutdown...\n", MODNAME);
}
