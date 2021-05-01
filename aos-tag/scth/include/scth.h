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
 * @brief Header file for the "System Call Table Hacker" library.
 *        A small set of routines that track the position of the Table in
 *        kernel memory and provide interfaces to replace its entries, and
 *        then restore them.
 *        This library exposes the following functions, plus some module
 *        parameters that you can find in the related source file. Some of them
 *        may be specified at boot to reconfigure the table search.
 *
 * @author Roberto Masocco
 *
 * @date February 8, 2021
 */

#ifndef SCT_HACKER_H
#define SCT_HACKER_H

void **scth_finder(void);
void scth_cleanup(void);
int scth_hack(void *new_call_addr);
void scth_unhack(int to_restore);

#endif
