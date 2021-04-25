/**
 * @brief Test code for system calls. Will be constantly changed.
 *
 * @author Roberto Masocco
 *
 * @date April 17, 2021
 */

#include <stdio.h>
#include <stdlib.h>

#include "../aos-tag/include/aos-tag.h"

int main(void) {
    int ret;
    ret = tag_get(1024, 1, 1);
    printf("tag_get: %d.\n", ret);
    perror("tag_get");
    ret = tag_get(1024, 0, 1);
    printf("tag_get: %d.\n", ret);
    perror("tag_get");
    ret = tag_receive(200, 12, NULL, 4096);
    printf("tag_receive: %d.\n", ret);
    perror("tag_receive");
    ret = tag_send(2345, 34, NULL, 5000);
    printf("tag_send: %d.\n", ret);
    perror("tag_send");
    ret = tag_ctl(34566, 0);
    printf("tag_ctl: %d.\n", ret);
    perror("tag_ctl");
    exit(EXIT_SUCCESS);
}
