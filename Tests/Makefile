# Roberto Masocco <robmasocco@gmail.com>
# May 2, 2021
# Makefile for the "AOS-TAG" kernel module testers.

.PHONY: all

CFLAGS := -Wall -Wextra

all:
	$(CC) $(CFLAGS) -o talker.out talker.c
	$(CC) $(CFLAGS) -o listener.out listener.c
	$(CC) $(CFLAGS) -pthread -o deadlock_test.out deadlock_test.c
	$(CC) $(CFLAGS) -o functional_test.out functional_test.c
	$(CC) $(CFLAGS) -pthread -o load_test.out load_test.c
	$(CC) $(CFLAGS) -o syscalls_test.out syscalls_test.c
