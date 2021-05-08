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
 * @brief Definition of common macros, data and user stubs for this module.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date April 25, 2021
 */

#ifndef AOS_TAG_H
#define AOS_TAG_H

#ifdef __KERNEL__
/* KERNEL MODULE HEADER */

/* Module name. */
#define MODNAME "AOS-TAG"

/* Default sizes of module internal structures. */
#define __NR_LEVELS 32         // Number of levels in an instance.
#define __MAX_TAGS_DFL 256     // Default max number of active instances.
#define __MAX_MSG_SZ_DFL 4096  // Default max message size, in bytes.

/* tag_get commands and special keys. */
#define __TAG_OPEN 0
#define __TAG_CREATE 1
#define __TAG_ALL 0
#define __TAG_USR 1
#define __TAG_IPC_PRIVATE 0  // This value is coeherent with sys/ipc.h.

/* tag_ctl commands. */
#define __TAG_AWAKE_ALL 0
#define __TAG_REMOVE 1

#else
/* USERSPACE HEADER */

/* System calls numbers. */
#ifndef __NR_tag_get
#define __NR_tag_get 134
#endif

#ifndef __NR_tag_receive
#define __NR_tag_receive 174
#endif

#ifndef __NR_tag_send
#define __NR_tag_send 177
#endif

#ifndef __NR_tag_ctl
#define __NR_tag_ctl 178
#endif

/* Commands and special values for system calls. */
/* tag_get commands and special keys. */
#define TAG_OPEN 0
#define TAG_CREATE 1
#define TAG_ALL 0
#define TAG_USR 1

/* tag_ctl commands. */
#define AWAKE_ALL 0
#define REMOVE 1

#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/ipc.h>

/* Userspace system calls stubs. */

/**
 * @brief Opens a new instance of the service. 
 * Instances can be shared or not, depending on the value of key. 
 * An instance can be created or reopened, depending on the value of cmd. 
 * With perm, it is possible to specify whether permission checks should be 
 * peformed to limit access to threads executing on behalf of the same user 
 * that created the instance. 
 * Use the TAG_* flags for command and permission.
 *
 * @param key Key to assign to the new instance, or to look for.
 * @param cmd Open a new instance, or look for an existing one.
 * @param perm Enables EUID checks for following operations.
 * @return Tag descriptor, or -1 and errno will be set.
 */
static inline int tag_get(int key, int command, int permission) {
    errno = 0;
    return syscall(__NR_tag_get, key, command, permission);
}

/**
 * @brief Allows a thread to receive a message from a level of an instance. 
 * The instance should have been previously opened with tag_get, however 
 * presence and permissions checks are always performed. 
 * The provided buffer must be large enough to store the new message.
 *
 * @param tag Tag descriptor of the instance to access.
 * @param lvl Level of the aforementioned instance to receive from.
 * @param buf Buffer in which to copy the new message.
 * @param size Size of the aforementioned buffer.
 * @return Size of the message if successful, or -1 and errno will be set.
 */
static inline int tag_receive(int tag, int level, char *buffer, size_t size) {
    errno = 0;
    return syscall(__NR_tag_receive, tag, level, buffer, size);
}

/**
 * @brief Allows a thread to send a message on a level of an instance. 
 * The instance should have been previously opened with tag_get, however 
 * presence and permissions checks are always performed. 
 * I/O is packetized: the entire size of the buffer provided will 
 * be copied for distribution to readers. The operation will fail if this is 
 * not possible. 
 * Note that zero-length messages are allowed.
 *
 * @param tag Tag descriptor of the instance to access.
 * @param lvl Level of the aforementioned instance to write into.
 * @param buf Buffer holding the message to send.
 * @param size Size of the aforementioned buffer.
 * @return 0 if the message was successfully delivered, 1 if no one was there or
 * -1 and errno will be set.
 */
static inline int tag_send(int tag, int level, char *buffer, size_t size) {
    errno = 0;
    return syscall(__NR_tag_send, tag, level, buffer, size);
}

/**
 * @brief Once the tag descriptor has been retrieved via tag_get, 
 * allows to control an instance. 
 * Supported commands are: 
 * - REMOVE: Deletes the instance, freeing the related tag descriptor. 
 * - AWAKE_ALL: Awakes all threads waiting on all levels. 
 * Use the TAG_* flags for command.
 *
 * @param tag Tag descriptor of the instance to operate on.
 * @param cmd Operation to perform on the instance.
 * @return 0 if successful, or -1 and errno will be set.
 */
static inline int tag_ctl(int tag, int command) {
    errno = 0;
    return syscall(__NR_tag_ctl, tag, command);
}

#endif

#endif
