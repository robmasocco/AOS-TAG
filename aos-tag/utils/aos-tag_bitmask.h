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
 * @brief Set of macros to interact with the instance bitmask.
 *
 * @author Roberto Masocco
 *
 * @date April 3, 2021
 */

#ifdef __KERNEL__
#include <linux/slab.h>
#include <linux/spinlock.h>
#else
#include <stdlib.h>
#endif

/* Structure that holds a bitmask and metadata to quickly manage it. */
typedef struct _tag_bitmask {
    unsigned long *_mask;  /* Actual mask. Must be set manually outside. */
    int _nr_tags;          /* Number of valid bits in the mask. */
    int _mask_len;         /* Number of ulongs that compose the mask. */
#ifdef __KERNEL__
    spinlock_t _lock;      /* Lock to synchronize accesses to the mask. */
#endif
} tag_bitmask;

/*
 * Creates a tag bitmask capable of holding the specified number of elements.
 *
 * @param nr_tags Number of valid bits to hold.
 * @return Address of the new bitmask.
 */
#ifndef __KERNEL__
#define TAG_MASK_CREATE(nr_tags) ({                                          \
    tag_bitmask *new_mask;                                                   \
    new_mask = (tag_bitmask *)calloc(1, sizeof(tag_bitmask));                \
    if (new_mask != NULL) {                                                  \
        new_mask->_nr_tags = nr_tags;                                        \
        new_mask->_mask_len = nr_tags / (sizeof(unsigned long) * 8);         \
        if (nr_tags % (sizeof(unsigned long) * 8)) new_mask->_mask_len++;    \
        new_mask->_mask = (unsigned long *)calloc(new_mask->_mask_len,       \
                                                  sizeof(unsigned long));    \
        if (new_mask->_mask == NULL) {                                       \
            free(new_mask);                                                  \
            new_mask = NULL;                                                 \
        }                                                                    \
    }                                                                        \
    new_mask; })
#else
#define TAG_MASK_CREATE(nr_tags) ({                                          \
    tag_bitmask *new_mask;                                                   \
    new_mask = (tag_bitmask *)kzalloc(sizeof(tag_bitmask), GFP_KERNEL);      \
    if (new_mask != NULL) {                                                  \
        new_mask->_nr_tags = nr_tags;                                        \
        new_mask->_mask_len = nr_tags / (sizeof(unsigned long) * 8);         \
        if (nr_tags % (sizeof(unsigned long) * 8)) new_mask->_mask_len++;    \
        new_mask->_mask = (unsigned long *)kzalloc(                          \
            new_mask->_mask_len * sizeof(unsigned long),                     \
            GFP_KERNEL);                                                     \
        if (new_mask->_mask == NULL) {                                       \
            kfree(new_mask);                                                 \
            new_mask = NULL;                                                 \
        }                                                                    \
    }                                                                        \
    new_mask; })
#endif

/*
 * Removes a given tag bitmask, freeing memory.
 *
 * @param mask Tag mask to free.
 */
#ifndef __KERNEL__
#define TAG_MASK_FREE(mask)                                                  \
    do {                                                                     \
        free(mask->_mask);                                                   \
        free(mask);                                                          \
    } while (0)
#else
#define TAG_MASK_FREE(mask)                                                  \
    do {                                                                     \
        kfree(mask->_mask);                                                  \
        kfree(mask);                                                         \
    } while (0)
#endif

/*
 * Sets a specific bit in the bitmask.
 * NOTE: No validity check on the index is performed!
 *
 * @param tag_mask Address of the bitmask.
 * @param tag_desc Index of the bit to set.
 */
#define TAG_SET(tag_mask, tag_desc)                                          \
    do {                                                                     \
        int ulong_indx, bit_indx;                                            \
        ulong_indx = tag_desc / (sizeof(unsigned long) * 8);                 \
        unsigned long tag_ulong = (tag_mask->_mask)[ulong_indx];             \
        bit_indx = tag_desc - (ulong_indx * (sizeof(unsigned long) * 8));    \
        tag_ulong |= (0x1UL << bit_indx);                                    \
        (tag_mask->_mask)[ulong_indx] = tag_ulong;                           \
    } while (0)

/*
 * Clears a specific bit in the bitmask.
 * NOTE: No validity check on the index is performed!
 *
 * @param tag_mask Address of the bitmask.
 * @param tag_desc Index of the bit to set.
 */
#define TAG_CLR(tag_mask, tag_desc)                                          \
    do {                                                                     \
        int ulong_indx, bit_indx;                                            \
        ulong_indx = tag_desc / (sizeof(unsigned long) * 8);                 \
        unsigned long tag_ulong = (tag_mask->_mask)[ulong_indx];             \
        bit_indx = tag_desc - (ulong_indx * (sizeof(unsigned long) * 8));    \
        tag_ulong &= (~0x0UL) ^ (0x1UL << bit_indx);                         \
        (tag_mask->_mask)[ulong_indx] = tag_ulong;                           \
    } while (0)

/*
 * Returns the index of the first zero bit in the bitmask, or -1.
 * For the sake of speed, the bit is also set to 1.
 * NOTE: Validity check is performed here, since the mask length could
 *       exceed the number of valid positions in the array.
 *
 * @param tag_mask Address of the bitmask.
 * @param nr_tags Number of valid bits in the bitmask.
 * @return Index of the newly set bit, or -1 if the mask was full.
 */
#define TAG_NEXT(tag_mask) ({                                                \
    int i, ret = -1, mask_len, nr_tags;                                      \
    mask_len = tag_mask->_mask_len;                                          \
    nr_tags = tag_mask->_nr_tags;                                            \
    for (i = 0; i < mask_len; i++) {                                         \
        unsigned long curr_ulong;                                            \
        curr_ulong = (tag_mask->_mask)[i];                                   \
        int j;                                                               \
        for (j = 0; j < (sizeof(unsigned long) * 8); j++) {                  \
            if ((j + (i * sizeof(unsigned long) * 8)) >= nr_tags) break;     \
            if (!(curr_ulong & (0x1UL << j))) {                              \
                ret = j + (i * sizeof(unsigned long) * 8);                   \
                TAG_SET(tag_mask, ret);                                      \
                break;                                                       \
            }                                                                \
        }                                                                    \
        if (ret != -1) break;                                                \
    }                                                                        \
    ret; })
