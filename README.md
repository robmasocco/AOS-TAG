# AOS-TAG

TAG-based data exchange service between user-level threads in the Linux kernel.

For a general description of the architecture, some pseudocode and a brainstorm-like representation of the ideas behind this see [here](ideas.md).

## Dependencies

This module depends on some other works, entirely included in this repository and developed by myself:

- A kernel module to "hack" the system call table, i.e. to locate it in main memory and replace entries in it. It is called **_System Call Table Hacker - SCTH_**, was developed as one of the course homeworks and is included in the *scth* subfolder. It is compiled as a secondary module when building *aos-tag*, and must be inserted before it since this service depends on it to be correctly initialized. Proper module locking on it is also performed. Relies on the *kmalloc* SLAB allocator to create some internal management data structures.
- An implementation of the *Splay Tree* dictionary data structure, a particular kind of binary search tree. Its code is a kernel-side rework of the contents of my other repository [splay-trees_c](https://github.com/robmasocco/splay-trees_c), added only as a library and not as a secondary module. Compared to the complete user-mode library, this one lacks all unnecessary search routines and relies on the *kmalloc* SLAB allocator to dynamically get and release memory for the nodes.