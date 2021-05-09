# GENERAL ARCHITECTURE NOTES

## KERNEL-SIDE ARCHITECTURE

- Centralized architecture: there's only one binary search tree to lookup active instances, register new ones, or delete existing ones from. Indexed by keys, each entry contains the tag descriptor for that instance, if present. Only for shared instances.
- The tag descriptor is an index in a kernel-level shared array of structs: pointers and two rw_semaphores.
- In the array structs, first things checked by every syscall are the semaphores, then IMMEDIATELY the validity of the pointer.
- The device file driver only has to scan the array.
- Permissions are implemented as simple checks of the current EUID against the creator's EUID when a thread calls a *send* or a *receive* on an active instance. Such EUID is stored upon creation of the instance and checked each time it is acted upon. As for any service like this, the *root user* (aka EUID 0) can do everything everywhere.

## BINARY SEARCH TREE

- Represents shared key-instance associations.
- Is indexed by int keys.
- Each entry is an int "tag descriptor", index in the array.
- Must be optimized for speed, "cache-like", so a splay tree should fit right in.
- Must be protected from concurrent access; see below.

## MODULE LOCKING

A very simple module locking scheme is implemented in this project to ensure that syscalls do not end up operating on stale, inconsistent, not-anymore-present data (especially blocking ones), possibly causing kernel oopses or worse.
The *SCTH* module is a dependency, so it is locked upon insertion and released upon removal.
Then, in its wrapper, each system call does a *try_module_get(THIS_MODULE)* before attempting to execute its real code.
**Note that, due to how the module locking feature is implemented in the Linux kernel, this still leaves room for some really impossible race conditions that would consist in a system call executing code that lies in a released memory region (the part before the _try\_module\_get_). This is the best that we can do. Causing the aforementioned condition during normal execution would require surgical scheduler precision, excellent timing, and a strong will to wreak havoc. We assume that a user knows when to remove the module, and do all that is possible to prevent damage anywhere we can.**

# DATA STRUCTURES AND TYPES

## BST-DICTIONARY

This dictionary holds *key-tag descriptor* pairs of the instances that were **NOT** created as *IPC_PRIVATE*, thus meant to be shared. Goes with an rw_semaphore to synchronize accesses (see below), which is embedded into the structure and automatically used as part of the normal operations, so completely transparent to the code that uses the BST. Each node holds:

- int key, the dictionary key.
- int tag descriptor, index in the shared instances array.
- Pointers to make the tree work as a linked structure.

## INSTANCES ARRAY

Array of structs with protected instance pointers, indexed by "tag descriptor".

Goes with a bitmask that tells its current state, protected by a spinlock.

Each entry holds:

- Pointer to the corresponding instance data structure, meant to be NULL when the instance hasn't been created.
- Receivers rw_semaphore.
- Senders rw_semaphore.

## INSTANCE DATA STRUCTURE
- Key.
- For the levels, arrays of 32:
    - char * pointers to message buffers.
    - size_t sizes of the messages stored.
    - Mutexes to mutually exclude senders on each level.
    - Array of 2 wait queues.
    - Level conditions structs.
- Creator EUID.
- Protection-enabled binary flag. Set by *tag_get* upon instance creation, enables permissions checks for subsequent operations.
- Mutex to mutually exclude threads that execute an *AWAKE_ALL*.
- Instance-global condition struct.

## CONDITION DATA STRUCTURE

- One atomic char *epoch selector*.
- Array of two char *conditions*.
- Array of two atomic long *presence counters*.
- spinlock "condition lock".

Numeric fields are accessed using atomic operations, with the *RELAXED* memory order since we have no specific ordering requirement.

# MODULE PARAMETERS

- System call numbers (one pseudofile each) (read-only).
- Device driver major number (read-only).
- Max number of active instances (configurable at insertion but checked: must not drop below 256) (read-only).
- Max message size (configurable at insertion but checked: must not drop below 4 KB) (read-only). Comment in the docs/README that this better be page-aligned.

# CHAR DEVICE DRIVER

## OPEN

Takes a snapshot of the state of the AOS-TAG service and saves it in text form in a buffer, which acts as the file.
Such buffer is allocated with *vmalloc* instead of *kmalloc*, to avoid putting too much strain on the buddy allocators requesting many contiguous pages if the device file is opened many times. The buffer is then linked to the session accessing the *private_data* member of the *struct_file*.
The snapshot is taken by scanning the instances array and accessing only active instances. To do so, the senders rw_sem is acquired as reader, as explained in the Synchronization section.
Returns 0 if all was done successfully and the "file" is ready, or -1 and *errno* is set to indicate the cause of the error.

## CLOSE

Free all structures and text buffers.

## READ

*copy_to_user* stuff from the file buffer, with size checks, return values, EOF setting, loff advancing and stuff.

# SYNCHRONIZATION

Each synchronization scheme implemented, that makes the basic aforementioned operations work, is thoroughly described below, though the pseudocode above should have already given many hints.
Some sections rely on a light use of:

