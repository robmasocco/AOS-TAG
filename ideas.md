# GENERAL ARCHITECTURE NOTES

Brainstorming table for whatever needs to be discussed before the coding starts.

## KERNEL-SIDE ARCHITECTURE

- Centralized architecture: there's only one binary search tree to lookup active instances, register new ones, or delete existing ones from. Indexed by keys, each entry contains the tag descriptor for that instance, if present. Max 256 nodes should be allowed. Only for shared instances.
- The tag descriptor is an index in a kernel-level shared array of structs: pointers and two rw_semaphores.
- In the array structures, first things checked by every syscall are the semaphores, then IMMEDIATELY the validity of the pointer.
- The device file driver only has to lock and scan the array.
- Permissions are implemented as simple checks of the current EUID against the creator's EUID when a thread does a tag_get. Such EUID is stored upon creation of the instance and checked each time it is reopened.
- When loaded, this does a *try_module_get* on the *scth* module in the *init_module*, which is a dependency, releasing it with a *module_put* in *cleanup_module*.
- Routines should be embedded into functions, to simplify code-writing, system calls definitions and device drivers coding (if we ever get to that).

## BINARY SEARCH TREE

- Represents key-instance associations.
- Is indexed by int keys.
- Each entry is an int "tag descriptor", index in the array.
- Must be optimized for speed, "cache-like".
- Must be protected from concurrent access, using an rw_semaphore at least.

# OPERATIONS DETAILS

## *int tag_get(int key, int command, int permission)*

Opens a new instance of the service. You can open whatever instance you want but if permissions aren't ok for you then **all subsequent *receives* and *sends* will fail**.

This uses instance rw_sems as a writer to avoid race conditions with itself, while also checking for any activity on an instance.

Returns the tag descriptor (array index) of the new instance, or -1 and *errno* is set to indicate the cause of the error.

- *try_module_get*
- Consistency checks on input arguments.
- If *command* is *TAG_OPEN* and *key* is not *IPC_PRIVATE*:
    - Acquire the tree rw_sem as a reader.
    - Make a query in the tree for the specified key.
    - Release the tree rw_sem as a reader.
- If *command* is *TAG_CREATE*:
    - Acquire fd_set spinlock (allowing IRQs).
    - Linearly scan the set to find a free entry in the array, then add it to the set and save the index (use *!FD_ISSET* and *FD_SET*).
    - Release fd_set spinlock (as above).
    - Allocate and accordingly initialize a new instance struct.
    - Acquire both rw_sems as writer.
    - Set the instance struct pointer to the new struct's address.
    - Release both rw_sems as writer.
    - If key is not *IPC_PRIVATE*:
        - Acquire the tree rw_sem as a writer.
        - Add a new entry to the tree.
        - Release the tree rw_sem as a writer.
- *module_put*
- Return.

Ensure proper locks are released in each *if-else* to avoid deadlocks, and that the module is always released.

## *int tag_receive(int tag, int level, char \*buffer, size_t size)*

This configures the running thread as a *reader thread*.

Returns 0 if the message was read, or -1 and *errno* is set to indicate the cause of the error.

- *try_module_get*
- Consistency checks on input arguments.
- Lock receivers's rw_sem as reader (use the *_interruptible* variant here).
- Check instance pointer, eventually exit.
- Check permissions if required (flag), eventually exit.
- Atomically increment level readers counter in the instance struct.
- Acquire condition rw_sem as reader (this might be *_interruptible*).
- Read current condition value.
- Atomically increment epoch presence counter.
- Release condition rw_sem as reader.
- Wait on the queue (with *wait_event_interruptible(...)*) with condition according to current condition value (*if 0 wait on 1 else wait on 0*, an *if-else* should suffice). Catch signals here and be very careful about which locks to release and counters to atomically decrease upon exit!!!
- *preempt_disable()*
- *memcpy* the new message from the level buffer into an on-the-go-set array in the stack.
- Atomically decrease epoch presence counter (with preemption disabled!).
- Atomically decrease level readers counter in the instance struct (this too with preemption disabled, because why not?).
- *preempt_enable()*
- Release receivers's rw_sem as a reader (no need to delay this further).
- *Copy_to_user* the new message.
- *module_put*
- Return.

TSO bypasses are avoided by executing memory barriers embedded in spinlocks and wait queue APIs.

