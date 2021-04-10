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
 * @brief Source file for the "System Call Table Hacker" library.
 *        See related header file.
 *
 * @author Roberto Masocco
 *
 * @date February 8, 2021
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/compiler.h>

#include "utils/x86_utils.h"
#include "utils/paging_navigator.h"
#include "include/scth.h"

#define MODNAME "SCTH"

#define __PAGE_SIZE 4096
#define __PAGE_MASK 0xfffffffffffff000ULL

/*
 * Virtual kernel memory addresses at which the search starts and ends.
 * Note that this search covers 4 GiB of the kernel virtual address space,
 * where we expect to find the kernel's data segment as loaded at boot as
 * per the System.map contents nowadays.
 */
#define KERNEL_START_ADDR ((void *)0xffffffff00000000ULL)
#define KERNEL_END_ADDR   ((void *)0xfffffffffff00000ULL)

/* Structure to hold information about a hackable entry in the table. */
struct scth_entry {
    int tab_index;
    unsigned char hacked : 1;
};

/* Total legit entries in the table (get this from "syscall_64.tbl" file). */
int tab_entries = 256;
module_param(tab_entries, int, S_IRUGO);
MODULE_PARM_DESC(tab_entries, "Number of legit entries in the system call "
                 "table.");

/* Exposed addresses (for the root user only). */
unsigned long sys_call_table_addr = 0x0ULL;
unsigned long sys_ni_syscall_addr = 0x0ULL;
module_param(sys_call_table_addr, ulong, S_IRUSR | S_IRGRP);
module_param(sys_ni_syscall_addr, ulong, S_IRUSR | S_IRGRP);
MODULE_PARM_DESC(sys_call_table_addr, "Virtual address of the system"
                 " call table.");
MODULE_PARM_DESC(sys_ni_syscall_addr, "Virtual address of the \"ni\" syscall.");

/* Array of known "ni" entries in the table. The more, the better.*/
int known_sysnis[] = {134, 174, 182, 183, 214, 215, 236};
int nr_known_sysnis = 7;
module_param(nr_known_sysnis, int, S_IRUGO);
module_param_array(known_sysnis, int, &nr_known_sysnis, S_IRUGO);
MODULE_PARM_DESC(nr_known_sysnis, "Number of entries pointing to "
                 "\"ni\" syscall.");
MODULE_PARM_DESC(known_sysnis, "Indexes of entries pointing to "
                 "\"ni\" syscall.");

/* Array of discovered "ni" entries in the table, ready to be hacked. */
struct scth_entry *avail_sysnis = NULL;
int nr_sysnis = 0;
module_param(nr_sysnis, int, S_IRUGO);
MODULE_PARM_DESC(nr_sysnis, "Number of hackable entries in the "
                 "syscall table.");

/*
 * Library cleanup routine: restores entries and frees memory.
 */
void scth_cleanup(void) {
    int i = 0;
    unsigned long flags;
    void **table_addr = (void **)sys_call_table_addr;
    if (avail_sysnis == NULL) return;
    for (; i < nr_sysnis; i++) {
        if (avail_sysnis[i].hacked) {
            __x86_wp_disable(flags);
            table_addr[avail_sysnis[i].tab_index] = (void *)sys_ni_syscall_addr;
            __x86_wp_enable(flags);
            printk(KERN_INFO "%s: Restored entry %d.\n",
                   MODNAME, avail_sysnis[i].tab_index);
        }
    }
    kfree(avail_sysnis);
    avail_sysnis = NULL;
    printk(KERN_INFO "%s: System call table restored.\n", MODNAME);
}

/*
 * Routine to replace a free entry in the table with a pointer to some other
 * function.
 *
 * @param new_call_addr Pointer to replace.
 * @return Index of the new system call, or -1 if there's no room left.
 */
int scth_hack(void *new_call_addr) {
    int i = 0, new_call_index;
    unsigned long flags;
    void **table_addr = (void **)sys_call_table_addr;
    // Consistency check on input arguments.
    if (avail_sysnis == NULL) return -1;
    for (; i < nr_sysnis; i++) {
        if (!(avail_sysnis[i].hacked)) {
            new_call_index = avail_sysnis[i].tab_index;
            __x86_wp_disable(flags);
            table_addr[new_call_index] = new_call_addr;
            __x86_wp_enable(flags);
            avail_sysnis[i].hacked = 1;
            printk(KERN_INFO "%s: Hacked entry %d.\n", MODNAME, new_call_index);
            return new_call_index;
        }
    }
    return -1;
}

/*
 * Routine to restore an entry in the table.
 *
 * @param to_restore Index of the entry to restore.
 */
void scth_unhack(int to_restore) {
    int i = 0;
    unsigned long flags;
    void **table_addr = (void **)sys_call_table_addr;
    // Consistency check on input arguments.
    if ((to_restore < 0) || (avail_sysnis == NULL)) return;
    for (; i < nr_sysnis; i++) {
        if ((avail_sysnis[i].tab_index == to_restore) &&
            avail_sysnis[i].hacked) {
            avail_sysnis[i].hacked = 0;
            __x86_wp_disable(flags);
            table_addr[to_restore] = (void *)sys_ni_syscall_addr;
            __x86_wp_enable(flags);
            printk(KERN_INFO "%s: Restored entry %d.\n", MODNAME, to_restore);
            return;
        }
    }
}

