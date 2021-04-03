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
 * @brief Header file for a small library of functions and macros to access
 *        control registers and perform some architecture-specific tasks on
 *        x86 machines.
 *        They're both defined and declared here since they're inlined, as
 *        static symbols to avoid compiler complaints about multiple
 *        definitions.
 * 
 * @author Roberto Masocco
 * 
 * @date February 1, 2021
 */

#ifndef _X86_UTILS_H
#define _X86_UTILS_H

#include <linux/irqflags.h>

#define __X86_CR0_WP 0x10000

/* 
 * Returns the content of the CR3 register: physical address of the current
 * page table in main memory.
 * 
 * @return Physical address of top level page table (PML4 on x86-64).
 */
static inline unsigned long __x86_read_cr3(void) __attribute__((always_inline));
static inline unsigned long __x86_read_cr3(void) {
    unsigned long cr3 = 0;
    asm volatile (
        "xor %%rax, %%rax\n\t"
        "mov %%cr3, %%rax\n\t"
        : "=a" (cr3)
        :
        :
    );
    return cr3;
}

/* 
 * Disables Write Protection on x86 CPUs, clearing the WP bit in CR0.
 * WARNING: To keep machine state consistent, this disables IRQs too, saving
 * their disabled state in the provided variable. Is meant to be used to circle
 * some really critical, deterministic, nonblocking and short code.
 *
 * @param flags unsigned long in which to store IRQ state.
 */
#define __x86_wp_disable(flags)        \
    do {                               \
        local_irq_save(flags);         \
        asm volatile (                 \
            "xor %%rax, %%rax\n\t"     \
            "xor %%rbx, %%rbx\n\t"     \
            "mov %%cr0, %%rax\n\t"     \
            "mov %0, %%rbx\n\t"        \
            "not %%rbx\n\t"            \
            "and %%rbx, %%rax\n\t"     \
            "mov %%rax, %%cr0\n\t"     \
            :                          \
            : "i" (__X86_CR0_WP)       \
            : "rax", "rbx"             \
        );                             \
	} while (0)

/* 
 * Enables Write Protection on x86 CPUs, setting the WP bit in CR0.
 * WARNING: According to its dual above, this reenables IRQs, restoring the
 * saved state provided.
 *
 * @param flags unsigned long that holds IRQ state to restore.
 */
#define __x86_wp_enable(flags)         \
    do {                               \
        asm volatile (                 \
            "xor %%rax, %%rax\n\t"     \
            "xor %%rbx, %%rbx\n\t"     \
            "mov %%cr0, %%rax\n\t"     \
            "mov %0, %%rbx\n\t"        \
            "or %%rbx, %%rax\n\t"      \
            "mov %%rax, %%cr0\n\t"     \
            :                          \
            : "i" (__X86_CR0_WP)       \
            : "rax", "rbx"             \
        );                             \
        local_irq_restore(flags);      \
    } while (0)

#endif
