# AOS-TAG

Tag-based data exchange service between user threads, implemented as system calls added to the Linux kernel via kernel modules.
Final project for the *Advanced Operating Systems* course at University of Rome Tor Vergata.
**For x86 systems and kernels later than 4.17 only.**

## What's this?

AOS-TAG is an interprocess communication system for user threads, implemented as a set of system calls added to the Linux kernel at runtime using the Loadable Kernel Modules.
At the very base it is a **publish-subscribe** message system between threads of a same process or started by the same user, handling full packets of data without buffering. New instances of the service can be created or reopened, and each instance has 32 communication channels, named *levels* numbered 0 to 31, on which threads can post or listen to incoming data.
The system calls provided allow a thread to:

- Create a new instance of the service, or open an existing one, using a common *key*s and obtaining a *tag descriptor* used to operate on the instance from that moment on.
- Post data on a level of an instance. Note that being this a publish-subscribe system with no logging, messages are discarded after being read by subscribed threads (if any). Zero-length messages are anyhow allowed.
- Wait for an incoming packet of data on an instance. Since this service performs only **packetized I/O**, the delivery is successfully performed only if the provided buffer is large enough to store the incoming data, and the copy operation succeeds.
- Wake up all threads waiting on levels of an instance, interrupting their sleep like a signal would do.
- Remove an instance from the system.

On each of the aforementioned operations, security checks are enforced to ensure that the calling thread is allowed to perform such a call, and is providing coherent input arguments to the kernel code. Root user (EUID 0) has, by design, full control.

For a thorough description of the underlying architecture and some notes about testing see [ideas.md](ideas.md).

## Dependencies

This module depends on two other works, both included in this repository and developed by myself:

- A kernel module to "hack" the system call table, i.e. to locate it in main memory and replace entries in it. It is called **_System Call Table Hacker - SCTH_**, was developed as one of the course homeworks and improved after the public solution discussion. It is included in the *aos-tag/scth/* folder, gets compiled as a secondary module when building *aos-tag*, and must be inserted before it since this service depends on it to be correctly initialized. Proper module locking on it is also performed.
    It has been left as a secondary module and not completely integrated in the final project to experiment with module locking, Kbuild compilation and linking dependencies, and exported symbols. It exposes four functions to the kernel software that allow us to locate the system call table, alter and restore its entries, thus providing a simple interface to install new system calls, which is exactly what AOS-TAG has to do in its initialization routine. Some module parameters can also be specified during insertion to manually provide some hints about the table position in main memory, like the number of effective entries and the indexes of those mapped to *sys_ni_syscall*, which the module will replace on demand. It's also been a nice playground to experiment with many low-level architectural concepts and topics covered during the course.
    **NOTE:** During installation of a system call, this module needs to alter the contents of write-protected memory areas. To do so, Write Protection is temporarily disabled by toggling the corresponding bit in CR0. In order not to leave the system exposed during that time, not to mention the risk of being preempted and resumed on another CPU leaving the original one with WP disabled, **interrupts are disabled on the local CPU until WP is reenabled**, which really happens only after the write operation became globally visible thanks to memory fences. This is the only point in this project in which we tinker with interrupts.
- An implementation of the *Splay Tree* dictionary data structure, a particular kind of binary search tree. Its code is a kernel-side rework of the contents of my other repository [splay-trees_c](https://github.com/robmasocco/splay-trees_c), added only as a library and not as a secondary module. Compared to the complete user-mode library, this one lacks all unnecessary search routines and relies on the *kmalloc* SLAB allocator to dynamically get and release memory for the nodes. Also, in order to allow concurrent accesses by readers, the *splaying* operation is not performed when searching for a key, only when inserting or deleting entries. This way, searches have linear worst-case access times, but since in the average case we can expect many more searches than insertions or deletions, nodes will be placed in the tree in a way that reflects how old the related instances are, thus making searches quicker the more recent the target instance is.

## Installation

See [INSTALL.md](INSTALL.md).

## Usage

After the module has been correctly inserted, the script will have generated an header file named *aos-tag.h* in the main directory. The userspace part of this header contains all macros required in the specification plus definitions for the system calls stubs as *static inline* functions. To use the services provided by this module, just include this header in your code.
**NOTE:** The header is generated during module insertion because the system call numbers used by the stubs depend on where the calls will be installed on your system. Take care to regenerate or modify it should they change. For convenience, the macros defining such numbers are protected with *#ifndef* preprocessor directives, so they can be redefined from the command line if need be.

Each stub is properly documented in the header. For completeness, such documentation is also reported here:

- **_int tag_get(int key, int command, int permission)_:** Opens a new instance of the service, or reopens an existing one. Instances can be shared or not, depending on the value of *key*. An instance can be created or reopened, depending on the value of *cmd*. With *perm*, it is possible to specify whether permission checks should be performed to limit access to threads executing on behalf of the same user that created the instance. Use the _TAG\_*_ flags for command and permission. Returns a valid tag descriptor, or -1 and *errno* will be set to indicate an error among:

    - EINVAL: Invalid input arguments.
    - EINTR: Interrupted by signal.
    - ENOKEY: Asked to reopen an instance which doesn't exist.
    - EALREADY: Asked to create an instance with a key that corresponds to another existing instance.
    - ENOMEM: No memory available or maximum limit of active instances reached.

- **_int tag_receive(int tag, int level, char *buffer, size_t size)_:** Allows a thread to receive a message from a level of an instance. The instance should have been previously opened with tag_get, however presence and permissions checks are always performed. The provided buffer must be large enough to store the new message. Returns the number of bytes read if the operations was successfully completed, or -1 and *errno* will be set to indicate an error among:

    - EINVAL: Invalid input arguments.
    - EINTR: Interrupted by signal.
    - EIDRM: Requested tag instance is not present.
    - EACCES: User not allowed to receive messages from this instance.
    - ECANCELED: Interrupted by an *AWAKE ALL*.
    - ENOBUFS: Provided buffer is too small to hold the latest message.
    - EFAULT: Failed to copy the message from kernel to user memory; the buffer contents are undefined.

- **_int tag_send(int tag, int level, char *buffer, size_t size)_:** Allows a thread to send a message on a level of an instance. The instance should have been previously opened with *tag_get*, however presence and permissions checks are always performed. I/O is packetized: the entire size of the buffer provided will be copied for distribution to readers. The operation will fail if this is not possible. Note again that zero-length messages are allowed, and their effect will simply be to wake up readers. Returns 0 if the message was successfully delivered, 1 if it was discarded because no reader was there to get it, or -1 and *errno* will be set to indicate an error among:

    - EINVAL: Invalid input arguments.
    - EINTR: Interrupted by signal.
    - EIDRM: Requested tag instance is not present.
    - EACCES: User not allowed to receive messages from this instance.
    - ENOMEM: Not enough memory to deliver the provided message.
    - EFAULT: Failed to copy the message from user to kernel memory.

- **_int tag_ctl(int tag, int command)_:** Once the tag descriptor has been retrieved via *tag_get*, allows to control an instance. Supported commands are:

     * *REMOVE*: Deletes the instance, freeing the related tag descriptor. 
     * *AWAKE_ALL*: Awakes all threads waiting on all levels (if any).

    Use the *TAG_\** flags for *command*. Returns 0 if the operation was successfully completed, or -1 and *errno* will be set to indicate an error among:

    - EINVAL: Invalid input arguments.
    - EINTR: Interrupted by signal.
    - EIDRM: Requested tag instance is not present.
    - EACCES: User not allowed to receive messages from this instance.

## Checking system status

The module includes some basic means to check the system's status: some read-only module parameters and a device driver.
In detail, you have:

- Some pseudofiles in */sys/module/aos_tag/parameters/*:
    - **max_msg_sz:** Max message size in bytes. This can be configured while inserting the module, but cannot drop below the default of 4096 bytes.
    - **max_tags:** Max number of instances that the system supports. This too can be configured during insertion and has a minimum default value of 256.
    - **tag_get_nr:** *tag_get* index in the system call table.
    - **tag_receive_nr:** *tag_receive* index in the system call table.
    - **tag_send_nr:** *tag_send* index in the system call table.
    - **tag_ctl_nr:** *tag_ctl* index in the system call table.
    - **tag_drv_major:** Status device driver major number.
- A device file: */dev/aos_tag_status*, managed by a character device driver included in the module and initialized during insertion. This driver allows every user to check the current state of the service. The file can be opened for reading, and each line describes a level of an active instance, with the following format:
    **TAG    KEY    CREATOR EUID    LEVEL    WAITING THREADS**
    Only active, i.e. opened by at least one thread, instances are described in this file.
    Suggested (and tested) programs to access this file are *cat* and *less -f*.

## License

Copyright © 2021 Roberto Masocco

This is free software, licensed under the GNU GPL v3.0 license included in the LICENSE file, or any later version.
The same is stated in every source file except testers in the *Tests/* folder.