/**
 * @brief Test code for system calls. Will be constantly changed.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date April 17, 2021
 */

#include "tests_vscode_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "../aos-tag.h"

#define TEST_KEY 1024

void sighandler(int sig) {
    printf("Got signal: %d.\n", sig);
}

int main(void) {
    int ret, tag;
    signal(SIGINT, sighandler);
    ret = tag_get(TEST_KEY, TAG_CREATE, TAG_USR);
    printf("tag_get: %d.\n", ret);
    perror("tag_get");
    tag = ret;
    ret = tag_get(TEST_KEY, TAG_OPEN, 1);
    printf("tag_get: %d.\n", ret);
    perror("tag_get");
    printf("Now press CTRL-C to proceed!\n");
    ret = tag_receive(tag, 12, NULL, 0);
    printf("tag_receive: %d.\n", ret);
    perror("tag_receive");
    ret = tag_send(tag, 0, NULL, 0);
    printf("tag_send: %d.\n", ret);
    perror("tag_send");
    ret = tag_ctl(tag, REMOVE);
    printf("tag_ctl: %d.\n", ret);
    perror("tag_ctl");
    exit(EXIT_SUCCESS);
}