/*
 * Scans the system call table and determines which entries can be hacked later.
 *
 * @param table_addr Virtual address of the system call table.
 */
void scth_scan_table(void **table_addr) {
    int ni_count = 0;
    void *first_sysni = table_addr[known_sysnis[0]];
    int i, j = 0;
    // First pass: determine how many entries there are.
    for (i = known_sysnis[0]; i < tab_entries; i++)
        if (table_addr[i] == first_sysni) ni_count++;
    avail_sysnis = (struct scth_entry *)kzalloc(
                    ni_count * sizeof(struct scth_entry),
                    GFP_KERNEL);
    nr_sysnis = ni_count;
    // Second pass: populate the array.
    for (i = known_sysnis[0]; i < tab_entries; i++) {
        if (table_addr[i] == first_sysni) {
            avail_sysnis[j].tab_index = i;
            j++;
        }
    }
}

/*
 * Checks whether a candidate address could point to the system call table by
 * looking at the entries we know should point to "ni_syscall".
 *
 * @param addr Virtual address to check.
 * @return Yes or no.
 */
int scth_pattern_check(void **addr) {
    void *first_sysni;
    int i;
    first_sysni = addr[known_sysnis[0]];
    for (i = 1; i < nr_known_sysnis; i++)
        if (addr[known_sysnis[i]] != first_sysni) return 0;
    return 1;
}

/*
 * Checks whether a candidate address could point to the system call table by
 * ensuring that "ni_syscall" is pointed only where it should be, especially
 * not before the first entry we know.
 *
 * @param addr Virtual address to check.
 * @return Yes or no.
 */
int scth_prev_area_check(void **addr) {
    int i;
    for (i = 0; i < known_sysnis[0]; i++)
        if (addr[i] == addr[known_sysnis[0]]) return 0;
    return 1;
}

/*
 * Checks whether a given page could contain (part of) the system call table,
 * performing a linear pattern matching scan. Returns the table base address,
 * if found.
 *
 * @param pg Virtual address of the page to check.
 * @return Virtual base address of the UNISTD_64 system call table.
 */
void **scth_check_page(void *pg) {
    unsigned long i;
    void *sec_page, **candidate;
    for (i = 0; i < __PAGE_SIZE; i += sizeof(void *)) {
        // If the table may span over two pages, check the second one.
        sec_page = pg + i +
                   (known_sysnis[nr_known_sysnis - 1] * sizeof(void *));
        if (((ulong)(pg + __PAGE_SIZE) == ((ulong)sec_page & __PAGE_MASK)) && 
            (paging_navigator((unsigned long)sec_page) == NOMAP))
            return NULL;
        // Now we can only go for pattern matching.
        candidate = pg + i;
        if ((candidate[known_sysnis[0]] != 0x0) &&
            (((ulong)candidate[known_sysnis[0]] & 0x3) == 0x0) &&
            (candidate[known_sysnis[0]] > KERNEL_START_ADDR) &&
            scth_pattern_check(candidate) &&
            scth_prev_area_check(candidate))
            return candidate;
    }
    return NULL;
}

/*
 * This function looks for the system call table searching kernel memory in a
 * linear fashion. It relies, together with previous routines, on the following
 * assumptions:
 * 1 - We can start the search at KERNEL_START_ADDR.
 * 2 - When the kernel image is loaded in memory, relative offsets between
 *     elements aren't randomized even if KASLR or similar are enabled.
 * 3 - Table entries are 8-bytes long and aligned.
 * 4 - Entries in "known_sysnis" point to "ni_syscall". Since layout is
 *     subject to change over time, check the "syscall_64.tbl" file.
 *
 * @return UNISTD_64 system call table virtual address, or 0 if search fails.
 */
void **scth_finder(void) {
    void *pg, **addr;
    for (pg = KERNEL_START_ADDR; pg < KERNEL_END_ADDR; pg += __PAGE_SIZE) {
        // Do a simple linear search in the canonical higher half of virtual
        // memory, previously checking that the target address is mapped to
        // avoid General Protection Errors, page by page.
        if ((paging_navigator((unsigned long)pg) != NOMAP) &&
            ((addr = scth_check_page(pg)) != NULL)) {
            printk(KERN_INFO
                   "%s: UNISTD_64 system call table found at: 0x%px.\n",
                   MODNAME, addr);
            sys_call_table_addr = (unsigned long)addr;
            sys_ni_syscall_addr = (unsigned long)(addr[known_sysnis[0]]);
            scth_scan_table(addr);
            return addr;
        }
    }
    printk(KERN_ERR "%s: UNISTD_64 system call table not found.\n", MODNAME);
    return NULL;
}

/* Symbols this library makes available to other modules. */
EXPORT_SYMBOL(scth_finder);
EXPORT_SYMBOL(scth_cleanup);
EXPORT_SYMBOL(scth_hack);
EXPORT_SYMBOL(scth_unhack);
