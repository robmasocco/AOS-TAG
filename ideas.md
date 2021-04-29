# GENERAL ARCHITECTURE NOTES

## KERNEL-SIDE ARCHITECTURE

- Centralized architecture: there's only one binary search tree to lookup active instances, register new ones, or delete existing ones from. Indexed by keys, each entry contains the tag descriptor for that instance, if present. Only for shared instances.
- The tag descriptor is an index in a kernel-level shared array of structs: pointers and two rw_semaphores.
- In the array structs, first things checked by every syscall are the semaphores, then IMMEDIATELY the validity of the pointer.
- The device file driver only has to scan the array.
- Permissions are implemented as simple checks of the current EUID against the creator's EUID when a thread calls a *send* or a *receive* on an active instance. Such EUID is stored upon creation of the instance and checked each time it is acted upon. As for any service like this, the *root user* (aka EUID 0) can do everything everywhere.
- When loaded, this does a *try_module_get* on the *scth* module in the *init_module*, which is a dependency, releasing it with a *module_put* in *cleanup_module*.
- Routines are embedded into functions, to simplify code-writing, system calls definitions and device drivers coding.

## BINARY SEARCH TREE

- Represents key-instance associations.
- Is indexed by int keys.
- Each entry is an int "tag descriptor", index in the array.
- Must be optimized for speed, "cache-like", so a splay tree should fit right in.
- Must be protected from concurrent access; see below.

## MODULE MANAGEMENT

### *init_module*

- Consistency checks on parameters values, against their default values where appropriate.
- *find_module* on the SCTH module.
- *try_module_get* on the SCTH module.
- Create the BST dictionary.
- Create the tag bitmask.
- *kmalloc* memory for the instance array.
- For each entry in the instance array:
    - Set the instance struct pointer to *NULL*.
    - Initialize the rw_sems.
- Register the device driver.
- Create the VFS node in */dev*.
- Hack the system call table and install the new calls.
    This is the most critical and hazardous step so we do it now, when it's almost sure we got this.

If any of the aforementioned steps fails, the routine terminates releasing all resources up to that point, returning an appropriate error code.

### *cleanup_module*

- Unhack the system call table.
    Being this the most important thing, we do it immediately.
- *module_put* on the SCTH module.
- Remove the VFS node in */dev*.
- Unregister the device driver.
- For each entry in the instance array:
    - If the pointer is not *NULL*, *kfree* it.
- *kfree* the instance array.
- Remove the tag bitmask.
- Remove the BST dictionary.

Each of the aforementioned steps must at least be attempted, in order to remove as many resources as possible. If at the end some failed, we should only report it in the kernel log since no error code can be returned.

### Module locking

A very simple module locking scheme is implemented in this project to ensure that syscalls do not end up operating on stale, inconsistent, not-anymore-present data (especially blocking ones), possibly causing kernel oopses or worse.
The *SCTH* module is a dependency, so it is locked upon insertion and released upon removal.
Then, in its wrapper, each system call does a *try_module_get(THIS_MODULE)* before attempting to execute its real code.
**Note that, due to how the module locking feature is implemented in the Linux kernel, this still leaves room for some really impossible race conditions that would consist in a system call executing code that lies in a released memory region (the part before the _try\_module\_get_). This is the best that we can do. Causing the aforementioned condition during normal execution would require surgical scheduler precision, excellent timing, and a strong will to wreak havoc. We assume that a user knows when to remove the module, and do all that is possible to prevent damage anywhere we can.**

# OPERATIONS DETAILS

The following pseudocode describes how all operations are performed, and contains some hints about why some choices were made about synchronization and other similar issues.

## *int tag_get(int key, int command, int permission)*

Opens a new instance of the service. You can open whatever instance you want but if permissions aren't ok for you then **all subsequent *receives* and *sends* will fail**. This behavior is intended since tag descriptors don't really mean much, i.e. we need to check for them in *receive* and *send* that could be called on a different instance than the one originally intended if someone called a *REMOVE* in the meantime, so it doesn't make much sense to also check them here.

This uses instance rw_sems as a writer to avoid race conditions on an instance.

Returns the tag descriptor (array index) of the new instance, or -1 and *errno* is set to indicate the cause of the error.

- Consistency checks on input arguments.
- If *command* is *TAG_OPEN* and *key* is not *IPC_PRIVATE*:
    - Acquire the tree rw_sem as a reader.
    - Make a query in the tree for the specified key.
    - Release the tree rw_sem as a reader.
