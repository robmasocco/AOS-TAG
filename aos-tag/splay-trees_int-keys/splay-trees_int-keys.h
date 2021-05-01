/**
 * This is free software.
 * You can redistribute it and/or modify this file under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 * 
 * This file is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this file; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
 */
/**
 * @brief Splay Tree data structure library header.
 *
 * @author Roberto Masocco <robmasocco@gmail.com>
 *
 * @date April 4, 2021
 */
/**
 * This file contains type definitions and declarations for the Splay Tree data
 * structure. See the source file for brief descriptions of what each function
 * does. Note that functions which names start with "_" are meant for internal
 * use only, and only those without it should be used by the actual programmer.
 * Many functions require dynamic memory allocation, and many
 * exposed methods require pointers or return some: see
 * the source file to understand what needs to be freed after use.
 */
/**
 * This code is a kernel-side rework of my repository splay-trees_c.
 * It lacks many unnecessary things and does others differently.
 * To avoid complaints from GCC, node data is forced to int to match our
 * use case.
 */

#ifndef _SPLAYTREES_INTEGERKEYS_H
#define _SPLAYTREES_INTEGERKEYS_H

typedef unsigned long int ulong;

/**
 * These options can be specified to tell the search functions what data to
 * return from the trees.
 * Only one at a time is allowed.
 */
#define SEARCH_DATA 0x4
#define SEARCH_NODES 0x10

/**
 * These options can be used to specify the desired kind of breadth-first
 * search. Only one at a time is allowed.
 */
#define BFS_LEFT_FIRST 0x100
#define BFS_RIGHT_FIRST 0x200

/**
 * x86 cache line size, in bytes.
 */
#define X86_CACHE_LINE_SZ 64

/**
 * A Splay Tree's node stores pointers to its "father" node and to its sons.
 * Since we're using the "splay" heuristic, no balance information is stored.
 * In this implementation, integers are used as keys in the dictionary.
 * NOTE: In this implementation, we try to align nodes to cache lines in order
 *       to optimize accesses using hot cache lines and the splay tree behaviour
 *       during searches.
 */
struct _splay_int_node {
    struct _splay_int_node *_father;
    struct _splay_int_node *_left_son;
    struct _splay_int_node *_right_son;
    int _key;
    int _data;
} __attribute__((aligned(X86_CACHE_LINE_SZ)));
typedef struct _splay_int_node SplayIntNode;

/**
 * A Splay Tree stores a pointer to its root node and a counter which keeps
 * track of the number of nodes in the structure, to get an idea of its "size"
 * and be able to efficiently perform searches.
 * Splay trees implemented like this have a size limit set by the maximum
 * amount representable with an unsigned long integer.
 * NOTE: In this implementation, we try to align trees to cache lines in order
 *       to optimize accesses during searches.
 */
typedef struct {
    SplayIntNode *_root;
    unsigned long int nodes_count;
    unsigned long int max_nodes;
} __attribute__((aligned(X86_CACHE_LINE_SZ))) SplayIntTree;

/* Library functions. */
SplayIntTree *create_splay_int_tree(void);
int delete_splay_int_tree(SplayIntTree *tree);
SplayIntNode *splay_int_search(SplayIntTree *tree, int key);
ulong splay_int_insert(SplayIntTree *tree, int new_key, int new_data);
int splay_int_delete(SplayIntTree *tree, int key);
void **splay_int_bfs(SplayIntTree *tree, int type, int opts);

#endif
