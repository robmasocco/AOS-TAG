/* Small test program for the bitmasks.
 * Roberto Masocco
 * 2/4/2021
 */

#include <stdio.h>
#include <stdlib.h>

#include "../aos-tag/utils/aos-tag_bitmask.h"

#define MAX_INST 300  // Simulates default max number of instances.

/* Simulates related kernel module parameter. */
unsigned int max_inst = MAX_INST;

/* Bitmask of used tag descriptors. */
tag_bitmask *tag_mask;

/* The works. */
int main(void) {
    /* Create the bitmask. */
    tag_mask = TAG_MASK_CREATE(max_inst);
    printf("Have to host %d instances/bits, need %d ulongs.\n",
           max_inst, tag_mask->_mask_len);
    /* Try to set them all. */
    int tag, full = 0;
    for (tag = 0; tag <= max_inst; tag++) {
        int next = TAG_NEXT(tag_mask, full);
        if (full != 1) printf("Set tag: %d (full: %d).\n", next, full);
        else printf("Mask full.\n");
    }
    /* Clear then re-set one of them. */
    int ans;
    printf("Select an entry to clear (it HAS to be valid): ");
    scanf("%d", &ans);
    TAG_CLR(tag_mask, ans);
    printf("Had to reset: %d (full: %d).\n", TAG_NEXT(tag_mask, full), full);
    /* Reset them all. */
    for (tag = 0; tag < max_inst; tag++) TAG_CLR(tag_mask, tag);
    printf("Cleared. Next one is now: %d (full: %d).\n",
        TAG_NEXT(tag_mask, full), full);
    TAG_MASK_FREE(tag_mask);
    return 0;
}
