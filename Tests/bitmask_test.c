/* Small test program for the bitmasks.
 * Roberto Masocco
 * 2/4/2021
 */

#include <stdio.h>
#include <stdlib.h>

#include "aos-tag_bitmask.h"

#define MAX_INST 300  /* Simulates default max number of instances. */

int max_inst = MAX_INST;  /* Simulates related kernel module parameter. */
int __mask_len;  /* Number of ulongs in the mask, needs to be computed. */

/* Bitmask of used tag descriptors. */
unsigned long *tag_mask;

/* The works. */
int main(void) {
    /* Compute the size of the bitmask. */
    __mask_len = max_inst / (sizeof(unsigned long) * 8);
    if (max_inst % (sizeof(unsigned long) * 8)) __mask_len++;
    tag_mask = (unsigned long *)calloc(__mask_len, sizeof(unsigned long));
    printf("Have to host %d instances/bits, need %d ulongs.\n", max_inst, __mask_len);
    /* Try to set them all. */
    int tag;
    for (tag = 0; tag <= max_inst; tag++) {
        int next = TAG_NEXT(tag_mask, max_inst);
        if (next != -1) printf("Set tag: %d.\n", next);
        else printf("Mask full.\n");
    }
    /* Clear then re-set one of them. */
    int ans;
    printf("Select an entry to clear (it HAS to be valid): ");
    scanf("%d", &ans);
    TAG_CLR(tag_mask, ans);
    printf("Had to reset: %d.\n", TAG_NEXT(tag_mask, max_inst));
    /* Reset them all. */
    for (tag = 0; tag < max_inst; tag++) TAG_CLR(tag_mask, tag);
    printf("Cleared. Next one is now: %d.\n", TAG_NEXT(tag_mask, max_inst));
    free(tag_mask);
    return 0;
}
