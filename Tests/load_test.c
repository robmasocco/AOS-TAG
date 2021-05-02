/**
 * @brief Load performance tester for AOS-TAG.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date May 2, 2021
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
#include <sys/sysinfo.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <time.h>

#include "../aos-tag/include/aos-tag.h"

#define UNUSED(arg) (void)(arg)

#define KEY IPC_PRIVATE
#define LEVEL 12

cpu_set_t major_mask, single_mask;

sem_t multi_writers_sem;

int tag;

FILE *out_file;
char *out_file_name = "load_tests.txt";

int cpus;

/**
 * @brief Writer routine: sends an empty message.
 *
 * @param arg Thread argument (unused).
 * @return Thread exit status.
 */
void *writer(void *arg) {
    UNUSED(arg);
    if (tag_send(tag, LEVEL, NULL, 0)) {
        fprintf(stderr, "ERROR: Failed to send empty message.\n");
        perror("tag_send");
        exit(EXIT_FAILURE);
    }
    pthread_exit(NULL);
}

/**
 * @brief Reader routine: reads an empty message.
 *
 * @param arg Thread argument (unused).
 * @return Thread exit status.
 */
void *reader(void *arg) {
    UNUSED(arg);
    if (tag_receive(tag, LEVEL, NULL, 0)) {
        fprintf(stderr, "ERROR: Failed to receive empty message.\n");
        perror("tag_receive");
        exit(EXIT_FAILURE);
    }
    pthread_exit(NULL);
}

/**
 * @brief Multiple writers routine: waits for a signal, then sends an empty 
 * message.
 *
 * @param arg Thread argument (unused).
 * @return Thread exit status.
 */
void *multi_writer(void *arg) {
    UNUSED(arg);
    sem_wait(&multi_writers_sem);
    if (tag_send(tag, LEVEL, NULL, 0)) {
        fprintf(stderr, "ERROR: Failed to send empty message.\n");
        perror("tag_send");
        exit(EXIT_FAILURE);
    }
    pthread_exit(NULL);
}

/* The works. */
int main(int argc, char **argv) {
    // Parse input arguments.
    int NR_READERS = 1000;
    int NR_WRITERS = 1000;
    if (argc != 4) {
        fprintf(stderr, "Usage: load_test.out NR_READERS NR_WRITERS"
                        " FILENAME\n");
        fprintf(stderr, "Proceeding with default values of %d readers and %d"
                        " writers.\n", NR_READERS, NR_WRITERS);
        fprintf(stderr, "Output file will be named: %s.\n", out_file_name);
    } else {
        NR_READERS = atoi(argv[1]);
        NR_WRITERS = atoi(argv[2]);
    }
    // Allocate test metadata and resources.
    pthread_t readers_tids[NR_READERS];
    pthread_t writers_tids[NR_WRITERS];
    pthread_t single_writer;
    pthread_attr_t readers_attr[NR_READERS];
    pthread_attr_t writers_attr[NR_WRITERS];
    pthread_attr_t single_wr_attr;
    cpus = get_nprocs();
    clock_t tic, toc;
    double test_times[2];
    if (argc == 4) out_file = fopen(argv[3], "w+");
    else out_file = fopen(out_file_name, "w+");
    if (out_file == NULL) {
        fprintf(stderr, "ERROR: Failed to open output file.\n");
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fprintf(out_file, "### AOS-TAG SEND/RECEIVE LOAD TESTS RESULTS ###\n");
    tag = tag_get(KEY, TAG_CREATE, TAG_USR);
    if (tag == -1) {
        fprintf(stderr, "ERROR: Failed to create new tag service instance.\n");
        perror("tag_get");
        exit(EXIT_FAILURE);
    }
    printf("Opened new tag serivce instance with descriptor: %d.\n", tag);
    // TEST 1: Multiple readers, single writer.
    printf("Starting multiple readers, single writer test...\n");
    for (int i = 0; i < NR_READERS; i++) {
        pthread_attr_init(readers_attr + i);
        CPU_ZERO(&major_mask);
        for (int j = 1; j < cpus; j++) CPU_SET(j, &major_mask);
        pthread_attr_setaffinity_np(readers_attr + i, sizeof(cpu_set_t),
                                    &major_mask);
        pthread_create(readers_tids + i, readers_attr + i, reader, NULL);
    }
    sleep(1);  // Give readers some time to start waiting...
    pthread_attr_init(&single_wr_attr);
    CPU_ZERO(&single_mask);
    CPU_SET(0, &single_mask);
    pthread_attr_setaffinity_np(&single_wr_attr, sizeof(cpu_set_t),
                                &single_mask);
    pthread_create(&single_writer, &single_wr_attr, writer, NULL);
    tic = clock();
    for (int i = 0; i < NR_READERS; i++) pthread_join(readers_tids[i], NULL);
    toc = clock();
    pthread_join(single_writer, NULL);
    test_times[0] = (double)(toc - tic) / CLOCKS_PER_SEC;
    printf("LOAD TEST 1 ELAPSED TIME: %g second(s).\n", test_times[0]);
    fprintf(out_file, "[LOAD TEST 1 COMPLETED]\n");
    fprintf(out_file,
            "Multiple readers, single writer test completed without errors.\n");
    fprintf(out_file, "Number of readers: %u.\nNumber of writers: 1.\n",
            NR_READERS);
    fprintf(out_file, "Elapsed time: %g second(s).\n", test_times[0]);
    fputc('\n', out_file);
    // TEST 2: Multiple writers.
    printf("Starting multiple writers test...\n");
    sem_init(&multi_writers_sem, 0, 0);
    for (int i = 0; i < NR_WRITERS; i++) {
        pthread_attr_init(writers_attr + i);
        CPU_ZERO(&major_mask);
        for (int j = 1; j < cpus; j++)
            // Must leave one core out for unlocks.
            CPU_SET(j, &major_mask);
        pthread_attr_setaffinity_np(writers_attr + i, sizeof(cpu_set_t),
                                    &major_mask);
        pthread_create(writers_tids + i, writers_attr + i, multi_writer, NULL);
    }
    sleep(1);  // Give the writers some time to start...
    tic = clock();
    for (int i = 0; i < NR_WRITERS; i++) sem_post(&multi_writers_sem);
    for (int i = 0; i < NR_WRITERS; i++) pthread_join(writers_tids[i], NULL);
    toc = clock();
    test_times[1] = (double)(toc - tic) / CLOCKS_PER_SEC;
    printf("LOAD TEST 2 ELAPSED TIME: %g second(s).\n", test_times[1]);
    fprintf(out_file, "[LOAD TEST 2 COMPLETED]\n");
    fprintf(out_file, "Multiple writers test completed without errors.\n");
    fprintf(out_file, "Number of writers: %u.\n", NR_WRITERS);
    fprintf(out_file, "Elapsed time: %g second(s).\n", test_times[1]);
    fputc('\n', out_file);
    // All done!
    fclose(out_file);
    if (tag_ctl(tag, REMOVE)) {
        fprintf(stderr, "ERROR: Failed to remove service instance.\n");
        perror("tag_ctl");
        exit(EXIT_FAILURE);
    }
    printf("Load tests done!\n");
    exit(EXIT_SUCCESS);
}