- If *command* is *TAG_CREATE*:
    - If key is not *IPC_PRIVATE*:
        - Acquire the tree rw_sem as writer.
        - Look for the key in the tree, exit if found. Note that doing things like this is the only way to prevent adding multiple instances of the same key.
    - Acquire bitmask spinlock.
    - Linearly scan the bitmask to find a free entry in the array, then add it to the set and save the index.
    - Release bitmask spinlock.
    - Allocate and accordingly initialize a new instance struct.
    - If key is not *IPC_PRIVATE*: add a new entry to the tree.
        We do this now because we must check the result of the insert operation. Doing it later would mean being open to the possibility of leaving the internal state of the module corrupted, because of additional interruptions and error handling related to semaphores. Since we must anyhow hold the BST lock until the end, let's do this now to simplify things a lot, and make the effect visible later on when we'll release the lock.
    - Acquire both instance rw_sems as a writer.
    - Set the instance struct pointer to the new struct's address.
    - Release both instance rw_sems as a writer.
    - If key is not *IPC_PRIVATE*: release the tree rw_sem as a writer.
- Return.

Ensure proper locks are released in each *if-else* to avoid deadlocks.

## *int tag_receive(int tag, int level, char \*buffer, size_t size)*

This configures the running thread as a *reader thread*.
In this implementation I/O is *packetized*, meaning that only full messages are delivered, so this call will fail if the provided buffer isn't large enough.

Returns 0 if the message was read, or -1 and *errno* is set to indicate the cause of the error.

- Consistency checks on input arguments.
- Lock receivers's rw_sem as reader.
- Check instance pointer, eventually exit.
- Check permissions if required (flag), eventually exit.
- Acquire level condition spinlock.
- Atomically read the current *condition selector* from the level condition struct.
- Atomically increment the current *epoch presence counter* in the level condition struct.
- **MEMORY FENCE**
- Release level condition spinlock.
- Acquire instance condition spinlock.
- Atomically read the current *global condition selector* from the instance condition struct.
- Atomically increment the current *global epoch presence counter* in the instance condition struct.
- **MEMORY FENCE**
- Release instance condition spinlock.
- Wait on the current epoch's level queue (with *wait_event_interruptible(...)*) with (*curr_condition || curr_globl_condition*). Catch signals here and be very careful about which locks to release and counters to atomically decrement upon exit!!!
- If *curr_globl_condition == True* we've been awoken:
    - Atomically decrement both global and level epoch presence counter.
    - Exit.
- Atomically decrement global epoch presence counter. We're up now, no need to delay this further.
- If message size != 0 && usermode buffer size is enough:
    - *copy_to_user* the new message from the pointed buffer.
- Atomically decrement epoch presence counter.
- Release receivers's rw_sem as a reader.
- Return.

TSO bypasses are avoided by executing memory barriers embedded in spinlocks and wait queue APIs, and by using atomic GCC builtins.

Ensure to speed things up a bit if read message length is zero.

Ensure proper locks are released in each *if-else* to avoid deadlocks, that incremented counters are always subsequently decremented.

## *int tag_send(int tag, int level, char \*buffer, size_t size)*

This configures the running thread as a *writer thread*.

Only one writer should be active at any given time, since there's no message logging.

Returns 0 if the message was correctly sent, or -1 and *errno* is set to indicate the cause of the error.

- Consistency checks on input arguments.
- Lock senders's rw_sem as reader.
- Check instance pointer, eventually exit.
- Check permissions if required (flag), eventually exit.
- *kmalloc* a properly-sized message buffer.
- *copy_from_user* into the new message buffer.
    This is slow, might block and might not be necessary if no one's there to get it, but we do it without acquiring any level-related lock first since it's the only thing senders can do independently of each other when on a same level. Wasting momentarily a bit of time and memory is a fair risk to take.
- Acquire level senders mutex.
- Acquire level condition spinlock.
- Atomically read and flip the current *condition selector* from the level condition struct. This is the linearization point for the message buffer. Save the previous value.
- Reset the new condition value to 0x0.
- **MEMORY FENCE**
- Release level condition spinlock.
- Atomically read the current *epoch presence counter* from the level condition struct: exit if it is zero (no one is waiting for a message on this level, so discard yours). Remember to free the buffer!
- Set the level message pointer to the new buffer.
- Set message size.
- **STORE FENCE**
- Set the now "old" level condition to 0x1.
- **STORE FENCE**
- Wake up the current epoch's level wait queue (use *wake_up_all(...)*).
    Note that the APIs used prevent the "Lost wake-up problem".
    Also note that this call grabs the queue spinlock, wakes all readers that "got the message" in a single pass and then releases the queue spinlock. We're sure that all the threads that are in here must be awoken and are those that actually got the message, while those that came too late have been diverted onto the other queue.
