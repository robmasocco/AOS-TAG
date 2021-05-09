/**
 * @brief TAG listener CLI application.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date May 1, 2021
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/ipc.h>

#include "../aos-tag.h"

#define BUFLEN 80
#define BUFSIZE (BUFLEN + 1)

/* Signal handler: allows to gracefully terminate the process. */
void term_handler(int sig) {
    printf("\b\bGot signal: %d.\n", sig);
}

/* The works. */
int main(int argc, char **argv) {
    // Parse input arguments.
    if (argc != 3) {
        fprintf(stderr, "Usage: listener.out KEY LEVEL\n");
        exit(EXIT_FAILURE);
    }
    int key = atoi(argv[1]);
    int lvl = atoi(argv[2]);
    if (key == IPC_PRIVATE)
        fprintf(stderr, "ERROR: We're about to reopen a private instance...\n");
    // Open the instance of the service.
    int tag = tag_get(key, TAG_OPEN, TAG_USR);
    if (tag == -1) {
        fprintf(stderr, "ERROR: Failed to open tag service instance.\n");
        perror("tag_get");
        exit(EXIT_FAILURE);
    }
    printf("Opened instance with tag: %d.\n", tag);
    // Start receiving messages.
    char msg_buf[BUFSIZE];
    for (;;) {
        memset(msg_buf, 0, BUFSIZE);
        int receive_res = tag_receive(tag, lvl, msg_buf, BUFSIZE);
        if (receive_res == -1) {
            if (errno == EINTR) break;
            else if (errno == ECANCELED) {
                printf("Got hit by AWAKE_ALL!\n");
                break;
            } else {
                fprintf(stderr, "ERROR: Failed to receive message.\n");
                perror("tag_receive");
                exit(EXIT_FAILURE);
            }
        }
        printf("%s [%d]\n", msg_buf, receive_res);
    }
    // All done!
    printf("Listener terminated!\n");
    exit(EXIT_SUCCESS);
}
