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
 * @brief Definitions of the "condition" data type and related macros.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date April 10, 2021
 */

#ifndef AOS_TAG_CONDITIONS_H
#define AOS_TAG_CONDITIONS_H

#include <linux/spinlock.h>

/**
 * This structure makes RCU-like wakeup schemes work. 
 * As in RCU, we have an epoch selector and presence counters, but in addition 
 * we have two condition values, one for each epoch, used in wait calls. 
 * The spinlock is also required to prevent some bad race conditions, 
 * described in the notes together with the choice of a spinlock over an rwlock.
 */
typedef struct _tag_cond_t {
    unsigned char _cond_epoch;      // Epoch selector.
    unsigned char _conditions[2];   // Conditions used in wakeups.
    unsigned long _pres_count[2];   // (Atomic) presence counters for epochs.
    spinlock_t _lock;               // Lock to prevent "multi-epoch jumps".
} tag_cond_t;

/**
 * @brief Initializes a given condition struct.
 *
 * @param cond_addr Address of the tag_cond to initialize.
 */
#define TAG_COND_INIT(cond_addr)                  \
    do {                                          \
        (cond_addr)->_cond_epoch = 0;             \
        (cond_addr)->_conditions[0] = 0;          \
        (cond_addr)->_conditions[1] = 0;          \
        (cond_addr)->_pres_count[0] = 0;          \
        (cond_addr)->_pres_count[1] = 0;          \
        spin_lock_init(&((cond_addr)->_lock));    \
    } while (0)

/**
 * @brief Registers the calling thread on the current epoch. 
 * Returns the epoch on which the thread got registered. 
 * NOTE: Increments need to be atomic even if we hold the lock since the next 
 *       macro, which decrements, does so without holding any lock. 
 *       As for the epoch selector, the spinlock protects it.
 *
 * @param cond_addr Address of the tag_cond to register on.
 * @return Current epoch's selector.
 */
#define TAG_COND_REG(cond_addr) ({                                \
    unsigned char __epoch_sel;                                    \
    spin_lock(&((cond_addr)->_lock));                             \
    __epoch_sel = (cond_addr)->_cond_epoch;                       \
    __atomic_add_fetch(&((cond_addr)->_pres_count[__epoch_sel]),  \
        1, __ATOMIC_RELAXED);                                     \
    asm volatile ("mfence" ::: "memory");                         \
    spin_unlock(&((cond_addr)->_lock));                           \
    __epoch_sel; })

/**
 * @brief Unregisters the calling thread from the specified epoch of the given 
 * condition struct.
 *
 * @param cond_addr Address of the tag_cond to operate on.
 * @param epoch Epoch selector of the epoch to unregister from.
 */
#define TAG_COND_UNREG(cond_addr, epoch)                     \
    __atomic_sub_fetch(&((cond_addr)->_pres_count[epoch]),   \
        1, __ATOMIC_RELAXED);

/**
 * @brief Flips the given tag_cond's epoch, and returns the selector of the old 
 * epoch. Also resets the new epoch's condition.
 *
 * @param cond_addr Address of the tag_cond to operate on.
 * @return Epoch selector of the now "old" epoch.
 */
#define TAG_COND_FLIP(cond_addr) ({               \
    unsigned char __last_epoch, __new_epoch;      \
    spin_lock(&((cond_addr)->_lock));             \
    __last_epoch = (cond_addr)->_cond_epoch;      \
    __new_epoch = __last_epoch ^ 0x1;             \
    (cond_addr)->_cond_epoch = __new_epoch;       \
    (cond_addr)->_conditions[__new_epoch] = 0x0;  \
    asm volatile ("mfence" ::: "memory");         \
    spin_unlock(&((cond_addr)->_lock));           \
    __last_epoch; })

/**
 * @brief Evaluates to the condition value of the specified epoch. 
 * Can be used both as an rvalue and an lvalue. 
 * Yes, I'm that lazy.
 *
 * @param cond_addr Address of the given tag_cond.
 * @param epoch Epoch selector of the condition to get.
 * @return Value of the specified condition, by direct evaluation.
 */
#define TAG_COND_VAL(cond_addr, epoch) ((cond_addr)->_conditions)[epoch]

/**
 * @brief Evaluates to the presence counter of the specified epoch. 
 * Made to be typically used as an lvalue.
 * This time it was for neatness and not laziness.
 *
 * @param cond_addr Address of the given tag_cond.
 * @param epoch Epoch selector of the condition to get.
 * @return Presence counter of the specified epoch, by direct evaluation.
 */
#define TAG_COND_COUNT(cond_addr, epoch) ((cond_addr)->_pres_count)[epoch]

#endif
