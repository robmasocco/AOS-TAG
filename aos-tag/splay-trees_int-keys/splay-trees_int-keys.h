/*
 * @brief Splay Tree data structure library header.
 *
 * @author Roberto Masocco
 *
 * @date April 4, 2021
 */
/*
 * This file contains type definitions and declarations for the Splay Tree data
 * structure. See the source file for brief descriptions of what each function
 * does. Note that functions which names start with "_" are meant for internal
 * use only, and only those without it should be used by the actual programmer.
 * Many functions require dynamic memory allocation in the heap, and many
 * exposed methods require pointers or return some which refer to the heap: see
 * the source file to understand what needs to be freed after use.
 */
/* 
 * This code is released under the MIT license.
 * See the attached LICENSE file.
 */

#ifndef _SPLAYTREES_INTEGERKEYS_H
#define _SPLAYTREES_INTEGERKEYS_H

typedef unsigned long int ulong;

/* 
 * These options can be OR'd in a call to the delete functions to specify
 * if also the keys and/or the data in the nodes must be freed in the heap.
 * If nothing is specified, only the nodes are freed.
 */
#define DELETE_FREE_DATA 0x1

/*
 * These options can be specified to tell the search functions what data to
 * return from the trees.
 * Only one at a time is allowed.
 */
#define SEARCH_DATA 0x4
#define SEARCH_KEYS 0x8
#define SEARCH_NODES 0x10

/*
 * This option can be used to perform the splaying operation on the target node
 * during a search. For performance purposes, this behaviour is configurable,
 * since it can prevent concurrent accesses in multithreaded scenarios.
 * If set, the amortized analysis result applies and the amortized time for all
 * types of operations seen in a sequence is logarithmic in the max number of
 * nodes the structure reaches in a sequence, but searches must be performed
 * atomically (and locking of the structure is not dealt with here).
 * If not set, searches have a linear worst-case execution time, but can be
 * perfomed concurrently by multiple threads.
 * Can be OR'd with the previous ones.
 */
#define SEARCH_SPLAY 0x2

/*
 * These options can be used to specify the desired kind of depth-first search.
 * Only one at a time is allowed.
 */
#define DFS_PRE_ORDER 0x20
#define DFS_IN_ORDER 0x40
#define DFS_POST_ORDER 0x80

/*
 * These options can be used to specify the desired kind of breadth-first
 * search. Only one at a time is allowed.
 */
#define BFS_LEFT_FIRST 0x100
#define BFS_RIGHT_FIRST 0x200

/* 
 * A Splay Tree's node stores pointers to its "father" node and to its sons.
 * Since we're using the "splay" heuristic, no balance information is stored.
 * In this implementation, integers are used as keys in the dictionary.
 * The data kept inside the node can be everything, as long as it's at most
 * sizeof(void *)-wide. Could be e.g. pointers.
 * Note that, as per the deletion options, is not possible to have only SOME
 * data in the heap: either all or none, so think about the data you're
 * providing to these functions.
 */
typedef struct _splay_int_node {
    struct _splay_int_node *_father;
    struct _splay_int_node *_left_son;
    struct _splay_int_node *_right_son;
    int _key;
    void *_data;
} SplayIntNode;

/*
 * A Splay Tree stores a pointer to its root node and a counter which keeps
 * track of the number of nodes in the structure, to get an idea of its "size"
 * and be able to efficiently perform searches.
 * Splay trees implemented like this have a size limit set by the maximum
 * amount representable with an unsigned long integer, automatically set (as
 * long as you compile this code on the same machine you're going to use it on).
 */
typedef struct {
    SplayIntNode *_root;
    unsigned long int nodes_count;
    unsigned long int max_nodes;
} SplayIntTree;

/* Library functions. */
SplayIntTree *create_splay_int_tree(void);
int delete_splay_int_tree(SplayIntTree *tree, int opts);
void *splay_int_search(SplayIntTree *tree, int key, int opts);
ulong splay_int_insert(SplayIntTree *tree, int new_key, void *new_data);
int splay_int_delete(SplayIntTree *tree, int key, int opts);
void **splay_int_dfs(SplayIntTree *tree, int type, int opts);
void **splay_int_bfs(SplayIntTree *tree, int type, int opts);

#endif
