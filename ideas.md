# GENERAL NOTES

## KERNEL-SIDE ARCHITECTURE

The main goal of the design process for this project was to end up with an IPC system that could, according to the specification, be efficient enough without adding any overhead to the execution of other parts of the kernel. This is the reason behind many choices that will be thoroughly described below but that we can also summarize as follows:

- Complete absence of critical sections in which interrupts are also blocked.
- Very few sections in which spinning locks are acquired, only to execute a handful of instructions for which there isn't an atomic counterpart. And even in these sections only preemption is blocked, not interrupts, since per-CPU memory is never used and the system is completely independent of IRQs.
- Almost each time a blocking operation is performed, the thread can be interrupted or killed and the state of the module is preserved.
- Every time the system asks other parts of the kernel for something, e.g. memory, the request is done with normal priority and may actually lead the thread to sleep since these routines are not interrupt handlers.
- When multiple threads must access a shared resource but only some of them can, sleeping synchronization primitives are used which support fairness-oriented optimistic spinning schemes, like *mutexes* and *rw_semaphores*.

These design choices lay the foundations of the underlying architecture of this system, with which a thread that executes one of the system calls interacts to access an instance and fulfill the user's request.
The architecture, consisting of data structures in kernel memory, needs to allow accesses to two kinds of instances (given of course the necessary permission checks, described later on):

- *Shared* instances, i.e. those created with a key different from *IPC_PRIVATE* and that can be reopened by other threads.
- *Private* instances, i.e. those created with *IPC_PRIVATE* from *sys/ipc.h* as key; these shall be given a tag descriptor that is not associated with any key.

So a thread that wishes to interact with an instance should first create or open it with a given key, then get the corresponding tag descriptor to use in other calls.  It is evident that two different data structures are required to solve the two steps: a *dictionary* that allows to quickly recover the tag descriptor associated to a **shared** instance, if any, and an *array* that can be used to immediately access an instance given its tag descriptor. In this project, the dictionary is implemented as a particular kind of binary search tree.

### BST Dictionary

This dictionary holds *key-tag descriptor* pairs of the instances that were not created as *IPC_PRIVATE*, thus meant to be shared. It is indexed by keys and each node stores, together with the necessary pointers, the tag descriptor of the corresponding instance.
Concurrent accesses to this structure are regulated with an rw_semaphore that allows multiple readers to perform queries and single writers to update it, adding or removing nodes, whilst excluding all readers and other writers.
While an AVL tree would have certainly worked in this scenario, the choice has been made to optimize the dictionary even more considering an average usage pattern: it is reasonable to suppose that after a shared instance is created, and its node added to the tree, such instance will be referenced again multiple times by other threads that want to open it, possibly after a short time. The tree should then work like a *cache*: exploiting temporal locality with spatial locality by keeping "recent" nodes close to the root, making searches quicker.
The BST that best fits these requirements without being too complicated is the **splay tree**, and it is how the BST dictionary is implemented in this project. Simply put, it differs from an AVL in the fact that each time a node is accessed, for any kind of operation, an heuristic named *splay* is performed on that node consisting in a series of rotations to bring it to the root. Balance of the tree is not explicitly maintained, so searches prove to be efficient only in an amortized analysis, but the space that each node requires and the time needed to perform rotations are less since no balancing information has to be stored, checked or updated.
The only difference from the original version proposed by D. Sleator and R. Tarjan lies in the fact that we want to allow searches to be performed concurrently, which is not possible if at the end we need to splay the node (or the leaf node we end up at). Thus, **in this implementation we do not splay after searches**. This has the side effect that particularly pathological usage patterns may lead to a completely unbalanced tree, in which a search could have linear cost. This is indeed a compromise that we intend to make.

### Instances Array

As previously stated, this array allows to access every instance in the system given its tag descriptor, which is simply a valid index in it.
Each entry in this array consists of a struct holding three members:

