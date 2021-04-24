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
#include <linux/uaccess.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#include <linux/sched.h>
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

extern unsigned int max_tags;
extern unsigned int max_msg_sz;

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
 * @return Static list index as tag descriptor, or an error code for errno.
 */
int aos_tag_get(int key, int cmd, int perm) {
    int tag, full = 0;
    SplayIntNode *search_res;
    tag_t *new_srv;
    unsigned int i;
    unsigned long ins_res;
    // TODO Debug.
    printk(KERN_DEBUG "%s: tag_get: Called with (%d, %d, %d).\n",
        MODNAME, key, cmd, perm);
    // Consistency check on input arguments.
    if ((cmd != __TAG_OPEN) && (cmd != __TAG_CREATE)) return -EINVAL;
    if ((perm != __TAG_ALL) && (perm != __TAG_USR)) return -EINVAL;
    // Normal operation basically follows one of two paths.
    if ((cmd == __TAG_OPEN) && (key != __TAG_IPC_PRIVATE)) {
        // We have been asked to reopen an instance, if it exists.
        if (down_read_killable(&shared_bst_lock) == -EINTR) return -EINTR;
        search_res =
            (SplayIntNode *)splay_int_search(shared_bst, key);
        if (search_res == NULL) {
            up_read(&shared_bst_lock);
            return -ENOKEY;
        }
        tag = search_res->_data;
        asm volatile ("lfence" ::: "memory");
        up_read(&shared_bst_lock);
        // TODO Debug.
        printk(KERN_DEBUG "%s: tag_get: Requested key: %d.\n", MODNAME, tag);
        return tag;
    }
    if (cmd == __TAG_CREATE) {
        // We have been asked to create a new instance.
        if (key != __TAG_IPC_PRIVATE) {
            // We have been asked to create a new shared instance.
            // We gotta lock the BST and look for the key first.
            if (down_write_killable(&shared_bst_lock) == -EINTR) return -EINTR;
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
            new_srv->creator_euid.val = current_euid().val;
        }
        mutex_init(&(new_srv->awake_all_lock));
        TAG_COND_INIT(&(new_srv->globl_cond));
        // Add the new instance struct pointer to the static list.
        if (unlikely(down_write_killable(&(tags_list[tag].rcv_rwsem))
            == -EINTR)) {
            if (key != __TAG_IPC_PRIVATE) up_write(&shared_bst_lock);
            kfree(new_srv);
            printk(KERN_CRIT "%s: tag_get: Killed during list access.\n",
                   MODNAME);
            printk(KERN_CRIT "%s: Module state corrupted, please remove.\n",
                   MODNAME);
            return -ENOTRECOVERABLE;
        }
        if (unlikely(down_write_killable(&(tags_list[tag].snd_rwsem))
            == -EINTR)) {
            up_write(&(tags_list[tag].rcv_rwsem));
            if (key != __TAG_IPC_PRIVATE) up_write(&shared_bst_lock);
            kfree(new_srv);
            printk(KERN_CRIT "%s: tag_get: Killed during list access.\n",
                   MODNAME);
            printk(KERN_CRIT "%s: Module state corrupted, please remove.\n",
                   MODNAME);
            return -ENOTRECOVERABLE;
        }
        tags_list[tag].ptr = new_srv;
        asm volatile ("sfence" ::: "memory");
        up_write(&(tags_list[tag].snd_rwsem));
        up_write(&(tags_list[tag].rcv_rwsem));
        if (key != __TAG_IPC_PRIVATE) {
            // Now we make the modification visible by adding the new entry to
            // the BST.
            ins_res = splay_int_insert(shared_bst, key, tag);
            up_write(&shared_bst_lock);
            if (unlikely(ins_res == 0)) {
                // Insertion in the BST failed.
                // Now this is bad: we have to undo all that we just did,
                // but we released locks.
                // We try to reacquire them and do this gracefully.
                int sem_ret1 = 0, sem_ret2 = 0;
                sem_ret1 = down_write_killable(&(tags_list[tag].rcv_rwsem));
                if (sem_ret1 != -EINTR)
                    sem_ret2 = down_write_killable(&(tags_list[tag].snd_rwsem));
                tags_list[tag].ptr = NULL;
                asm volatile ("sfence" ::: "memory");
                if (sem_ret1 != -EINTR) up_write(&(tags_list[tag].snd_rwsem));
                if (sem_ret2 != -EINTR) up_write(&(tags_list[tag].rcv_rwsem));
                kfree(new_srv);
                if ((sem_ret1 == -EINTR) || (sem_ret2 == -EINTR)) {
                    printk(KERN_CRIT "%s: tag_get: Killed while recovering "
                           "failed BST insert.\n", MODNAME);
                    printk(KERN_CRIT "%s: Module state corrupted, please "
                           "remove.\n", MODNAME);
                    return -ENOTRECOVERABLE;
                }
                TAG_CLR(tags_mask, tag);
                return -ENOMEM;
            }
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

/**
 * Allows a thread to receive a message from a level of an instance. 
 * The instance should have been previously opened with tag_get, however 
 * presence and permissions checks are always performed. 
 * The userspace buffer provided must be large enough to store the new message.
 *
 * @param tag Tag descriptor of the instance to access.
 * @param lvl Level of the aforementioned instance to receive from.
 * @param buf Userspace buffer in which to copy the new message.
 * @param size Size of the aforementioned buffer.
 * @return 0 if the message was successfully copied, or an error code for errno.
 */
int aos_tag_rcv(int tag, int lvl, char *buf, size_t size) {
    tag_t *tag_inst;
    unsigned char lvl_epoch, globl_epoch;
    int wait_res = 0;
    // TODO Debug.
    printk(KERN_INFO "%s: tag_receive: Called with (%d, %d, 0x%px, %lu).\n",
        MODNAME, tag, lvl, buf, size);
    // Consistency check on input arguments.
    if ((tag < 0) || (tag >= max_tags) || (buf == NULL) ||
        (lvl < 0) || (lvl >= __NR_LEVELS)) return -EINVAL;
    // First, check if the instance exists and we're allowed to access it.
    if (down_read_killable(&(tags_list[tag].rcv_rwsem)) == -EINTR)
        return -EINTR;
    tag_inst = tags_list[tag].ptr;
    if (tag_inst == NULL) {
        // Instance is not there anymore, or yet.
        up_read(&(tags_list[tag].rcv_rwsem));
        return -EIDRM;
    }
    if ((tag_inst->perm_check) && (current_euid().val != 0) &&
        (tag_inst->creator_euid.val != current_euid().val)) {
        // We're not allowed to receive messages from this instance.
        up_read(&(tags_list[tag].rcv_rwsem));
        return -EACCES;
    }
    // We're in.
    // Now let's register for the current local and global wait conditions.
    lvl_epoch = TAG_COND_REG(&((tag_inst->lvl_conds)[lvl]));
    globl_epoch = TAG_COND_REG(&(tag_inst->globl_cond));
    // TODO Debug.
    printk(KERN_DEBUG "%s: tag_receive: Local epoch: %d, global epoch: %d.\n",
           MODNAME, lvl_epoch, globl_epoch);
    // Now we can wait on our level's wait queue, keeping an eye out for both
    // the local and the global conditions, of the respective epochs.
    wait_res =
        wait_event_interruptible((tag_inst->lvl_queues)[lvl][lvl_epoch],
           ((TAG_COND_VAL(&((tag_inst->lvl_conds)[lvl]), lvl_epoch) == 0x1) ||
            (TAG_COND_VAL(&(tag_inst->globl_cond), globl_epoch) == 0x1)));
    // At this point we've been awoken!
    // Let's check what happened.
    if (wait_res == -ERESTARTSYS) {
        // We got a signal.
        TAG_COND_UNREG(&((tag_inst->lvl_conds)[lvl]), lvl_epoch);
        TAG_COND_UNREG(&(tag_inst->globl_cond), globl_epoch);
        up_read(&(tags_list[tag].rcv_rwsem));
        return -EINTR;
    }
    if (TAG_COND_VAL(&(tag_inst->globl_cond), globl_epoch) == 0x1) {
        // We got hit by an AWAKE_ALL.
        TAG_COND_UNREG(&((tag_inst->lvl_conds)[lvl]), lvl_epoch);
        TAG_COND_UNREG(&(tag_inst->globl_cond), globl_epoch);
        up_read(&(tags_list[tag].rcv_rwsem));
        // TODO Debug.
        printk(KERN_DEBUG "%s: tag_receive: Got hit by AWAKE_ALL.\n", MODNAME);
        return -EINTR;
    }
    // If we got here means that there's a message. Let's get to it.
    TAG_COND_UNREG(&(tag_inst->globl_cond), globl_epoch);
    if (tag_inst->mgs_sizes[lvl] != 0) {
        unsigned long not_copied = 0;
        // Remember that zero-length messages are allowed!
        // Must only check if the provided buffer is large enough.
        if (size < tag_inst->mgs_sizes[lvl]) {
            // Not enough.
            TAG_COND_UNREG(&((tag_inst->lvl_conds)[lvl]), lvl_epoch);
            up_read(&(tags_list[tag].rcv_rwsem));
            return -ENOBUFS;
        }
        not_copied = copy_to_user(buf, tag_inst->msg_bufs[lvl],
                                  tag_inst->mgs_sizes[lvl]);
        asm volatile ("mfence" ::: "memory");
        if (not_copied != 0) {
            // copy_to_user failed. Since it shouldn't, this service doesn't
            // retry, so the operation is aborted.
            TAG_COND_UNREG(&((tag_inst->lvl_conds)[lvl]), lvl_epoch);
            up_read(&(tags_list[tag].rcv_rwsem));
            return -ECANCELED;
        }
    }
    TAG_COND_UNREG(&((tag_inst->lvl_conds)[lvl]), lvl_epoch);
    up_read(&(tags_list[tag].rcv_rwsem));
    // TODO Debug.
    printk(KERN_DEBUG "%s: tag_receive: Got message from tag: %d, on level "
           "%d.\n", MODNAME, tag, lvl);
    return 0;
}

int aos_tag_snd(int tag, int lvl, char *buf, size_t size) {
    // TODO Debug.
    printk(KERN_INFO "%s: tag_send: Called with (%d, %d, 0x%px, %lu).\n",
        MODNAME, tag, lvl, buf, size);
    return 0;
}

/**
 * Once the tag descriptor has been retrieved via tag_get, allows to control 
 * an instance.
 * Supported commands are: 
 * - TAG_REMOVE: Deletes the instance, freeing the related tag descriptor. 
 * - TAG_AWAKE_ALL: Awakes all threads waiting on all levels. 
 * Returns 0 if the operation was successfully completed, or an error code 
 * used to set errno.
 *
 * @param tag Tag descriptor of the instance to operate on.
 * @param cmd Operation to perform on the instance.
 * @return 0 if operation completed successfully, or an error code for errno.
 */
int aos_tag_ctl(int tag, int cmd) {
    tag_t *tag_inst;
    // TODO Debug.
    printk(KERN_DEBUG "%s: tag_ctl: Called with (%d, %d).\n",
           MODNAME, tag, cmd);
    // Consistency check on input arguments.
    if ((tag < 0) || (tag >= max_tags) ||
        ((cmd != __TAG_REMOVE) && (cmd != __TAG_AWAKE_ALL)))
        return -EINVAL;
    // Execution will follow one of the next paths.
    if (cmd == __TAG_AWAKE_ALL) {
        unsigned char last_epoch;
        unsigned int i;
        // We have been asked to awake all threads waiting on all levels.
        if (down_read_killable(&(tags_list[tag].snd_rwsem)) == -EINTR)
            return -EINTR;
        tag_inst = tags_list[tag].ptr;
        if (tag_inst == NULL) {
            // Instance is not there anymore, or yet.
            up_read(&(tags_list[tag].snd_rwsem));
            return -EIDRM;
        }
        if ((tag_inst->perm_check) && (current_euid().val != 0) &&
            (tag_inst->creator_euid.val != current_euid().val)) {
            // We're not allowed to operate on this instance.
            up_read(&(tags_list[tag].snd_rwsem));
            return -EACCES;
        }
        // Grab the AWAKE_ALL lock to exclude others.
        if (mutex_lock_interruptible(&(tag_inst->awake_all_lock)) == -EINTR) {
            up_read(&(tags_list[tag].snd_rwsem));
            return -EINTR;
        }
        // Change the current global epoch for this instance.
        // This is a linearization point: all receivers that come after this
        // won't get the call: they were too late.
        last_epoch = TAG_COND_FLIP(&(tag_inst->globl_cond));
        TAG_COND_VAL(&(tag_inst->globl_cond), last_epoch) = 0x1;
        asm volatile ("sfence" ::: "memory");
        // Wake up all levels, both queues since we don't know which reader
        // got in which local epoch and we don't want to care.
        for (i = 0; i < __NR_LEVELS; i++) {
            wake_up_all(&((tag_inst->lvl_queues)[i][0]));
            wake_up_all(&((tag_inst->lvl_queues)[i][1]));
        }
        // Wait for receivers to consume the condition.
        // Since busy-wait loops are bad in the kernel let the scheduler run
        // some other task on this CPU in the meantime.
        while (TAG_COND_COUNT(&(tag_inst->globl_cond), last_epoch) > 0)
            schedule();
        // All done!
        mutex_unlock(&(tag_inst->awake_all_lock));
        up_read(&(tags_list[tag].snd_rwsem));
        // TODO Debug.
        printk(KERN_DEBUG "%s: tag_ctl: Awoken all receivers on tag: %d.\n",
               MODNAME, tag);
    }
    if (cmd == __TAG_REMOVE) {
        // We have been asked to remove an instance.
        // But first, check if someone is there, waiting to read.
        if (down_write_trylock(&(tags_list[tag].rcv_rwsem)) == 0) return -EBUSY;
        if (down_write_killable(&(tags_list[tag].snd_rwsem)) == -EINTR) {
            up_write(&(tags_list[tag].rcv_rwsem));
            return -EINTR;
        }
        // We're in. Check if the instance is there and whether we can
        // access it or not, as above.
        tag_inst = tags_list[tag].ptr;
        if (tag_inst == NULL) {
            up_write(&(tags_list[tag].snd_rwsem));
            up_write(&(tags_list[tag].rcv_rwsem));
            return -EIDRM;
        }
        if ((tag_inst->perm_check) && (current_euid().val != 0) &&
            (tag_inst->creator_euid.val != current_euid().val)) {
            up_write(&(tags_list[tag].snd_rwsem));
            up_write(&(tags_list[tag].rcv_rwsem));
            return -EACCES;
        }
        // We got this. Just disconnect the instance ASAP.
        tags_list[tag].ptr = NULL;
        asm volatile ("mfence" ::: "memory");
        up_write(&(tags_list[tag].snd_rwsem));
        up_write(&(tags_list[tag].rcv_rwsem));
        // Ok, now let's cut all references: BST and bitmask.
        if (tag_inst->key != __TAG_IPC_PRIVATE) {
            // Remove this key from the BST.
            if (unlikely(down_write_killable(&shared_bst_lock) == -EINTR)) {
                // This path exists only as a failsafe.
                // We try to at least free the memory, but module state will
                // be left broken.
                kfree(tag_inst);
                printk(KERN_CRIT "%s: tag_ctl: Killed during BST access.\n",
                       MODNAME);
                printk(KERN_CRIT "%s: Module state corrupted, please remove.\n",
                       MODNAME);
                return -ENOTRECOVERABLE;
            }
            if (!splay_int_delete(shared_bst, tag_inst->key))
                printk(KERN_ERR "%s: tag_ctl: Couldn't remove key %d, with tag"
                       " %d.\n", MODNAME, tag_inst->key, tag);
            up_write(&shared_bst_lock);
            // TODO Debug.
            printk(KERN_DEBUG "%s: tag_ctl: Deleted key: %d from BST.\n",
                   MODNAME, tag_inst->key);
        }
        TAG_CLR(tags_mask, tag);
        tag_inst->creator_euid.val = 0;  // For security.
        kfree(tag_inst);  // Done!
        // TODO Debug.
        printk(KERN_DEBUG "%s: tag_ctl: Removed tag: %d.\n", MODNAME, tag);
    }
    return 0;
}
