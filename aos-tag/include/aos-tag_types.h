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
 * @brief Definitions of data types required by this module.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date April 10, 2021
 */

#ifndef AOS_TAG_TYPES_H
#define AOS_TAG_TYPES_H

#include <linux/types.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/cred.h>
#include <linux/wait.h>

#include "aos-tag.h"
#include "../utils/aos-tag_conditions.h"

/** 
 * Instance structure.
 * Holds metadata for instance management.
 */
typedef struct _tag_t {
    int key;                                       // Instance key.
    char *msg_bufs[__NR_LEVELS];                   // Pointers to messages.
    size_t mgs_sizes[__NR_LEVELS];                 // Sizes of active messages.
    struct mutex snd_locks[__NR_LEVELS];           // Locks for senders.
    wait_queue_head_t lvl_queues[__NR_LEVELS][2];  // Level wait queues.
    tag_cond_t lvl_conds[__NR_LEVELS];             // Level wait conditions.
    kuid_t creator_euid;                           // Instance creator EUID.
    char perm_check;                               // Enables permissions check.
    struct mutex awake_all_lock;                   // Lock for AWAKE_ALL.
    tag_cond_t globl_cond;                         // AWAKE_ALL condition.
} tag_t;

/**
 * Instances array entry.
 * Enables access to an instance, active or not.
 */
typedef struct _tag_ptr_t {
    tag_t *ptr;                      // Pointer to the corresponding instance.
    struct rw_semaphore rcv_rwsem;   // For receivers as readers.
    struct rw_semaphore snd_rwsem;   // For senders as readers.
} tag_ptr_t;

#endif