Even if new readers register themselves on the queue, those just awoken should prevent writers from running until they get the newest message.

Ensure proper locks are released in each *if-else* to avoid deadlocks, and that the module is always released.

## *int tag_send(int tag, int level, char \*buffer, size_t size)*

This configures the running thread as a *writer thread*.

Might have to act in a particular way if messages have length zero, to not disturb *copy* APIs and the like.

Returns 0 if the message was correctly sent, or -1 and *errno* is set to indicate the cause of the error.

- *try_module_get*
- Consistency checks on input arguments.
- Lock senders's rw_sem as reader.
- Check instance pointer, eventually exit.
- Check permissions if required (flag), eventually exit.
- *Copy_from_user* into an on-the-go-set array in the stack.
- Acquire level writers mutex.
- Acquire wait queue spinlock.
- Check for active readers (use *waitqueue_active(...)*), exit if there's none.
- Release wait queue spinlock.
- *preempt_disable()*
- *memcpy* the message in the level buffer.
- Set message size.
- **STORE FENCE**
- *preempt_enable()*
- Acquire condition rw_sem as writer.
- Flip level condition value (a XOR with 0x1 should suffice).
- Wake up the entire wait queue (use *wake_up(...)* on the level wait queue).
- Busy-wait on the epoch presence counter to become zero.
- Release condition rw_sem as writer.
- *memset* level buffer to 0 and set size to 0 (for security).
- Release level writers mutex.
- Release senders's rw_sem as a reader.
- *module_put*
- Return.

TSO bypasses are avoided by executing memory barriers and those embedded in spinlocks and wait queue APIs.

Only one writer should be active at any given time.

Ensure proper locks are released in each *if-else* to avoid deadlocks, and that the module is always released.

## *int tag_ctl(int tag, int command)*

Allows the calling thread to awake all readers potentially waiting on any level of an instance, or to remove an instance.

Returns 0 if the requested operation was completed successfully, or -1 and *errno* is set to indicate the cause of the error.

- *try_module_get*
- Consistency checks on input arguments.
- If *command* is *AWAKE_ALL*:
    - Call a write with a zero-length message on all levels, reusing the *send* routine.
- If *command* is *REMOVE*:
    - Trylock receivers rw_sem as writer, exit if at least a reader is there.
    - Lock senders rw_sem as writer.
    - Save instance struct pointer and set it to *NULL*.
    - Release senders rw_sem as writer.
    - Release receivers rw_sem as writer.
    - Check the key, if it is not *IPC_PRIVATE*:
        - Acquire the tree rw_sem as writer.
        - Remove the entry from the tree.
        - Release the tree rw_sem as writer.
    - Acquire fd_set spinlock (allowing IRQs).
    - Remove the tag descriptor from the set (using *FD_CLR*).
    - Release fd_set spinlock (as above).
    - Set creator EUID to zero (for security), then *kfree* instance struct.
- *module_put*
- Return.

Ensure proper locks are released in each *if-else* to avoid deadlocks, and that the module is always released.

# DATA STRUCTURES AND TYPES

## BST-DICTIONARY

This dictionary holds *key-tag descriptor* pairs of the instances that were **NOT** created as *IPC_PRIVATE*, thus meant to be shared. Goes with an rw_semaphore to synchronize accesses (see below). Each node holds:

- int key, the dictionary key.
- int tag descriptor, index in the shared instances array.
- A shitload of pointers to make the tree work as a linked structure.

## SHARED INSTANCES ARRAY

Array of 256 structs with protected instance pointers, indexed by "tag descriptor".

Goes with an *fd_set* that tells its current state, protected by a spinlock.

Each entry holds:

- Pointer to the corresponding instance data structure, meant to be NULL when the instance hasn't been created.
- Receivers rw_semaphore.
- Senders rw_semaphore.

## INSTANCE DATA STRUCTURE
- Key.
- Array of 32 level data structures.
- Creator EUID.
- Protection-enabled binary flag. Set by *tag_get* upon instance creation, enables permissions checks.
- Array of 32 atomic counters, one for each level, to count the readers.

## LEVEL DATA STRUCTURE

