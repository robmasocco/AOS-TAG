# GENERAL ARCHITECTURE NOTES

## KERNEL-SIDE ARCHITECTURE

- Centralized architecture: there's only one binary search tree to lookup active instances, register new ones, or delete existing ones from. Indexed by keys, each entry contains the tag descriptor for that instance, if present. Max 256 nodes should be allowed. Only for shared instances.
- The tag descriptor is an index in a kernel-level shared array of structs: pointers and two rw_semaphores.
- In the array structures, first things checked by every syscall are the semaphores, then IMMEDIATELY the validity of the pointer. Maybe the next free index could be looked up with a bitmask, like fd_set.
- The device file driver only has to lock and scan the array.
- Permissions are implemented as simple checks of the current EUID against the creator's EUID when a thread does a tag_get. Such EUID is stored upon creation of the instance and checked each time it is reopened.

## BINARY SEARCH TREE

- Represents key-instance associations.
- Is indexed by int keys.
- Each entry is an int "tag descriptor", index in the array.
- Must be optimized for speed, "cache-like", possibly RCU.
- Must be protected from concurrent access, using an rw_semaphore at least. An RCU implementation should especially synchronize writers using a mutex (wait time is not predictable, so we can't use a spinlock).

## MESSAGE PUBLISHING ARCHITECTURE
- Maybe we must embed in the general structure an array of structures that represent each level, with members that do what follows.
- There must be a wait queue for each level.

# OPERATIONS DETAILS

## *int tag_get(int key, int command, int permission)*

TODO

## *int tag_receive(int tag, int level, char\* buffer, size_t size)*

This configures the running thread as a *reader thread*.

Returns 0 if the message was read, or -1 and *errno* is set to indicate the cause of the error.

- *try_module_get*
- Trylock receivers's rw_sem as reader.
- Check instance pointer, eventually exit.
- Check permissions, eventually exit.
- Atomically read current condition value.
- Wait on the queue (with *wait_event_interruptible(...)*) with condition according to current condition value (*if 0 wait on 1 else wait on 0*, an *if-else* should suffice).
- Acquire level rwlock as reader.
- *Memcpy* the new message from the level buffer into an on-the-go-set array in the stack.
- Release level rwlock as reader.
- *Copy_to_user* the new message.
- Release receivers's rw_sem as a reader.
- *module_put*

TSO bypasses are avoided by executing memory barriers embedded in spinlocks and wait queue APIs.

Even if new readers register themselves on the queue, those just awoken should prevent writers from running until they get the newest message.

Ensure proper locks are released in each *if-else* to avoid deadlocks, and that the module is always released.

## *int tag_send(int tag, int level, char\* buffer, size_t size)*

This configures the running thread as a *writer thread*.

Returns 0 if the message was correctly sent, or -1 and *errno* is set to indicate the cause of the error.

- *try_module_get*
- Trylock senders's rw_sem as reader.
- Check instance pointer, eventually exit.
- Check permissions, eventually exit.
- *Copy_from_user* into an on-the-go-set array in the stack.
- Acquire level rwlock as writer.
- Acquire wait queue spinlock.
- Check for active readers (use *waitqueue_active(...)*), exit if there's none.
- Release wait queue spinlock.
- *Memcpy* the message in the level buffer.
- Set message size.
- **STORE FENCE**
- Atomically flip level condition value.
- Release level rwlock as writer.
- Wake up the entire wait queue (use *wake_up(...)* on the level wait queue).
- Release senders's rw_sem as a reader.
- *module_put*

TSO bypasses are avoided by executing memory barriers and those embedded in spinlocks and wait queue APIs.

Only one writer should be active at any given time.

Ensure proper locks are released in each *if-else* to avoid deadlocks, and that the module is always released.

## *int tag_ctl(int tag, int command)*

TODO

# DATA STRUCTURES AND TYPES

## BST-DICTIONARY

This dictionary holds *key-tag descriptor* pairs of the instances that were **NOT** created as *IPC_PRIVATE*, thus meant to be shared. Goes with an rw_semaphore to synchronize accesses (see below). Each node holds:

- int key, the dictionary key.
- int tag descriptor, index in the shared instances array.
- A shitload of pointers to make the tree work as a linked structure.

## SHARED INSTANCES ARRAY

Array of 256 structs with protected instance pointers, indexed by "tag descriptor". Goes with a bitmask of free/used tag descriptors (consider an *fd_set*?) (?).

Each entry holds:

- Pointer to the corresponding instance data structure, meant to be NULL when the instance hasn't been created.
- Receivers rw_semaphore.
- Senders rw_semaphore.

## INSTANCE DATA STRUCTURE
- Key.
- Array of 32 level data structures.
- Creator EUID.

## LEVEL DATA STRUCTURE
- Wait queue head (which embeds a spinlock).
- Pointer to a preallocated 1 page-buffer (using kmalloc).
- size_t size of the message currently stored.
- rwlock_t to access the buffer and the message size.
- Single char/int used as atomic condition value for the wait queue.

# MODULE PARAMETERS

Consider adding anything you might need to debug this module.

- System call numbers (one pseudofile each) (read-only).
- Max number of active instances (configurable at insertion).
- Max message size (4 KB) (read-only).
- Currently active instances (read-only).
- Currently waiting threads on any level (read-only).

# CHAR DEVICE DRIVER(s)

**Remember to set the *owner* member!**

## OPEN

Nop.

## CLOSE

Nop.

## READ

TODO

## Any other op that might be required to make this work

Nop (for now).

# DEVICE FILE(s)

A char device driver is required for these, maybe more than one or some minor-based behaviour.
## STATUS
As requested, line-by-line status report. Located in /dev.

## I/O (?)

A generic userland thread becomes a writer/reader through these device files.
What about IPC_PRIVATE? Which routines would need to be called?
Develop the baseline version first, then make sure it is doable and discuss it with Quaglia to avoid conflicts with the specification. Could be a nice addition.

# SYNCHRONIZATION

**At first, each operation should be protected with a *try_module_get/module_put* pair, the very first and last instructions of each system call, to ensure that the data structures we're about to access don't magically fade away whilst we're operating on them.**

## ACCESS TO THE BST-DICTIONARY

There's just an rw_semaphore to acquire and release: as a reader when making a query, as a writer when cutting an instance out.

## ACCESS TO AN INSTANCE, REMOVAL

There are 3 kinds of threads: *receivers*, *senders*, *removers*.

Keep in mind that senders and removers always return, never deadlock, so their critical section time is always constant. Each entry in the array holds two rw_semaphores and a pointer. Remember that *lock* and *unlock* are called *down* and *up* for semaphores. The first rw_sem is for receivers as readers, the second is for senders as readers, both are for removers as writers.

When a remover comes, it trylocks the receivers's one as a writer, then **eventually** locks the senders's one as a writer, then flips the instance pointer to NULL, releases both locks and does its thing.

When a receiver comes it trylocks its semaphore as reader, checks the pointer, eventually does its thing and unlocks its semaphore as reader.

When a sender comes it trylocks its semaphore as reader, checks the pointer, eventually does its thing and unlocks the semaphore as reader.

Don't use the *_interruptible* variants of rw_semaphores's APIs, so signals won't kick our butt.

## POSTING A MESSAGE ON A LEVEL

Each level structure embeds an *rwlock_t*: the writer takes it to post, updates the wakeup condition, releases it and wakes readers up. Readers acquire it to *memcpy* contents into an on-the-go-set array in the stack and release it afterwards (can't hold a spinlock while doing a sleeping call!); then, they call *copy_to_user*. Buffers are preallocated for each level (a single page, compromise between complexity and resource usage).

The wakeup condition is a single value which is flipped each time a writer posts a message, and readers will always wait on the opposite value; thus, this works as a linearization point for the level data structure (evident if you see the pseudocode above).

# TODO LIST

- How are fd_sets implemented and used?
- Complete definition of all operations.
- Test multiple-locks scenarios.
- Synchronization of everything, also thinking about the device file read operation.
- Signals, interrupts, preemption and the like checks against deadlocks and similar problems. Remember that wait queues functions return *-ERESTARTSYS* when a signal was delivered. Consider using local_locks to protect your (really) critical sections. See our little golden screenshot from our course materials to know how signals work (and remember: they're usermode shit, you just return -EINTR).
- Check TSO compliance everywhere, add memory fences where needed.
- Check against false cache sharing everywhere. Remember that one of our cache lines is 64-bytes long.
- Anything still marked as TODO here.

# EXTRAS
- Module parameters consistency check at insertion, especially for max values and sizes of stuff.
- Splay trees as BSTs, using join-based alternative for deletion (to avoid splaying the predecessor to the top)
and make nodes (structures) cache-aligned (in GCC: "struct ... {...} ... __attribute__ ((aligned (L1_CACHE_BYTES)));").
- MODULE_INFO stuff!
- A more complete device driver?
