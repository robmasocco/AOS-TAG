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
 * @brief Declarations of system calls included in this module.
 *        Signatures and values in here follow the same layout in
 *        corresponding userspace headers.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date April 10, 2021
 */

#ifndef AOS_TAG_SYSCALLS_H
#define AOS_TAG_SYSCALLS_H

#include <linux/types.h>

int aos_tag_get(int key, int cmd, int perm);
int aos_tag_rcv(int tag, int lvl, char *buf, size_t size);
int aos_tag_snd(int tag, int lvl, char *buf, size_t size);
int aos_tag_ctl(int tag, int cmd);

#endif
