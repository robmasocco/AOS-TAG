/**
 * @brief Tester to check whether the module can run on a single-core processor.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date May 1, 2021
 */

#include "tests_vscode_settings.h"

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ipc.h>
#include <pthread.h>
#include <sched.h>

#include "../aos-tag/include/aos-tag.h"

#define UNUSED(arg) (void)(arg)

#define NR_READERS 50
#define NR_MSGS 3
#define CHOSEN_CORE 0

pthread_t reader_tids[NR_READERS];
pthread_t writer_tid;

pthread_attr_t reader_attrs[NR_READERS];
pthread_attr_t writer_attr;

cpu_set_t aff_mask;

int key = IPC_PRIVATE;
int tag;
int lvl = 9;

/**
 * @brief Writer routine: sends some empty messages.
 *
 * @param arg Thread arguments (unused).
 * @return Thread exit status.
 */
void *writer(void *arg) {
    UNUSED(arg);
    for (int i = 0; i < NR_MSGS; i++) {
        if (tag_send(tag, lvl, NULL, 0)) {
            fprintf(stderr, "ERROR: Failed to send message.\n");
            perror("tag_send");
            exit(EXIT_FAILURE);
        }
        printf("Sent message no. %d.\n", i + 1);
        sleep(5);  // We gotta wait for many threads on a single core...
    }
    printf("Writer terminated!\n");
    pthread_exit(NULL);
}

/**
 * @brief Reader routine: reads an empty message.
 *
 * @param arg Thread arguments (unused).
 * @return Thread exit status.
 */
void *reader(void *arg) {
    UNUSED(arg);
    for (int i = 0; i < NR_MSGS; i++) {
        if (tag_receive(tag, lvl, NULL, 0)) {
            fprintf(stderr, "ERROR: Failed to receive message.\n");
            perror("tag_receive");
            exit(EXIT_FAILURE);
        }
    }
    pthread_exit(NULL);
}

/* The works. */
int main(int argc, char **argv) {
    UNUSED(argc);
    UNUSED(argv);
    // Create the service instance.
    tag = tag_get(IPC_PRIVATE, TAG_CREATE, TAG_USR);
    if (tag == -1) {
        fprintf(stderr, "ERROR: Failed to create a new tag instance.\n");
        perror("tag_get");
        exit(EXIT_FAILURE);
    }
    printf("Opened tag: %d.\n", tag);
    // Set thread attributes and spawn readers.
    for (int i = 0; i < NR_READERS; i++) {
        pthread_attr_init(reader_attrs + i);
        CPU_ZERO(&aff_mask);
        CPU_SET(CHOSEN_CORE, &aff_mask);
        pthread_attr_setaffinity_np(reader_attrs + i, sizeof(cpu_set_t),
                                    &aff_mask);
        pthread_create(reader_tids + i, reader_attrs + i, reader, NULL);
    }
    printf("Created %d readers.\n", NR_READERS);
    // Set thread attributes and spawn writer.
    pthread_attr_init(&writer_attr);
    CPU_ZERO(&aff_mask);
    CPU_SET(CHOSEN_CORE, &aff_mask);
    pthread_attr_setaffinity_np(&writer_attr, sizeof(cpu_set_t), &aff_mask);
    pthread_create(&writer_tid, &writer_attr, writer, NULL);
    printf("Spawned writer.\n");
    // Wait for threads to finish.
    for (int i = 0; i < NR_READERS; i++) pthread_join(reader_tids[i], NULL);
    printf("Readers joined.\n");
    pthread_join(writer_tid, NULL);
    printf("Writer joined.\n");
    if (tag_ctl(tag, REMOVE)) {
        fprintf(stderr, "ERROR: Failed to remove tag instance.\n");
        perror("tag_ctl");
        exit(EXIT_FAILURE);
    }
    printf("Deadlock tester terminated!\n");
    exit(EXIT_SUCCESS);
}
