# GENERAL NOTES

## KERNEL-SIDE ARCHITECTURE

The main goal of the design process for this project was to end up with an IPC system that could, according to the specification, be efficient enough without adding any overhead to the execution of other parts of the kernel. This is the reason behind many choices that will be thoroughly described below but that we can also summarize as follows:

- Complete absence of critical sections in which interrupts are also blocked.
- Very few sections in which spinning locks are acquired, only to execute a handful of instructions for which there isn't an atomic counterpart. And even in these sections only preemption is blocked, not interrupts, since per-CPU memory is never used and the system is completely independent of IRQs.
- Each time a blocking operation is performed, the thread can be interrupted or killed and the state of the module is preserved.
- Every time the system asks other parts of the kernel for something, e.g. memory, the request is done with normal priority and may actually lead the thread to sleep since these routines are not interrupt handlers.
- When multiple threads must access a shared resource but only some of them can, sleeping synchronization primitives are used which support fairness-oriented optimistic spinning schemes, like *mutexes* and *rw_semaphores*.

These design choices lay the foundations of the underlying architecture of this system, with which a thread that executes one of the system calls interacts to access an instance and fulfill the user's request.
The architecture, consisting of data structures in kernel memory, needs to allow accesses to two kinds of instances (given of course the necessary permission checks, described later on):

- *Shared* instances, i.e. those created with a key different from *IPC_PRIVATE* and that can be reopened by other threads.
- *Private* instances, i.e. those created with *IPC_PRIVATE* from *sys/ipc.h* as key; these shall be given a tag descriptor that is not associated with any key.

So, a thread that wishes to interact with an instance should first create or open it with a given key, then get the corresponding tag descriptor to use in other calls.  It is evident that two different data structures are required to solve the two steps: a *dictionary* that allows to quickly recover the tag descriptor associated to a **shared** instance, if any, and an *array* that can be used to immediately access an instance given its tag descriptor. In this project, the dictionary is implemented as a particular kind of binary search tree.

### BST Dictionary