- A pointer to the corresponding *tag struct* holding all data necessary to represent an instance and its levels.
- Two rw_semaphores, required to regulate concurrent access to an instance according to the specification, by threads that need to perform any of the allowed operations. The usage of these semaphores will be clearly explained in the Syncrhonization section later on.

Remember that, by specification, a thread can access an instance if it knows the tag descriptor and has compatible permissions, i.e. it can skip the reopening step. Thus, we need a way to check whether an instance if really *present* before acting on it. This is why each system call but *tag_get*, while accessing an entry in the array, first checks if the pointer to the *tag struct* is valid or not. Routines that need to create or remove instances act on such pointers very quickly: they set it when the *tag struct* is ready to be accessed or set it to NULL first and then start the removal process.
The contents of each *tag struct* can be summarized as follows:

- Key.
- For the levels, arrays of 32:
    - Pointers to message buffers.
    - Sizes of the messages stored.
    - Mutexes to mutually exclude senders on each level.
    - Array of 2 wait queues.
    - Level *conditions structs*.
- Creator EUID.
- *Protection-enabled* binary flag. Set by *tag_get* upon instance creation, enables permissions checks for subsequent operations.
- Mutex to mutually exclude threads that execute an *AWAKE ALL*.
- Instance-global *condition struct*.

*Condition structs* are used to materialize points in time when a message is delivered to the threads that could start to wait for it in time to get it, and when threads that started to wait on any level of an instance are awaken by a call to *tag_ctl(AWAKE_ALL)*. They implement an epoch-based scheme similar to what happens in RCU linked lists. Their use will be described later, and their contents are:

- One atomic *epoch selector*.
- Array of two *conditions*.
- Array of two atomic *presence counters*.
- A spinlock to allow for multiple operations to be performed atomically.

Much of the code regarding conditions is implemented as macros included in the header *utils/aos-tag_conditions.h*, which is adequately documented.

# SYNCHRONIZATION

Each synchronization scheme implemented, that makes the basic aforementioned operations work, is thoroughly described below, though the pseudocode above should have already given many hints.
Some sections rely on a light use of:

