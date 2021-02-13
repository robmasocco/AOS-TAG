# GENERAL ARCHITECTURE NOTES
## KERNEL-SIDE ARCHITECTURE
- Centralized architecture: there's only one AVL tree to lookup active instances, register new ones, or delete
existing ones from: indexed by keys, each entry contains the tag descriptor for that instance, if present.
Max 256 nodes should be allowed. Only for shared instances.
- The tag descriptor is an index in a kernel-level shared array of structs: pointers and two rw_semaphores.
- In the array structures, first things checked by every syscall are the semaphores, then IMMEDIATELY the validity
of the pointer. Maybe the next free index could be looked up with a bitmask, like fd_set.
- The device file driver only has to lock and scan the array.

## AVL TREE
- Represents key-instance associations.
- Is indexed by int keys.
- Each entry is an int "tag descriptor", index in the array.

## QUEUEING ARCHITECTURE
- Maybe we must embed in the general structure an array of structures that represent each level, with members
that do what follows.
- Levels must be RCU, one linked list or something per level, all pointed by a fixed-size list in the control
structure for that instance. Maybe not lists, but something lock/wait-free nonetheless.
- There must be a wait queue for each level.

# DATA STRUCTURES AND TYPES
## SHARED INSTANCES ARRAY
- Array of 256 structs with protected instance pointers, indexed by "tag descriptor".
- Bitmask of free/used descriptors (consider fd_set?).
- Two rw_semaphores or the like per each entry (that must then be a particular struct).

## INSTANCE DATA STRUCTURE
- Key.
- Array of 32 level data structures.
- Creator EUID.

## LEVEL DATA STRUCTURE
- Wait queue.
- RCU list of messages or something like that.
- Bitmasks and stuff to wait on to monitor conditions for wait events (use those APIs).

# MODULE PARAMETERS
- System call numbers (one pseudofile each) (read-only).
- Max number of active instances (configurable at insertion).

# SYNCHRONIZATION
## ACCESS TO AN INSTANCE, REMOVAL
There are 3 kinds of threads: receivers, senders, removers.
Keep in mind that senders and removers always return, never deadlock, so their critical section time is
always costant.
Each entry in the array holds two rw_semaphores and a pointer.
Remember that "lock" and "unlock" are called "down" and "up" for semaphores.
The first rw_sem is for receivers as readers, the second is for senders as readers, both are for removers as
writers.
When a remover comes, it trylocks the receivers's one as a writer, then eventually locks the senders's one as
a writer, then flips the instance pointer to NULL, releases both locks and does its thing.
When a receiver comes it trylocks its semaphore as reader, checks the pointer, eventually does its thing and
unlocks its semaphore as reader.
When a sender comes it trylocks its semaphore as reader, checks the pointer, eventually does its thing and
unlocks the semaphore as reader.
Consider percpu_rw_semaphores, maybe when things are already working, but the lack of a writer_down_trylock
makes them not so promising.

# TODOs
- Synchronization of everything, also thinking about the device file show method.
- System call module with exposed parameters (separate module?).
- Test "configurable at insertion"+"read-only" module parameters.
- The whole permissions+UID part. Probably EUID is involved here. May only be little checks at the beginning
of each call.
- Think about (1, N) registers for posting messages to readers.
- Check against false cache sharing everywhere. Remember that one of our cache lines is 64-bytes long.

# EXTRAS
- Module parameters consistency check at insertion, especially for max values and sizes of stuff.
