/**
 * @brief Playground to test AOS-TAG service functionalities and hunt bugs.
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
#include <sys/ipc.h>

#include "../aos-tag/include/aos-tag.h"

#define fflush(stdin) while (getchar() != '\n')
#define UNUSED(arg) (void)(arg)

#define TOADD 1001

/* The works. */
int main(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    printf("Starting instances creation test...\n");
    // Try to create all possible instances, plus one.
    for (int key = 0; key < TOADD; key++) {
        if (tag_get(key, TAG_CREATE, TAG_ALL) == -1) {
            fprintf(stderr, "ERROR: Failed to create instance no. %d.\n", key);
            perror("tag_get");
        }
    }
    printf("Created all the instances that I could.\n");
    printf("Maybe take some time to have a look at the status device file.\n");
    printf("Press ENTER when done...");
    getchar();
    // Try to delete all possible instances.
    for (int tag = 0; tag < TOADD; tag++) {
        if (tag_ctl(tag, REMOVE)) {
            fprintf(stderr, "ERROR: Failed to remove tag no. %d.\n", tag);
            perror("tag_ctl");
        }
    }
    printf("Deleted all the instances that I could.\n");
    printf("Maybe check again the status device file.\n");
    printf("Press ENTER when done...\n");
    getchar();
    printf("Functional tester done!\n");
    exit(EXIT_SUCCESS);
}