This dictionary holds *key-tag descriptor* pairs of the instances that were not created as *IPC_PRIVATE*, thus meant to be shared. It is indexed by keys and each node stores, together with the necessary pointers, the tag descriptor of the corresponding instance. It must support basic operations like *insert*, *search* and *delete*.
Concurrent accesses to this structure are regulated by an rw_semaphore that allows multiple readers to perform queries and single writers to update it, adding or removing nodes, whilst excluding all readers and other writers.
While an AVL tree would have certainly worked in this scenario, the choice has been made to optimize the dictionary even more considering an average usage pattern: it is reasonable to suppose that after a shared instance is created, and its node added to the tree, such instance will be referenced again multiple times by other threads that want to open it, possibly but not necessarily after a short time. The tree should then work like a *cache*: exploiting temporal locality with spatial locality by keeping "recent" nodes close to the root, making searches quicker.
The BST that best fits these requirements without being too complicated is the **splay tree**, and it is how the BST dictionary is implemented in this project. Simply put, it differs from an AVL in the fact that each time a node is accessed, for any kind of operation, an heuristic named *splay* is performed on that node consisting in a series of rotations to bring it to the root. Balance of the tree is not explicitly maintained, so operations prove to be efficient only in an amortized analysis, but the space that each node requires and the time needed to perform rotations are lower since no balancing information has to be stored, checked or updated. The amortized analysis shows that we can expect logarithmic access times for every operation: *O(log(n))*, where *n* is the number of nodes in the tree.
The only major difference of this implementation from the original version proposed by D. Sleator and R. Tarjan in 1985 lies in the fact that we want to allow searches to be performed concurrently, which is not possible if at the end we need to splay the node, or the leaf node we end up at. Thus, **in this implementation we do not splay after searches**. This has the side effect that particularly pathological usage patterns may lead to a completely unbalanced tree, in which a search could have linear cost. This is indeed a compromise that we intend to make.
Also, the deletion operation has been implemented using a *join-based* scheme: instead of splaying the parent of the removed node to the root we splay the node to delete (if any), then we cut the tree in two subtrees, splay the node with the largest key in the left subtree and attach it as left son of the right subtree's root.
Another optimization that has been chosen has to do with caching of nodes: given how small the size of the corresponding struct is, a compiler optimization has been added to align them to the x86 cache line size.
The code for this data structure, based on *kmalloc* for the dynamic allocation of nodes, can be found in the *aos-tag/splay-trees_int-keys/* subdirectory.

### Instances Array

As previously stated, this array allows access to every instance in the system given its tag descriptor, which is simply a valid index in it.
Each entry in this array consists of a struct holding three members:

- A pointer to the corresponding *tag struct* holding all data necessary to represent an instance and its levels.
- Two rw_semaphores, required to regulate concurrent accesses to an instance according to the specification, by threads that need to perform any of the allowed operations. The usage of these semaphores will be clearly explained in the next section.

Remember that, by specification, a thread can access an instance if it knows the tag descriptor and has compatible permissions, i.e. it can skip the reopening step. Thus, we need a way to check whether an instance is really *present* before acting on it. This is why each system call except *tag_get*, while accessing an entry in the array, first checks if the pointer to the *tag struct* is valid or not. Routines that need to create or remove instances act on such pointers very quickly: they set it when the *tag struct* is ready to be accessed or set it to NULL first and then start the removal process.
The contents of each *tag struct* can be summarized as follows:

- Key.
- For the levels, arrays of 32:
    - Pointers to message buffers.
    - Sizes of the messages stored.
    - Mutexes to mutually exclude senders on each level.
    - Array of 2 wait queues.
    - Level *condition structs*.
- Creator EUID.
- *Protection-enabled* binary flag. Set by *tag_get* upon instance creation, enables permissions checks for subsequent operations.
- Mutex to mutually exclude threads that execute an *AWAKE ALL*.
- Instance-global *condition struct*.

*Condition structs* are used to materialize points in time when a message is delivered to the threads that could start to wait for it in time to get it, and when threads that started to wait on any level of an instance are awaken by a call to *tag_ctl(AWAKE_ALL)*. They implement an epoch-based scheme similar to what happens in RCU linked lists. Their use will be described later, and their contents are:

- One atomic *epoch selector*.
- Array of two *condition values*.
- Array of two atomic *presence counters*.
- A spinlock to allow for multiple operations to be performed atomically.

Much of the code regarding conditions is implemented as macros included in the header *utils/aos-tag_conditions.h*, which is adequately documented.

The instance array comes with an associated bitmask, implemented as an array of *ulongs* with a set of macros defined in *utils/aos-tag_bitmask.h*. The point of this auxiliary structure is to quickly get a free spot when adding a new instance, if any. Given how quickly it is accessed, it is protected with a spinlock.

# OPERATIONS AND SYNCHRONIZATION DETAILS

Each synchronization and operation scheme implemented will now be briefly but thoroughly described. What will not be described here is a series of small operational details of the single four system calls that can easily be inferred from their source code, contained in the file *aos-tag_syscalls.c*.
As has been stated above, these rely on a light use of:

- Sleeping locks, in the form of mutexes and rw_semaphores. The last ones are used as presence counters, with the ability to exclude threads when needed without holding CPUs should the optimistic spinning scheme they embed fail.
    When each of these locks is requested, the *interruptible/killable* variants of the APIs are used. This is intended because since any thread can request access to any tag entry in the instance array, independently of the instance effectively being active or not and of permissions allowing the subsequent operations on it, there could always be some activity on an entry of the instance array. In the unfortunate (and unlikely, under normal usage) case that such activity is extensive and the call that tries to lock the rw_sems/mutexes blocks for too much time, it can be aborted with a signal.
    Since the semaphores are there to manage access to the internal state of the service and not really to wait indefinitely for events (like e.g. messages), and the *interruptible* variant for rw_semaphores is still quite [recent](https://www.spinics.net/lists/kernel/msg3759815.html), the *killable* variant is used almost everywhere. When these interruptions occur, the calls try to return to user space as soon as possible, releasing all the resources and memory they can in the process without taking any other lock. Note that each time an interruption can occur, the internal state of the system can never be left corrupted.
    **When such variants aren't used it's because other threads, by either terminating successfully or being killed, will inevitably release the locks, and then the last thread will successfully terminate without leaving an inconsistent module state.**
- Spinning locks, in the form of spinlocks, to guard status-critical data structures. Critical sections involving these have been kept as small and quick as possible, and are meant to be executed as soon as possible.

One last word about the ***condition structure* lock**: could we have used an *rwlock* there, instead of a spinlock? Sure, most of the time this is accessed by readers and a lock is really needed only to prevent a particularly bad race condition, so why the full exclusion? Because compared to spinlocks, especially when the critical section is short, rwlocks are [slow](https://www.kernel.org/doc/html/latest/locking/spinlocks.html#lesson-2-reader-writer-spinlocks), effectively slower than using a fully exclusive spinning lock. Considering that the critical sections that involve such structure are only made of one or two simple atomic operations, the choice has been in favor of spinlocks instead of rwlocks.

Finally, there are a few sections in which memory operations need to be performed in a particular order, or where we need to be sure that e.g. stores have been executed before proceeding to the next instruction. In those points, the desired ordering is enforced with appropriate memory fencing assembly instructions and compiler barriers.

In the rest of the section we will refer to four kinds of threads, depending on the kind of operation they are executing: *receivers*, *senders*, *removers*, *adders*, respectively those executing a *tag_receive*, *tag_send* (or *tag_ctl(AWAKE_ALL)*), *tag_ctl(REMOVE)*, *tag_get(TAG_CREATE)*. All threads except those that perform a *tag_get* check permissions immediately after accessing an instance.

## ACCESS TO AN INSTANCE, REMOVAL, ADDITION

Each entry in the instances array holds two rw_semaphores and a pointer. The first rw_sem is for receivers as readers, the second is for senders as readers, both are for removers and adders as writers. Keep in mind that senders, adders and removers always return, never deadlock, so their critical section time is always constant or at least finite.

When a remover comes, it trylocks the receivers one as writer, locks the senders one as writer, then flips the instance pointer to *NULL*, releases both locks and does its thing **afterwards** for the sake of speed. Note that the first trylock could mean either that the instance is really there but there's at least one reader in it, or that the instance isn't there because it is being removed or created, so nothing to do either way.
When a receiver comes it locks its semaphore as reader, checks the pointer, does its thing and unlocks its semaphore as reader.
When a sender comes it locks its semaphore as reader, checks the pointer, does its thing and unlocks the semaphore as reader.
This way, if an instance is being accessed by a thread that *could* block, according to the specification we do not try to remove it, but if it is being accessed by a thread that *won't* block, we shall wait a bit.

Things are a little bit different when adding an instance: at first, if the key is not in the BST, the bitmask is atomically checked for a free spot, then the adder thread locks both rw_sems as writer since being the pointer *NULL*, eventual readers/writers would almost immediately get out, then sets the pointer to that of a new instance struct. The BST is kept locked during this in order to avoid adding the same key possibly multiple times. Of course, if just a *tag_get(TAG_OPEN)* is requested, only the initial BST search step is performed.

Also, threads that come from the VFS while doing an *open* must synchronize with *adders* and *removers* to take a snapshot of each instance before it fades away. This can be accomplished by trylocking the senders rw_sem and checking the instance pointer, as will be explained later on.

## POSTING A MESSAGE ON A LEVEL

The algorithm described here is a variation of the algorithm used in RCU linked lists: while that one has to do with record deletion, this one has to manage wakeup conditions and message delivery.

The sender acquires the level senders mutex, posts a message in a dynamically-allocated buffer that is then linked in the instance structure, together with its size, updates the wakeup condition and wakes receivers up. Considering also the *copy_\** APIs, it won't be very quick, but it won't waste any memory and use only what is necessary at any given time. Also, as for RCU, only one sender is allowed to change the epoch at any given time and then free the message buffer after the grace period, but given the nature of this system a sleeping lock, i.e. a mutex, is used instead of a spinlock, to avoid unnecessary CPU contention. In both senders and receivers, memory operations like *copy_to/from_user* are performed only if the new message has a nonzero length, to speed things up for zero-length messages.

When the epoch selector in the *condition struct* gets flipped, that's a linearization point for the message buffer: all receivers that got in there before this will get the message, others were too late. The only difference is the need for a spinlock in the struct to avoid that the epoch selector gets flipped before a receiver can atomically increment the corresponding epoch presence counter: this is required here because if not, there could be some unlikely but dangerous race conditions that would lead to a receiver registering to an epoch that is *two times ahead* the one that it believes to be in, thus behaving incorrectly and skipping a message that it should get.

Senders wait for all receivers that got a condition value, i.e. they "busy-wait" on the "old" presence counter to become zero. This represents RCU's grace period. Since it is not advisable to implement in the kernel busy-wait loops that depend on updates from other threads (think about a single-core system...), while the counter isn't zero the scheduler is invoked, so in a single-core system the sender would voluntarily relinquish the CPU in favor of receivers. This has been proved to work in one of the testers, with acceptable performance.

For receivers, atomically reading the current condition value and then incrementing the presence counter is very important to sync with the state, avoid deadlocks and be waited for by the very next writer. They just register on an epoch and go to sleep, then get woken up, check what happened (a new message, a signal or an *AWAKE ALL*) and act accordingly, deregistering from their epoch when appropriate. Note that they always decrement their presence counter before terminating in any way, so the wait loop the sender is in will always get to an end.

Full instance wakeups work in a similar fashion. The only difference is that the wakeup is performed on both queues for each level since we can't know, nor should we care about, in which epoch each level is, thus in which queue each thread from the current instance-global epoch is found.

## MODULE LOCKING

A very simple module locking scheme is implemented in this project to ensure that syscalls do not end up operating on stale, inconsistent, not-anymore-present data (especially blocking ones), possibly causing kernel oopses or worse.
The *SCTH* module is a dependency, so it is locked upon insertion and released upon removal.
Then, in its wrapper, each system call does a *try_module_get(THIS_MODULE)* before attempting to execute its real code, and terminates with a *module_put*.
Note that, due to how the module locking feature is implemented in the Linux kernel, this still leaves room for some really impossible race conditions that would consist in a system call executing code that lies in a released memory region (the part before the _try\_module\_get_). This is the best that we can do. Causing the aforementioned condition during normal execution would require surgical scheduler precision, excellent timing, and a strong will to wreak havoc. We assume that a user knows when to remove the module, and do all that is possible to prevent damage anywhere we can.

# CHARACTER DEVICE DRIVER

The device driver included in this module has the only purpose of offering a quick way to instantly check the state of the AOS-TAG system.
Thus, a single device file named *aos_tag_status* is created in */dev* with read-only permissions for all users during the module's initialization routine: this is achieved with a series of calls that first involve the creation of a class in *sysfs* and then of a VFS node in */dev*. The module's cleanup routine removes everything in reverse.
The only three routines included in the driver are *open*, *read* and *release*, whilst *write* and (unlocked) *ioctl* are included just as nops that return *-EPERM*.

