/**
 * @brief TAG talker CLI application.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date May 1, 2021
 */

#include "tests_vscode_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include "../aos-tag.h"

/* Signal handler: allows to gracefully terminate the process. */
void term_handler(int sig) {
    printf("\b\bGot signal: %d.\n", sig);
}

/* The works. */
int main(int argc, char **argv) {
    // Parse input arguments.
    if (argc != 5) {
        fprintf(stderr, "Usage: talker.out KEY LEVEL MESSAGE PERIOD\n");
        fprintf(stderr, "\tPERIOD: Sleep time in milliseconds.\n");
        exit(EXIT_FAILURE);
    }
    int key = atoi(argv[1]);
    int lvl = atoi(argv[2]);
    int period = atoi(argv[4]);
    size_t bufsize = strlen(argv[3]) + 10 + 3 + 1;
    char msg_buf[bufsize];
    unsigned int msg_cnt = 0;
    signal(SIGINT, term_handler);
    // Open a new instance of the service.
    int tag = tag_get(key, TAG_CREATE, TAG_USR);
    if (tag == -1) {
        fprintf(stderr, "ERROR: Failed to create new tag service instance.\n");
        perror("tag_get");
        tag = tag_get(key, TAG_OPEN, TAG_USR);
        if (tag == -1) {
            fprintf(stderr, "ERROR: Failed to reopen instance.\n");
            perror("tag_get");
            exit(EXIT_FAILURE);
        }
    }
    printf("Opened new instance with tag: %d.\n", tag);
    // Start posting messages.
    for (;;) {
        memset(msg_buf, 0, bufsize);
        sprintf(msg_buf, "[%u] %s", msg_cnt, argv[3]);
        int send_res = tag_send(tag, lvl, msg_buf, strlen(msg_buf));
        if (send_res == -1) {
            if (errno != EINTR) {
                fprintf(stderr, "ERROR: Failed to send message no. %u.\n",
                        msg_cnt);
                perror("tag_send");
                exit(EXIT_FAILURE);
            } else break;
        } else if (send_res == 1)
            printf("Discarded message no. %u.\n", msg_cnt);
        else printf("Delivered message no. %u.\n", msg_cnt);
        msg_cnt++;
        if (usleep(period * 1000)) break;
    }
    usleep(100 * 1000);
    // Awake all readers so that we can remove the instance.
    if (tag_ctl(tag, AWAKE_ALL)) {
        fprintf(stderr, "ERROR: AWAKE_ALL failed.\n");
        perror("tag_ctl");
        exit(EXIT_FAILURE);
    }
    // Try to remove the instance.
    // If listeners are still active, this should fail.
    if (tag_ctl(tag, REMOVE)) {
        fprintf(stderr, "ERROR: Failed to remove tag service instance.\n");
        perror("tag_ctl");
        exit(EXIT_FAILURE);
    }
    printf("Talker done!\n");
    exit(EXIT_SUCCESS);
}
