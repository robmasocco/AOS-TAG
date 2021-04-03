/* 
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
/* 
 * @brief Header file for the "paging_navigator" routine.
 *        Tells if a paged virtual address is mapped on a physical frame, and
 *        in case returns the corresponding physical frame number.
 *        Works on x86-64 machines in long mode with 4-level paging.
 * 
 * @author Roberto Masocco
 * 
 * @date February 1, 2021
 */

#ifndef _PAGING_NAVIGATOR_H
#define _PAGING_NAVIGATOR_H

#define NOMAP -1

long paging_navigator(unsigned long vaddr);

#endif