The idea behind this driver is to take a snapshot of the status of the system each time the device file is opened, and then return it to the user space code reading the file within subsequent calls to *read*. The snapshot is essentially a kernel buffer, allocated with *vmalloc* because of its potential size, which holds the status of the system in human-readable text form as explained before. The pointer to that buffer is stored in the *private_data* member of the *struct file* passed to *open*. This "fake text file" remains the same for the process that opened it until it gets closed, and is generated by *open* with a two-pass linear scan of the instances array:

- During the first pass, the data encoding the status of the system is read as quickly as possible by scanning the instances array entries, taking care to note which instances are active and how many threads are waiting on their levels by looking directly at conditions presence counters. This data is saved in an array of structures defined just for this purpose. In order to prevent removers from deleting an instance while *open* is accessing it, the senders rw_semaphore is trylocked: this is both because *open* will do its job really quickly (it doesn't acquire any other lock, just reads data) so a remover won't have to wait that much, and because locking the receivers one would cause a false positive for *tag_ctl(REMOVE)*, looking like a receiver was actually waiting on a level of that instance. Also, if the trylock fails it means the instance is being either created or removed, so the thread got there too early or late respectively.
- During the second pass, lines of text are printed following the defined style in a line buffer using *scnprintf*, and then copied in the file buffer using *memcpy*. At the end, the array holding the information gathered in the first pass is freed, and the text file buffer pointer is written in *private_data*.

Following calls to *read* will only access the text file buffer, determine the appropriate amount of data to copy from it to user space, and then advance the file offset accordingly until hitting *EOF*.
A call to *close* will then invoke the *release* function, which simply releases the buffer.

# TESTING

Test runs on this module have been carried out in two ways: *functional* and *performance* testing. The first set of testers had the goal to prove that each feature required in the specification actually worked, while the second one needed to investigate how efficiently this system could run.

Tests have been performed on a virtual machine running in Oracle VM VirtualBox with KVM support enabled. The guest system was an installation of Xubuntu 20.10 running kernel 5.8 on 8 cores out of 16 from the host system, and using up to 8 GB of RAM out of 16 from the host system. The host system's basic specs were:

- **CPU:** AMD Ryzen 7 3700X, 8c/16t, L1: 512 KB, L2: 4 MB, L3: 32 MB, up to 4.4 GHz of boost clock.
- **RAM:** G.Skill Trident Z Neo, DDR4, dual channel, 16 GB, 3600 MHz.
- **OS:** Arch Linux, kernel 5.11.

Each of the testers will now be briefly described, together with its results. Their code can be found in the *Tests/* subdirectory.

## syscalls_test.c

This simple tester can be used to check that the system calls work, and produce the correct results (or error codes) when invoked. In the current version, the calling thread is asked to read from an instance level, so SIGINT or a similar signal has to be sent to wake it up.

## talker.c & listener.c

These two programs allow us to try out the message system, echoing  a given message from a talker process to any number of listeners.

The talker requires a key to open a new instance, a level, a message string and a period in milliseconds that defines the message sending rate. A counter will be added to the message's contents, and of course zero is an allowed period.
The listener only requires a key and a level. It will print the message together with its size (in bytes).

The two processes will then behave as the specification requires until interrupted. When the writer is interrupted, an *AWAKE ALL* is called on the whole instance, which is then removed.

## functional_test.c

This tester was meant to try out a vast majority of features primarily regarding an instance creation and removal, message posting and error codes. Since almost all of this has been done with *talker* and *listener*, the only feature it tests is the possibility to create, and subsequently remove, all possible instances, checking the status device file in the meantime.

## deadlock_test.c

This tester proves that the message posting scheme works also in single-core systems.
A certain number of readers (currently 50) and a writer are initially spawned, with affinity settings that pin them all on a single CPU. The writer has to post a message and then terminate, the readers have to wait for a message and then exit. This is done a number of times (currently 3), with a sleep timer in the writer to ensure that all readers get back to wait after a message is delivered.
All threads rejoin the main thread, and the process terminates successfully.

## load_test.c

This tester was meant to investigate the performances of this system.
We have many factors at play, especially considering that this code runs in the kernel and must be called via a GATE.
Essentially, operations can be divided in two groups: those interacting with the BST and those handling messages.
About the first group, everything depends on the actual performance of the data structure employed, which is thoroughly described [here](https://www.cs.cmu.edu/~sleator/papers/self-adjusting.pdf). As already noted, the only two differences from the original implementation are the absence of the *splay* step in the search operation, and the join-based deletion scheme.
About the second group, much has already been told, however the only metric that has been valued in this project is **perceived user space latency** of a group of read or write operations on a single level of an instance. Considering the jitter induced by the OS scheduler negligible, latencies will be mainly influenced by the following facts:

- A group of writers, even when delivering to a single reader thread, has to synchronize on the same level by acquiring a mutex.
- Readers must wake from the wait queue before consuming the message.

This program takes in input a number of readers and one of writers to spawn, and the name of a file to log results in.
It then proceeds to create a new instance and runs two tests:

- In the first one, a large group of readers and a single writer are spawned. Readers go to sleep on a single level, then the writer posts a single empty message on it. The time that this operation takes is logged, in seconds, using the *clock* API.
- In the second one, a large group of writers is spawned. They all have to send an empty message on a single level. Given that no reader will be waiting on it, it'll be discarded, but in this test what we care about is the raw time necessary for a reader to access an instance on which there's contention. The time that this sequence of operations takes is logged as in the previous test.

Some example runs logs are included in the included TXT files.