- Sleeping locks, in the form of mutexes and rw_semaphores. The last ones are used primarily as presence counters, with the ability to exclude threads when needed without holding CPUs.
    You may have noticed from the pseudocode above that when each of these locks is requested, the *interruptible/killable* variants of the APIs are used. This is intended because since any thread can request access to any tag entry in the instance array, independently of the instance effectively being active or not and of permissions allowing the following operations on it, there could be some activity on an instance. In the unfortunate (and unlikely, under normal usage) case that such activity is extensive and the call that tries to lock the rw_sems/mutexes blocks for too much time, it can be aborted with a signal.
    Since the semaphores are there to manage access to the internal state of the service and not really to wait indefinitely for events (like e.g. messages), and the *interruptible* variant for rw_semaphores is still quite [recent](https://www.spinics.net/lists/kernel/msg3759815.html), the *killable* variant is used almost everywhere, so in case of emergency *SIGKILL* should be used. When these interruptions occur, the calls try to return to user mode as soon as possible, releasing all the resources and memory they can in the process without taking any other lock.
    **When such variants aren't used it's because other threads, by either terminating successfully or being killed, will inevitably release the semaphore, and the last thread will successfully terminate without leaving an inconsistent module state.**
- Spinning locks, in the form of spinlocks, to guard status-critical data structures. Critical sections involving these have been kept as small and quick as possible, and are meant to be executed ASAP, so we'd like some speed also while locking. But we're not coding interrupt handlers, so we don't need the additional overhead that comes when blocking IRQs, which is why we use only the basic APIs.
    One last word about the ***condition structure* lock**: could we have used an *rwlock* there, instead of a spinlock? Sure, most of the time this is accessed by readers and a lock is really needed only to prevent a particularly bad race condition, so why the full exclusion? Because compared to spinlocks, especially when the critical section is short, rwlocks are [slow](https://www.kernel.org/doc/html/latest/locking/spinlocks.html#lesson-2-reader-writer-spinlocks), effectively slower than using a fully exclusive spinning lock. Considering that the critical sections that involve such structure are only made of one or two simple atomic operations, the choice has favored spinlocks instead of rwlocks.

## ACCESS TO THE BST-DICTIONARY

There's just an rw_semaphore to acquire and release: as a reader when making a query, as a writer when cutting an instance out or adding one. It is embedded into the data structure and its usage is part of the normal operations.

## ACCESS TO AN INSTANCE, REMOVAL, ADDITION

There are 4 kinds of threads: *receivers*, *senders*, *removers*, *adders*.

Keep in mind that senders and removers always return, never deadlock, so their critical section time is always constant or at least finite. Each entry in the array holds two rw_semaphores and a pointer. Remember that *lock* and *unlock* are called *down* and *up* for semaphores. The first rw_sem is for receivers as readers, the second is for senders as readers, both are for removers as writers.

When a remover comes, it trylocks the receivers's one as a writer, then **eventually** locks the senders's one as a writer, then flips the instance pointer to NULL, releases both locks and does its thing **afterwards** for the sake of speed. Note that the first trylock could mean either that the instance is really there but there's at least one reader in it, or that the instance isn't there because it is being removed or created, so nothing to do either way.

When a receiver comes it locks its semaphore as reader, checks the pointer, eventually does its thing and unlocks its semaphore as reader.
When a sender comes it locks its semaphore as reader, checks the pointer, eventually does its thing and unlocks the semaphore as reader.
These are both real locks since potential writers are *removers* and *adders*, both having a very short and deterministic critical section, and are deadlock-proof.

Things are a little bit different when *adding* an instance: at first, the bitmask is atomically checked for a free spot, then the *adder* thread locks both rw_sems as a writer since being the pointer *NULL*, eventual readers/writers would almost immediately get out, then sets the pointer to that of a new instance struct. The BST is kept locked during this in order to avoid adding a same key possibly multiple times.

Also, threads that come from the VFS while doing an *open* must synchronize with *adders* and *removers* to take a snapshot of each instance before it fades away. This can be accomplished by trylocking the senders rw_sem and checking the instance pointer. Using the receivers one causes a false positive for removers that want to check if no reader is there, this way they'd only have to wait for the snapshot to be taken since these threads deterministically release this rw_sem shortly after. Also, if the trylock fails it means the instance is being either created or removed, so the thread got there too early or late respectively.

## POSTING A MESSAGE ON A LEVEL

The algorithm described here is a variation of the algorithm used in RCU linked lists.

The writer posts a message in the level buffer (together with its size), updates the wakeup condition, then wakes readers up. Kernel-side buffers are dynamically allocated to allow for messages greater than two pages (8 MB) if required (i.e. temporarily saving the message to post in the stack it's not a great idea). Considering also the *copy_\** APIs, it won't be quick, but it won't waste any memory and use only what is necessary at any given time. Also, as for RCU, only one writer is allowed to change the epoch at any given time and then free the message buffer after the grace period, but given the nature of this system a sleeping lock, i.e. a mutex, is used instead of a spinlock.

The wakeup condition is a particular epoch-based struct. When the epoch selector gets flipped, that's a linearization point for the message buffer: all receiver threads that got in there before this will get the message, others were too late. The only difference is the need for a spinlock to avoid that the epoch selector gets flipped before a receiver can atomically increment the corresponding epoch presence counter: this is required here because if not, there could be some unlikely but dangerous race conditions that would lead to a receiver registering to an epoch that is *two times ahead* the one that it believes to be in, thus behaving incorrectly and skipping a message that it should get.

Writers wait for all readers that got a condition value, i.e. they busy-wait on the "old" presence counter to become zero. This represents RCU's grace period. For receivers, reading the current condition value at first, before atomically incrementing the presence counter, is very important to sync with the state, avoid deadlocks and be waited by the very next writer.

Full instance wakeups work in a similar fashion, as is clear from the pseudocode above. The only difference is that the wakeup is performed on both queues for each level since we can't know, nor should we care about, in which epoch each level is, thus in which queue each thread from the current instance-global epoch is found.