- Sleeping locks, in the form of mutexes and rw_semaphores. The last ones are used primarily as presence counters, with the ability to exclude threads when needed without holding CPUs.
    You may have noticed from the pseudocode above that when each of these locks is requested, the *interruptible/killable* variants of the APIs are used. This is intended because since any thread can request access to any tag entry in the instance array, independently of the instance effectively being active or not and of permissions allowing the following operations on it, there could be some activity on an instance. In the unfortunate (and unlikely, under normal usage) case that such activity is extensive and the call that tries to lock the rw_sems/mutexes blocks for too much time, it can be aborted with a signal.
    Since the semaphores are there to manage access to the internal state of the service and not really to wait indefinitely for events (like e.g. messages), and the *interruptible* variant for rw_semaphores is still quite [recent](https://www.spinics.net/lists/kernel/msg3759815.html), the *killable* variant is used almost everywhere, so in case of emergency *SIGKILL* should be used. When these interruptions occur, the calls try to return to user mode as soon as possible, releasing all the resources and memory they can in the process without taking any other lock.
    **When such variants aren't used it's because other threads, by either terminating successfully or being killed, will inevitably release the semaphore, and the last thread will successfully terminate without leaving an inconsistent module state.**
- Spinning locks, in the form of spinlocks, to guard status-critical data structures. Critical sections involving these have been kept as small and quick as possible, and are meant to be executed ASAP, so we'd like some speed also while locking. But we're not coding interrupt handlers, so we don't need the additional overhead that comes when blocking IRQs, which is why we use only the basic APIs.
    One last word about the ***condition structure* lock**: could we have used an *rwlock* there, instead of a spinlock? Sure, most of the time this is accessed by readers and a lock is really needed only to prevent a particularly bad race condition, so why the full exclusion? Because compared to spinlocks, especially when the critical section is short, rwlocks are [slow](https://www.kernel.org/doc/html/latest/locking/spinlocks.html#lesson-2-reader-writer-spinlocks), effectively slower than using a fully exclusive spinning lock. Considering that the critical sections that involve such structure are only made of one or two simple atomic operations, the choice has favored spinlocks instead of rwlocks.

TODO Fences.

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

## MODULE LOCKING

A very simple module locking scheme is implemented in this project to ensure that syscalls do not end up operating on stale, inconsistent, not-anymore-present data (especially blocking ones), possibly causing kernel oopses or worse.
The *SCTH* module is a dependency, so it is locked upon insertion and released upon removal.
Then, in its wrapper, each system call does a *try_module_get(THIS_MODULE)* before attempting to execute its real code, and terminates with a *module_put*.
Note that, due to how the module locking feature is implemented in the Linux kernel, this still leaves room for some really impossible race conditions that would consist in a system call executing code that lies in a released memory region (the part before the _try\_module\_get_). This is the best that we can do. Causing the aforementioned condition during normal execution would require surgical scheduler precision, excellent timing, and a strong will to wreak havoc. We assume that a user knows when to remove the module, and do all that is possible to prevent damage anywhere we can.

# OPERATIONS DETAILS

# CHARACTER DEVICE DRIVER

The device driver included in this module has the only purpose of offering a quick way to instantly check the state of the AOS-TAG system.
Thus, a single device file named *aos_tag_status* is created in */dev* with read-only permissions for all users during the module's initialization routine: this is achieved with a series of calls that first involve the creation of a class in *sysfs* and then of a VFS node in */dev*. The module's cleanup routine removes everything in reverse.
The only three routines included in the driver are *open*, *read* and *release*, whilst *write* and (unlocked) *ioctl* are included just as nops that return *-EPERM*.

The idea behind this driver is to take a snapshot of the status of the system each time the device file is opened, and then return it to the user space code reading the file with subsequent calls to *read*. The snapshot is essentially a kernel buffer, allocated with *vmalloc* because of its potential size, which holds the status of the system in human-readable text form as explained before. The pointer to such buffer is stored in the *private_data* member of the *struct file* passed to *open*. This "fake text file" remains the same for the process that opened it until it gets closed, and is generated by *open* with a two-pass linear scan of the instances array:

- During the first pass, the data encoding the status of the system is read as quickly as possible by scanning the instances array entries, taking care to note which instances are active and how many threads are waiting on their levels by looking directly at conditions presence counters. This data is saved in an array of structures defined just for this purpose. In order to prevent removers from deleting an instance while *open* is accessing it, the sensers rw_semaphore is acquired: this is both because *open* will do its job really quickly (it doesn't acquire any other lock, just reads data) so a remover won't have to wait that much, and because locking the receivers one would cause a false positive for *tag_ctl(REMOVE)*, looking like a receiver was actually waiting on a level of that instance.
- During the second pass, 

Following calls to *read* will only access the text file buffer, determine the appropriate amount of data to copy from it to user space, and then advance the file offset accordingly until hitting *EOF*.
A call to *close* will then invoke the *release* function, which simply releases the buffer.

## OPEN

Takes a snapshot of the state of the AOS-TAG service and saves it in text form in a buffer, which acts as the file.
Such buffer is allocated with *vmalloc* instead of *kmalloc*, to avoid putting too much strain on the buddy allocators requesting many contiguous pages if the device file is opened many times. The buffer is then linked to the session accessing the *private_data* member of the *struct_file*.
The snapshot is taken by scanning the instances array and accessing only active instances. To do so, the senders rw_sem is acquired as reader, as explained in the Synchronization section.
Returns 0 if all was done successfully and the "file" is ready, or -1 and *errno* is set to indicate the cause of the error.

## CLOSE

Free all structures and text buffers.

## READ

*copy_to_user* stuff from the file buffer, with size checks, return values, EOF setting, loff advancing and stuff.

# TESTING

