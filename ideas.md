## KERNEL-SIDE ARCHITECTURE
- Decentralized architecture: there's only one AVL tree to lookup active instances, register new ones, or delete
existing ones from: indexed by keys, each entry points to a data structure for that instance, shared by all
threads that open it. Max 256 nodes should be allowed.
- All threads keep a per-cpu structure for this module, with an array of pointers to instance structures, and a
bitmask for free positions that is kept updated. Works like VFS with file descriptors.
tag_get returns the index in this per_cpu array as per the bitmask, given the key, and fills the entry in
the array with the pointer to the shared data structure, also creating a node in the tree [key, pointer] for
future gets (skip this step if IPC_PRIVATE is specified).
- When REMOVE is invoked, each thread decrements the atomic counter and NULLifies the corresponding entry in its
per-cpu array, and the last one (i.e. counter = 0) also removes the node from the tree if the entry was shared,
or from the linked list if it was not shared.
- So yeah: in order to print the device file and instantly check the status of the system we need a linked list
of non-shared (IPC_PRIVATE) instance pointers.

## AVL TREE
- Represents key-instance associations.
- Is indexed by int keys.
- Each entry is a pointer to an instance structure.

## NONSHARED LINKED LIST
- Simple linked list of pointers to non-shared instances, for status logging purposes.
- Must be protected with a spinlock or a rwlock.

## QUEUEING ARCHITECTURE
- Maybe we must embed in the general structure an array of structures that represent each level, with members
that do what follows.
- Levels must be RCU, one linked list or something per level, all pointed by a fixed-size list in the control structure
for that instance.
- There must be a wait queue for each level.

## PER-CPU MODULE STRUCTURE
- Array of 256 instance pointers, indexed by "tag descriptor".
- Bitmask of free/used descriptors (consider fd_set?).

## INSTANCE DATA STRUCTURE
- Atomic usage count.
- Shared flag (i.e. "is also in the tree").
- Array of 32 level data structures.
- Creator EUID.

## LEVEL DATA STRUCTURE
- Wait queue.
- RCU list of messages.

## MODULE PARAMETERS
- System call numbers (one pseudofile each) (read-only).
- Max number of entries in the general sharing AVL tree (configurable at insertion).
- Length of the used instance array (configurable at insertion).

## TODOs
- Synchronization of everything, also thinking about the device file show method.
- System call module with exposed parameters (separate module?).
- Test "configurable at insertion"+"read-only" module parameters.

## EXTRAS
- You might want to use thread-specific desctructors to call a hidden system call and
force the removal of all instances when a thread exits: consider pthread_key_create embedded in tag_get somehow.
