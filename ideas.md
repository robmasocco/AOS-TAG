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

This is a *reader* thread.

TODO return what?

- TODO Instance access operations. ATOMIC FLAGS SET IN INTERRUPT-SAFE PLACES
- Wait on the queue (with wait_event_interruptible(...) on condition byte).
- Acquire level rwlock as reader (saving IRQ state).
- Do a memcpy of the new message from the level buffer into an on-the-go-set array in the stack.
- Acquire wait queue spinlock (without touching IRQ state).
- If the wait queue is empty set the condition to zero.
- Release wait queue spinlock (as above).
- Release level rwlock as reader (restoring IRQ state).
- Do a copy_to_user of the new message.

TSO bypasses are avoided by executing memory barriers embedded in spinlocks and wait queue APIs.

Ensure proper locks are released in each *if-else* to avoid deadlocks.

## *int tag_send(int tag, int level, char\* buffer, size_t size)*

This is a *writer* thread.

TODO return what?

- TODO Instance access operations.
- Do a copy_from_user into an on-the-go-set array in the stack.
- Acquire level rwlock as writer (saving IRQ state): only one writer should be performing this at any given time.
- Acquire wait queue spinlock (without touching IRQ state).
- Check for active readers (use waitqueue_active(...)), exit if there's none.
- Release wait queue spinlock (as above).
- memcpy the message in the level buffer.
- Set message size.
- STORE MEMORY BARRIER
- Set level condition value to 1.
- Release level rwlock as writer (restoring IRQ state).
- Wake up the entire wait queue (use wake_up(...) on the level wait queue).

TSO bypasses are avoided by executing memory barriers embedded in spinlocks and wait queue APIs.

Ensure proper locks are released in each *if-else* to avoid deadlocks.

## *int tag_ctl(int tag, int command)*

TODO

# DATA STRUCTURES AND TYPES

## SHARED INSTANCES ARRAY
- Array of 256 structs with protected instance pointers, indexed by "tag descriptor".
- Two rw_semaphores or the like per each entry (that must then be a particular struct).
- Bitmask of free/used descriptors (consider fd_set?).

## INSTANCE DATA STRUCTURE
- Key.
- Array of 32 level data structures.
- Creator EUID.

## LEVEL DATA STRUCTURE
- Wait queue head (which embeds a spinlock).
- Pointer to a preallocated 1 page-buffer (using kmalloc) (needed compromise between complexity and resource usage).
- size_t size of the message currently stored.
- rwlock_t to access the buffer and the message size.
- A byte used as condition value for the wait queue.

# MODULE PARAMETERS

Consider adding anything you might need to debug this module.

- System call numbers (one pseudofile each) (read-only).
- Max number of active instances (configurable at insertion).
- Max message size (4 KB) (read-only).
- Currently active instances (read-only).
- Currently waiting threads on any level (read-only).

# CHAR DEVICE DRIVER(s)

**Remember to set the owner member!**

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
## ACCESS TO AN INSTANCE, REMOVAL
There are 3 kinds of threads: receivers, senders, removers.
Keep in mind that senders and removers always return, never deadlock, so their critical section time is always constant.
Each entry in the array holds two rw_semaphores and a pointer.
Remember that "lock" and "unlock" are called "down" and "up" for semaphores.
The first rw_sem is for receivers as readers, the second is for senders as readers, both are for removers as writers.
When a remover comes, it trylocks the receivers's one as a writer, then eventually locks the senders's one as a writer, then flips the instance pointer to NULL, releases both locks and does its thing.
When a receiver comes it trylocks its semaphore as reader, checks the pointer, eventually does its thing and unlocks its semaphore as reader.
When a sender comes it trylocks its semaphore as reader, checks the pointer, eventually does its thing and unlocks the semaphore as reader.

## ACCESS TO THE BST-DICTIONARY

TODO

## POSTING A MESSAGE ON A LEVEL

Each level structure embeds and rwlock_t: the writer takes it to post, updates the wakeup condition, releases it and wakes readers up. Readers acquire it to memcpy contents into an on-the-go-set array in the stack and release it afterwards (can't hold a spinlock while doing a sleeping call!); then, they call copy_to_user.

TODO

# TODO LIST

- How should messages be delivered?
- Is an RCU BST a good idea or can we just use an rw_sem?
- How are fd_sets implemented and used?
- Complete definition of all operations.
- Test multiple-locks scenarios.
- Synchronization of everything, also thinking about the device file read operation.
- Signals, interrupts, preemption and the like checks against deadlocks and similar problems. Remember that wait queues functions return -ERESTARTSYS when a signal was delivered and that signals are checked for upon return from interrupt or syscalls, so consider using local_locks to protect your (really) critical sections.
- Check TSO compliance everywhere, add memory fences where needed.
- Check against false cache sharing everywhere. Remember that one of our cache lines is 64-bytes long.
- Anything still marked as TODO here.

# EXTRAS
- Module parameters consistency check at insertion, especially for max values and sizes of stuff.
- Carefully think about what to expose as a module parameter (logic-less) and what needs more complexity,
thus requiring a device file.
- Signals support in system calls: handlers will be called upon return to user mode, so all you have
to do is place threads on an interruptible wait queue, check for pending signals and in case release all locks and return -EINTR from system calls. This is because signals are a user mode facility. Test this first.
- Module locking: module "put" and "get" to prevent removal.
- Splay trees as BSTs, using join-based alternative for deletion (to avoid splaying the predecessor to the top)
and make nodes (structures) cache-aligned (in GCC: "struct ... {...} ... __attribute__ ((aligned (L1_CACHE_BYTES)));").
- MODULE_INFO stuff!
- A more complete device driver?