- Wait for the old epoch presence counter to become zero.
    Busy-wait loops while executing in the kernel are bad, I know. That's why we call *schedule()* in the loop: the current CPU will be released by the task to execute other threads, e.g. the receivers we're waiting for.
- Set message size to 0 and buffer pointer to NULL (all registered receivers read it at this point). Save it to *kfree* it in a bit.
- **STORE FENCE**
- Release level senders mutex.
- Release senders's rw_sem as reader.
- *kfree* the message buffer.
- Return.

TSO bypasses are avoided by executing memory barriers and those embedded in spinlocks and wait queue APIs, and by using GCC atomic builtins.

Ensure to speed things up a bit if message length is zero, so the thread just have to be awoken and no blocking *copy_from_user* API is ever called.

Ensure proper locks are released in each *if-else* to avoid deadlocks.

## *int tag_ctl(int tag, int command)*

Allows the calling thread to awake all readers potentially waiting on any level of an instance, or to remove an instance.

Returns 0 if the requested operation was completed successfully, or -1 and *errno* is set to indicate the cause of the error.

- Consistency checks on input arguments.
- If *command* is *AWAKE_ALL*:
    - Lock senders's rw_sem as reader.
    - Check instance pointer, eventually exit.
    - Check permissions if required (flag), eventually exit.
    - Acquire instance awake_all mutex.
    - Acquire instance condition spinlock.
    - Atomically read and flip the current *condition selector* from the instance condition struct. Save the previous value.
    - Reset the new condition value to 0x0.
    - **MEMORY FENCE**
    - Release instance condition spinlock.
    - Set the now "old" global condition to 0x1.
    - **STORE FENCE**
    - For each level in the instance:
        - Wake up both level wait queues (use *wake_up_all(...)*).
            Note that the APIs used prevent the "Lost wake-up problem".
    - Busy-wait on old global epoch presence counter to become zero.
        This still needs to happen because even if there's no buffer to read from, the threads that were awoken need to *consume the condition*, i.e. be able to check that it is verified before another awaker comes and resets it.
        Just as in *tag_send*, call *schedule()* in the loop: the current CPU will be released by the task to execute other threads, e.g. the receivers we're waiting for.
    - Release instance awake_all mutex.
    - Release senders's rw_sem as reader.
- If *command* is *REMOVE*:
    - Trylock receivers rw_sem as writer, exit if this fails since at least a reader is there.
    - Lock senders rw_sem as writer.
    - Check instance pointer, eventually exit.
    - Check permissions if required (flag), eventually exit.
    - Save instance struct pointer and set it to *NULL*.
    - **MEMORY FENCE**
    - Release senders rw_sem as writer.
    - Release receivers rw_sem as writer.
    - Check the key, if it is not *IPC_PRIVATE*:
        - Acquire the tree rw_sem as writer.
        - Remove the entry from the tree.
        - Release the tree rw_sem as writer.
    - Acquire bitmask spinlock.
    - Remove the tag descriptor from the bitmask.
    - Release bitmask spinlock.
    - Set creator EUID to zero (for security), then *kfree* instance struct.
- Return.

Ensure proper locks are released in each *if-else* to avoid deadlocks.

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

This module includes a character device driver that allows every user to check the current state of the service.
A device file, */dev/aos_tag_status*, can be accessed and read entirely to end up with a text file that sums up the service's status.
Suggested programs for this are:

- *cat*
- *less -f*

Each line of the status device file describes a level of an active instance, with the following format:
**KEY    CREATOR EUID    LEVEL    WAITING THREADS**
Only active, i.e. opened by at least one thread, instances are described in this file.

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

# TODO LIST

- Load and unload scripts, that handle *insmod*, *rmmod* and possibly compilation accordingly.
- Hide debug audits with preprocessor directives.
- Try to remove memory fences and compiler barriers next to synchronization primitives operations.
- Documentation.

# EXTRAS

- Definitions of system call numbers for the user code given by *make* after module insertion using *awk*/*sed* to read numbers from pseudofiles and generate the header.
- *__randomize_layout* of some structs?
- Docs:
    - Words first, pseudocode last. Maybe a totally different file for that.
    - A README for SCTH.
        Mention that we left it as a secondary module to experiment with exported symbols, module dependencies, and module locking, but that for simplicity we just modified it a tad bit, i.e. we added a mutex to make operations atomic.
    - Usage manual, in the main page.

