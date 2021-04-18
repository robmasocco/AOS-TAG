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
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/compiler.h>

#include "include/aos-tag_defs.h"
#include "include/aos-tag_types.h"
#include "include/aos-tag_syscalls.h"

#include "utils/aos-tag_bitmask.h"
#include "utils/aos-tag_conditions.h"

#include "splay-trees_int-keys/splay-trees_int-keys.h"

extern SplayIntTree *shared_bst;
extern struct rw_semaphore shared_bst_lock;

extern tag_ptr_t *tags_list;
extern tag_bitmask *tags_mask;

/**
 * Opens a new instance of the service. 
 * Instances can be shared or not, depending on the value of key. 
 * An instance can be created or reopened, depending on the value of cmd. 
 * With perm, it is possible to specify whether permission checks should be 
 * peformed to limit access to threads executing on behalf of the same user 
 * that created the instance. 
 * Shared instances will be added to the BST, thus everyone could potentially 
 * reopen them (but following operations might check permissions), 
 * instead PRIVATE ones will only be created and added to the static list.
 *
 * @param key Key to assign to the new instance, or to look for in the BST.
 * @param cmd Open a new instance, or look for an existing one.
 * @param perm Enables EUID checks for following operations.
 * @return Static list index as tag descriptor.
 */
int aos_tag_get(int key, int cmd, int perm) {
    int sem_ret, tag, full = 0;
    SplayIntNode *search_res;
    tag_t *new_srv;
    unsigned int i;
    unsigned long ins_res;
    // TODO Debug.
    printk(KERN_INFO "%s: tag_get called with (%d, %d, %d).\n",
        MODNAME, key, cmd, perm);
    // Consistency check on input arguments.
    if ((cmd != __TAG_OPEN) && (cmd != __TAG_CREATE)) return -EINVAL;
    if ((perm != __TAG_ALL) && (perm != __TAG_USR)) return -EINVAL;
    // Normal operation basically follows one of two paths.
    if ((cmd == __TAG_OPEN) && (key != __TAG_IPC_PRIVATE)) {
        // We have been asked to reopen an instance, if it exists.
        sem_ret = down_read_killable(&shared_bst_lock);
        if (sem_ret == -EINTR) return -EINTR;
        search_res =
            (SplayIntNode *)splay_int_search(shared_bst, key);
        up_read(&shared_bst_lock);
        if (search_res == NULL) return -ENOKEY;
        // TODO Debug.
        printk(KERN_DEBUG "%s: tag_get: Requested key: %d.\n",
            MODNAME, search_res->_data);
        return search_res->_data;
    }
    if (cmd == __TAG_CREATE) {
        // We have been asked to create a new instance.
        if (key != __TAG_IPC_PRIVATE) {
            // We have been asked to create a new shared instance.
            // We gotta lock the BST and look for the key first.
            sem_ret = down_write_killable(&shared_bst_lock);
            if (sem_ret == -EINTR) return -EINTR;
            search_res =
                (SplayIntNode *)splay_int_search(shared_bst, key);
            if (search_res != NULL) {
                // Key already exists: exit.
                up_write(&shared_bst_lock);
                return -EALREADY;
            }
            // At this point we must hold the lock until done.
        }
        tag = TAG_NEXT(tags_mask, full);
        if (full) {
            // System is full: we can't add a new instance.
            if (key != __TAG_IPC_PRIVATE) up_write(&shared_bst_lock);
            return -ENOMEM;
        }
        // Allocate and initialize a new instance struct.
        new_srv = (tag_t *)kzalloc(sizeof(tag_t), GFP_KERNEL);
        if (unlikely(new_srv == NULL)) {
            if (key != __TAG_IPC_PRIVATE) up_write(&shared_bst_lock);
            return -ENOMEM;
        }
        new_srv->key = key;
        for (i = 0; i < __NR_LEVELS; i++) {
            mutex_init(&((new_srv->snd_locks)[i]));
            init_waitqueue_head(&((new_srv->lvl_queues)[i][0]));
            init_waitqueue_head(&((new_srv->lvl_queues)[i][1]));
            TAG_COND_INIT(&((new_srv->lvl_conds)[i]));
        }
        if (perm == __TAG_USR) {
            new_srv->perm_check = 0x1;
            new_srv->creator_euid = current_euid();
        }
        mutex_init(&(new_srv->awake_all_lock));
        TAG_COND_INIT(&(new_srv->globl_cond));
        // Add the new instance struct pointer to the static list.
        sem_ret = down_write_killable(&(tags_list[tag].rcv_rwsem));
        if (sem_ret == -EINTR) {
            if (key != __TAG_IPC_PRIVATE) up_write(&shared_bst_lock);
            kfree(new_srv);
            TAG_CLR(tags_mask, tag);
            return -EINTR;
        }
        sem_ret = down_write_killable(&(tags_list[tag].snd_rwsem));
        if (sem_ret == -EINTR) {
            up_write(&(tags_list[tag].rcv_rwsem));
            if (key != __TAG_IPC_PRIVATE) up_write(&shared_bst_lock);
            kfree(new_srv);
            TAG_CLR(tags_mask, tag);
            return -EINTR;
        }
        tags_list[tag].ptr = new_srv;
        up_write(&(tags_list[tag].snd_rwsem));
        up_write(&(tags_list[tag].rcv_rwsem));
        if (key != __TAG_IPC_PRIVATE) {
            // Now we make the modification visible by adding the new entry to
            // the BST.
            ins_res = splay_int_insert(shared_bst, key, tag);
            if (unlikely(ins_res == 0)) {
                // Insertion in the BST failed.
                // Now this is bad: we have to undo all that we just did.
                // Given how bad this is we don't admit interruptions.
                down_write(&(tags_list[tag].rcv_rwsem));
                down_write(&(tags_list[tag].snd_rwsem));
                tags_list[tag].ptr = NULL;
                up_write(&(tags_list[tag].snd_rwsem));
                up_write(&(tags_list[tag].rcv_rwsem));
                kfree(new_srv);
                TAG_CLR(tags_mask, tag);
                up_write(&shared_bst_lock);
                return -ENOMEM;
            }
            up_write(&shared_bst_lock);
            // TODO Debug.
            printk(KERN_DEBUG "%s: tag_get: Insert returned: %lu.\n",
                MODNAME, ins_res);
        }
        // TODO Debug.
        printk(KERN_DEBUG "%s: tag_get: New tag: %d.\n", MODNAME, tag);
        return tag;
    }
    return -EINVAL;
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