- Wait queue head (which embeds a spinlock).
- Pointer to a preallocated 1 page-buffer (using kmalloc).
- size_t size of the message currently stored.
- Mutex to mutually exclude writers.
- Single char/int used as condition value for the wait queue.
- rw_sem "condition rw_sem" to synchronize readers and writers together. This has to be sleeping since it has to allow existing readers to consume the message (the writer might also exchange its place for one of them this way, freeing an additional CPU core).
- Atomic epoch presence counter, to have writers wait for all registered readers when a message is delivered.

# MODULE PARAMETERS

Consider adding anything you might need to debug this module.

- System call numbers (one pseudofile each) (read-only).
- Max number of active instances (configurable at insertion).
- Max message size (4 KB) (read-only).
- Currently active instances (read-only).
- Currently waiting threads on any level (read-only).

# CHAR DEVICE DRIVER

**Remember to set the *owner* member to _THIS\_MODULE_!**

Compute a fair exceeding estimate of the buffer size in *init_module*, using 80 chars lines * 32 levels * how many max instances the module is started with.

Return *-ENOMEM* or similar on open if allocation fails or memory is insufficient.

Write stuff on lines, separating data with tabs.

Compute the size of the file as you produce it with subsequent calls to _sprintf_.l, and store it in a struct together with a pointer to the buffer holding the contents.

## OPEN

Takes a snapshot of the state of the AOS-TAG service and saves it in text form in a buffer, which acts as the file.

Returns 0 if all was done successfully and the "file" is ready, or -1 and *errno* is set to indicate the cause of the error.

Keep in mind that all data that forms the status of the system is at most 32 bits long, so many unsigned ints and type casts should suffice.

- Allocate a *line buffer* (80 chars) in the stack and *memset* it to 0.
- Allocate a 32-entries unsigned int array in the stack, for readers presence counters.
- Allocate two unsigned ints, for the key and the creator EUID.
- *kzalloc* a *file buffer* which will hold the file's contents, size: *max_instances* * *32 (levels)* * *80 (chars)*.
- Allocate a _char *_ in the stack that for now points to the base of the aforementioned buffer.
- For each entry in the instance struct array:
    - Trylock senders rw_sem as reader, *continue* if it fails (means that the instance is unavailable: it is being removed or created).
    - If the instance struct pointer is not *NULL*:
        - Get the key, the creator EUID and (atomically) the readers presence counters (in another *for* loop).
        - **STORE FENCE**
        - Release senders rw_sem as reader.
        - For each of the 32 levels:
            - *memset* the line buffer to 0.
            - *sprintf* status information in the line buffer: "TAG-key TAG-creator TAG-level Waiting-threads". Get the number of bytes written, it'll be useful in a moment.
            - *memcpy* the contents of the line buffer in the file buffer. Add a newline character at the end.
            - Advance the pointer accordingly.
- Set the *private_data* member of the current *struct file* to the file buffer base.
- Set all other required data, like *f_pos* and stuff.
- Return.

Again, be sure to release all locks and memory areas on any *if-else* sequence and exit.

## CLOSE

- Release the buffer.
- Set *private_data* to *NULL*.
- Return 0. Nothing can go wrong (maybe only *kfree*, should we trace it?).

## READ

*copy_to_user* stuff from the file buffer, with size checks, return values, EOF setting, f_pos advancing and shit.

## LSEEK

Reset *f_pos* as requested (if possible).

## WRITE

Nop.

## IOCTL

Nop.

## Any other op that might be required to make this work

Nop.

# DEVICE FILE(s)

A char device driver is required for these, maybe more than one or some minor-based behaviour.

To create a device file in */dev* inside *init_module* specifying your device driver have a look at [this](https://stackoverflow.com/questions/49350553/can-i-call-mknod-from-my-kernel-module).

## STATUS
As requested, line-by-line status report. Located in /dev. Named */dev/aos_tag*.

## I/O (?)

A generic userland thread becomes a writer/reader through these device files.
What about IPC_PRIVATE? Which routines would need to be called?
Develop the baseline version first, then make sure it is doable and discuss it with Quaglia to avoid conflicts with the specification. Could be a nice addition. Might require a different device driver, or an extension of that using the minor number. Or, it might be ioctl-based.

# SYNCHRONIZATION

**At first, each operation should be protected with a *try_module_get/module_put* pair, the very first and last instructions of each system call, to ensure that the data structures we're about to access don't magically fade away whilst we're operating on them.**

Don't use the *_interruptible* variants of rw_semaphores's APIs unless otherwise specified, so signals won't kick our butt.

## ACCESS TO THE BST-DICTIONARY

There's just an rw_semaphore to acquire and release: as a reader when making a query, as a writer when cutting an instance out or adding one.

## ACCESS TO AN INSTANCE, REMOVAL, ADDITION

There are 3 kinds of threads: *receivers*, *senders*, *removers*.

Keep in mind that senders and removers always return, never deadlock, so their critical section time is always constant. Each entry in the array holds two rw_semaphores and a pointer. Remember that *lock* and *unlock* are called *down* and *up* for semaphores. The first rw_sem is for receivers as readers, the second is for senders as readers, both are for removers as writers.

When a remover comes, it trylocks the receivers's one as a writer, then **eventually** locks the senders's one as a writer, then flips the instance pointer to NULL, releases both locks and does its thing **afterwards** for the sake of speed. Note that the first trylock could mean either that the instance is really there but there's at least one writer in it, or that the instance isn't there, so nothing to do either way.

When a receiver comes it locks its semaphore as reader, checks the pointer, eventually does its thing and unlocks its semaphore as reader.
When a sender comes it locks its semaphore as reader, checks the pointer, eventually does its thing and unlocks the semaphore as reader.
These are both real locks since potential writers are *removers* and *adders*, and both have a very short and deterministic critical section, and are deadlock-proof.

Things are a little bit different when *adding* an instance: at first, the bitmask is atomically checked for a free spot, then the *adder thread* locks both rw_sems as a writer since being the pointer *NULL*, eventual readers/writers would almost immediately get out, then sets the pointer to that of a new instance struct.

Also, threads that come from the VFS while doing an *open* must synchronize with *adders* and *removers* to take a snapshot of each instance before it fades away. This can be accomplished by locking the senders rw_sem and checking the instance pointer. Using the receivers one causes a false positive for removers that want to check if no reader is there, but that would only have to wait for the snapshot to be taken since these threads deterministically release this rw_sem shortly after.

## POSTING A MESSAGE ON A LEVEL

The writer posts a message in the level buffer (together with its size), updates the wakeup condition, then wakes readers up. Readers *memcpy* contents into an on-the-go-set array in the stack; then, they call *copy_to_user*. Buffers are preallocated for each level (a single page, compromise between complexity and resource usage). These sections run with preemption disabled for the sake of speed.

The wakeup condition is a single value which is flipped each time a writer posts a message, and readers will always wait on the opposite value; thus, this works as a linearization point for the level data structure (evident if you see the pseudocode above).

The condition value is protected by an rw_sem and there's also an atomic presence counter to distinguish between the moments when a message has been posted and has yet to be posted in a concurrent scenario for the readers, and make writers wait for all readers that got a condition value. This allows for some degree of concurrency, but heavily relies on having the wait queues APIs check the condition before going to sleep. Reading the current condition value at first, before atomically incrementing the presence counter, is very important to synch with the state, avoid deadlocks and be waited by the very next writer.

# TODO LIST

- BST-Dictionary implementation (also check and merge guidelines below and above).
- Signals, interrupts, preemption and the like checks against deadlocks and similar problems. Remember that wait queues functions return *-ERESTARTSYS* when a signal was delivered. Consider using local_locks to protect your (really) critical sections. See our little golden screenshot from our course materials to know how signals work (and remember: they're usermode shit, you just return -EINTR).
- Check TSO compliance everywhere, add memory fences where needed.
- Check against false cache sharing everywhere. Remember that one of our cache lines is 64-bytes long.
- Anything still marked as TODO here.
- Load and unload scripts, that handle *insmod*, *rmmod* and possibly compilation accordingly.

# EXTRAS

- Module parameters consistency check at insertion during *init_module*, especially for max values and sizes of stuff.
- Error checks and errno settings everywhere.
- Splay trees as BSTs, using join-based alternative for deletion (to avoid splaying the predecessor to the top)
and make nodes (structures) cache-aligned (in GCC: "struct ... {...} ... \__attribute__ ((aligned (L1_CACHE_BYTES)));").
- Definitions of system call numbers for the user code given by *make* after module insertion using *awk* to read numbers from pseudofiles.
