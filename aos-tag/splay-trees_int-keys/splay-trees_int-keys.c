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
 * @brief Splay Tree data structure library source code.
 *
 * @author Roberto Masocco
 *
 * @date April 4, 2021
 */
/**
 * This code is a kernel-side rework of my repository splay-trees_c.
 * It lacks many unnecessary things and does others differently.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <limits.h>
#include "splay-trees_int-keys/splay-trees_int-keys.h"

/* Internal library subroutines declarations. */
SplayIntNode *_spli_create_node(int new_key, void *new_data);
void _spli_delete_node(SplayIntNode *node);
SplayIntNode *_spli_search_node(SplayIntTree *tree, int key);
void _spli_insert_left_subtree(SplayIntNode *father, SplayIntNode *new_son);
void _spli_insert_right_subtree(SplayIntNode *father, SplayIntNode *new_son);
SplayIntNode *_spli_cut_left_subtree(SplayIntNode *father);
SplayIntNode *_spli_cut_right_subtree(SplayIntNode *father);
SplayIntNode *_spli_max_key_son(SplayIntNode *node);
void _spli_swap_info(SplayIntNode *node1, SplayIntNode *node2);
void _spli_right_rotation(SplayIntNode *node);
void _spli_left_rotation(SplayIntNode *node);
SplayIntNode *_spli_splay(SplayIntNode *node);
SplayIntNode *_spli_join(SplayIntNode *left_root, SplayIntNode *right_root);

// LIBRARY FUNCTIONS //
/**
 * Creates a new Splay Tree.
 *
 * @return Pointer to the newly created tree, NULL if allocation failed.
 */
SplayIntTree *create_splay_int_tree(void) {
    SplayIntTree *new_tree;
    new_tree = (SplayIntTree *)kmalloc(sizeof(SplayIntTree), GFP_KERNEL);
    if (new_tree == NULL) return NULL;
    new_tree->_root = NULL;
    new_tree->nodes_count = 0;
    new_tree->max_nodes = ULONG_MAX;
    return new_tree;
}

/**
 * Frees a given Splay Tree. Using options defined in the header, 
 * it's possible to specify whether also data has to be freed or not.
 *
 * @param tree Pointer to the tree to free.
 * @param opts Options to configure the deletion behaviour (see header).
 * @return 0 if all went well, or -1 if input args were bad.
 */
int delete_splay_int_tree(SplayIntTree *tree, int opts) {
    // Sanity check on input arguments.
    if ((tree == NULL) || (opts < 0)) return -1;
    // If the tree is empty free it directly.
    if (tree->_root == NULL) {
        kfree(tree);
        return 0;
    }
    // Do a BFS to get all the nodes (less taxing on memory than a DFS).
    SplayIntNode **nodes;
    nodes = (SplayIntNode **)splay_int_bfs(tree, BFS_LEFT_FIRST, SEARCH_NODES);
    // Free the nodes and eventually their data.
    unsigned long int i;
    for (i = 0; i < tree->nodes_count; i++) {
        if (opts & DELETE_FREE_DATA) kfree((*(nodes[i]))._data);
        _spli_delete_node(nodes[i]);
    }
    // Free the nodes array and the tree, and that's it!
    kfree(nodes);
    kfree(tree);
    return 0;
}

/**
 * Searches for an entry with the specified key in the tree.
 *
 * @param tree Tree to search into.
 * @param key Key to look for.
 * @param opts Configures the behaviour of the search operation (see header).
 * @return Data stored in a node (if any) or pointer to the node (if any).
 */
void *splay_int_search(SplayIntTree *tree, int key, int opts) {
    if ((opts <= 0) || (tree == NULL)) return NULL;  // Sanity check.
    SplayIntNode *searched_node;
    searched_node = _spli_search_node(tree, key);
    if (searched_node == NULL) return NULL;
    if (opts & SEARCH_DATA) return searched_node->_data;
    if (opts & SEARCH_NODES) return (void *)searched_node;
    return NULL;
}

/**
 * Deletes an entry from the tree.
 *
 * @param tree Pointer to the tree to delete from.
 * @param key Key to delete from the dictionary.
 * @param opts Also willing to free the stored data?
 * @return 1 if found and deleted, 0 if not found or input args were bad.
 */
int splay_int_delete(SplayIntTree *tree, int key, int opts) {
    // Sanity check on input arguments.
    if ((opts < 0) || (tree == NULL)) return 0;
    SplayIntNode *to_delete;
    to_delete = _spli_search_node(tree, key);
    if (to_delete != NULL) {
        // Splay the target node. Follow the content swaps!
        while (tree->_root != to_delete)
            to_delete = _spli_splay(to_delete);
        // Remove the new root from the tree, then join the two subtrees.
        SplayIntNode *left_sub, *right_sub;
        left_sub = _spli_cut_left_subtree(to_delete);
        right_sub = _spli_cut_right_subtree(to_delete);
        tree->_root = _spli_join(left_sub, right_sub);
        // Apply eventual options to free keys and data, then free the node.
        if (opts & DELETE_FREE_DATA) kfree(to_delete->_data);
        kfree(to_delete);
        tree->nodes_count--;
        return 1;  // Found and deleted.
    }
    return 0;  // Not found.
}

/**
 * Creates and inserts a new node in the tree.
 *
 * @param tree Pointer to the tree to insert into.
 * @param new_key New key to add to the dictionary.
 * @param new_data New data to store into the dictionary.
 * @return Internal nodes counter after the insertion, or 0 if full/bad args.
 */
ulong splay_int_insert(SplayIntTree *tree, int new_key, void *new_data) {
    if (tree == NULL) return 0;  // Sanity check.
    if (tree->nodes_count == tree->max_nodes) return 0;  // The tree is full.
    SplayIntNode *new_node;
    new_node = _spli_create_node(new_key, new_data);
    if (tree->_root == NULL) {
        // The tree is empty.
        tree->_root = new_node;
        tree->nodes_count++;
    } else {
        // Look for the correct position and place it there.
        SplayIntNode *curr = tree->_root;
        SplayIntNode *pred = NULL;
        int comp;
        while (curr != NULL) {
            pred = curr;
            comp = curr->_key - new_key;
            // Equals are kept in the left subtree.
            if (comp >= 0) curr = curr->_left_son;
            else curr = curr->_right_son;
        }
        comp = pred->_key - new_key;
        if (comp >= 0) _spli_insert_left_subtree(pred, new_node);
        else _spli_insert_right_subtree(pred, new_node);
        // Splay the new node.
        curr = new_node;
        while (tree->_root != curr)
            curr = _spli_splay(curr);
        tree->nodes_count++;
    }
    return tree->nodes_count;  // Return the result of the insertion.
}

/**
 * Performs a breadth-first search of the tree, the type of which can be 
 * specified using the options defined in the header (left or right son 
 * visited first). 
 * Depending on the option specified, returns an array of: 
 * - Pointers to the nodes. 
 * - Keys. 
 * - Data. 
 * See the header for the definitions of such options. 
 * Remember to free the returned array afterwards! 
 * NOTE: In this work, we'll use this function only to delete the whole tree, 
 *       so some stuff from the original implementation is missing.
 * 
 * @param tree Pointer to the tree to operate on.
 * @param type Type of BFS to perform (see header).
 * @param opts Type of data to return (see header).
 * @return Pointer to an array with the result of the search correctly ordered.
 */
void **splay_int_bfs(SplayIntTree *tree, int type, int opts) {
    // Sanity check on input arguments.
    if ((tree == NULL) || (tree->_root == NULL)) return NULL;
    // Allocate memory.
    void **bfs_res = NULL;
    void **int_ptr;
    if (opts & SEARCH_DATA) {
        bfs_res = kzalloc((tree->nodes_count) * sizeof(void *), GFP_KERNEL);
    } else if (opts & SEARCH_NODES) {
        bfs_res = kzalloc((tree->nodes_count) * sizeof(SplayIntNode *),
                           GFP_KERNEL);
    } else return NULL;  // Invalid option.
    if (bfs_res == NULL) return NULL;
    int_ptr = bfs_res + 1;
    *bfs_res = (void *)(tree->_root);
    SplayIntNode *curr;
    // Start the visit, using the same array to return as a temporary queue
    // for the nodes.
    unsigned long int i;
    for (i = 0; i < tree->nodes_count; i++) {
        curr = (SplayIntNode *)bfs_res[i];
        // Visit the current node.
        if (opts & SEARCH_DATA) bfs_res[i] = curr->_data;
        if (opts & SEARCH_NODES) bfs_res[i] = curr;
        // Eventually add the sons to the array, to be visited afterwards.
        if (type & BFS_LEFT_FIRST) {
            if (curr->_left_son != NULL) {
                *int_ptr = (void *)(curr->_left_son);
                int_ptr++;
            }
            if (curr->_right_son != NULL) {
                *int_ptr = (void *)(curr->_right_son);
                int_ptr++;
            }
        }
        if (type & BFS_RIGHT_FIRST) {
            if (curr->_right_son != NULL) {
                *int_ptr = (void *)(curr->_right_son);
                int_ptr++;
            }
            if (curr->_left_son != NULL) {
                *int_ptr = (void *)(curr->_left_son);
                int_ptr++;
            }
        }
    }
    return bfs_res;
}

// INTERNAL LIBRARY SUBROUTINES //
/**
 * Creates a new node. Requires an integer key and some data.
 *
 * @param new_key Key to add.
 * @param new_data Data to add.
 * @return Pointer to a new node, or NULL if allocation failed.
 */
SplayIntNode *_spli_create_node(int new_key, void *new_data) {
    SplayIntNode *new_node;
    new_node = (SplayIntNode *)kmalloc(sizeof(SplayIntNode), GFP_KERNEL);
    if (new_node == NULL) return NULL;
    new_node->_father = NULL;
    new_node->_left_son = NULL;
    new_node->_right_son = NULL;
    new_node->_key = new_key;
    new_node->_data = new_data;
    return new_node;
}

/**
 * Frees memory occupied by a node.
 *
 * @param node Node to release.
 */
void _spli_delete_node(SplayIntNode *node) {
    kfree(node);
}

/**
 * Inserts a subtree rooted in a given node as the left subtree of a given 
 * node.
 *
 * @param father Pointer to the node to root the subtree onto.
 * @param new_son Root of the subtree to add.
 */
void _spli_insert_left_subtree(SplayIntNode *father, SplayIntNode *new_son) {
    if (new_son != NULL) new_son->_father = father;
    father->_left_son = new_son;
}

/**
 * Inserts a subtree rooted in a given node as the right subtree of a given 
 * node.
 *
 * @param father Pointer to the node to root the subtree onto.
 * @param new_son Root of the subtree to add.
 */
void _spli_insert_right_subtree(SplayIntNode *father, SplayIntNode *new_son) {
    if (new_son != NULL) new_son->_father = father;
    father->_right_son = new_son;
}

/**
 * Cuts and returns the left subtree of a given node.
 *
 * @param father Node to cut the subtree at.
 * @return Pointer to the cut subtree's root.
 */
SplayIntNode *_spli_cut_left_subtree(SplayIntNode *father) {
    SplayIntNode *son = father->_left_son;
    if (son == NULL) return NULL;  // Sanity check.
    son->_father = NULL;
    father->_left_son = NULL;
    return son;
}

/**
 * Cuts and returns the right subtree of a given node.
 *
 * @param father Node to cut the subtree at.
 * @return Pointer to the cut subtree's root.
 */
SplayIntNode *_spli_cut_right_subtree(SplayIntNode *father) {
    SplayIntNode *son = father->_right_son;
    if (son == NULL) return NULL;  // Sanity check.
    son->_father = NULL;
    father->_right_son = NULL;
    return son;
}

/**
 * Returns the descendant of a given node with the greatest key.
 *
 * @param node Node for which to look for the descendant.
 * @return Pointer to the descendant node.
 */
SplayIntNode *_spli_max_key_son(SplayIntNode *node) {
    SplayIntNode *curr = node;
    while (curr->_right_son != NULL) curr = curr->_right_son;
    return curr;
}

/**
 * Returns a pointer to the node with the specified key, or NULL.
 *
 * @param tree Pointer to the tree to look into.
 * @param key Key to look for.
 * @return Pointer to the target node, or NULL if none or input args were bad.
 */
SplayIntNode *_spli_search_node(SplayIntTree *tree, int key) {
    if (tree->_root == NULL) return NULL;
    SplayIntNode *curr = tree->_root;
    int comp;
    while (curr != NULL) {
        comp = curr->_key - key;
        if (comp > 0) {
            curr = curr->_left_son;
        } else if (comp < 0) {
            curr = curr->_right_son;
        } else return curr;
    }
    return NULL;
}

/**
 * Swaps contents between two nodes.
 *
 * @param node1 First node.
 * @param node2 Second node.
 */
void _spli_swap_info(SplayIntNode *node1, SplayIntNode *node2) {
    int key1 = node1->_key;
    void *data1 = node1->_data;
    int key2 = node2->_key;
    void *data2 = node2->_data;
    node1->_key = key2;
    node2->_key = key1;
    node1->_data = data2;
    node2->_data = data1;
}

/**
 * Performs a simple right rotation at the specified node.
 *
 * @param node Node to rotate onto.
 */
void _spli_right_rotation(SplayIntNode *node) {
    SplayIntNode *left_son = node->_left_son;
    // Swap the node and its son's contents to make it climb.
    _spli_swap_info(node, left_son);
    // Shrink the tree portion in subtrees.
    SplayIntNode *r_tree, *l_tree, *l_tree_l, *l_tree_r;
    r_tree = _spli_cut_right_subtree(node);
    l_tree = _spli_cut_left_subtree(node);
    l_tree_l = _spli_cut_left_subtree(left_son);
    l_tree_r = _spli_cut_right_subtree(left_son);
    // Recombine portions to respect the search property.
    _spli_insert_right_subtree(l_tree, r_tree);
    _spli_insert_left_subtree(l_tree, l_tree_r);
    _spli_insert_right_subtree(node, l_tree);
    _spli_insert_left_subtree(node, l_tree_l);
}

/**
 * Performs a simple left rotation at the specified node.
 *
 * @param node Node to rotate onto.
 */
void _spli_left_rotation(SplayIntNode *node) {
    SplayIntNode *right_son = node->_right_son;
    // Swap the node and its son's contents to make it climb.
    _spli_swap_info(node, right_son);
    // Shrink the tree portion in subtrees.
    SplayIntNode *r_tree, *l_tree, *r_tree_l, *r_tree_r;
    r_tree = _spli_cut_right_subtree(node);
    l_tree = _spli_cut_left_subtree(node);
    r_tree_l = _spli_cut_left_subtree(right_son);
    r_tree_r = _spli_cut_right_subtree(right_son);
    // Recombine portions to respect the search property.
    _spli_insert_left_subtree(r_tree, l_tree);
    _spli_insert_right_subtree(r_tree, r_tree_l);
    _spli_insert_left_subtree(node, r_tree);
    _spli_insert_right_subtree(node, r_tree_r);
}

/**
 * Performs a single splay step onto a given node. 
 * Note that in order to fully splay a node, this has to be called until a 
 * node becomes the tree's root.
 *
 * @param node Node to splay.
 * @return Pointer to the splayed node as it climbs up (content swaps!).
 */
SplayIntNode *_spli_splay(SplayIntNode *node) {
    // Consistency checks.
    if (node == NULL) return NULL;
    if (node->_father == NULL) return node;  // Nothing to do.
    SplayIntNode *father_node = node->_father;
    SplayIntNode *grand_node = father_node->_father;
    SplayIntNode *new_curr_node;
    if (grand_node == NULL) {
        // Case 1: Father is the root. Rotate to climb accordingly.
        if (father_node->_left_son == node) _spli_right_rotation(father_node);
        else _spli_left_rotation(father_node);
        // The node always takes its father's place.
        new_curr_node = father_node;
    } else {
        // Notice how only one of these is possible.
        if ((father_node->_left_son == node) &&
            (grand_node->_left_son == father_node)) {
            // Case 2: Both nodes are left sons.
            // Perform two right rotations. Watch out for content swaps!
            _spli_right_rotation(grand_node);
            _spli_right_rotation(grand_node);
        }
        if ((father_node->_right_son == node) &&
            (grand_node->_right_son == father_node)) {
            // Case 3: Both nodes are right sons. Watch out for content swaps!
            // Perform two left rotations.
            _spli_left_rotation(grand_node);
            _spli_left_rotation(grand_node);
        }
        if ((father_node->_left_son == node) &&
            (grand_node->_right_son == father_node)) {
            // Case 4: Father is right son while this is a left son.
            // Perform two rotations, on the father and on the grand node.
            _spli_right_rotation(father_node);
            _spli_left_rotation(grand_node);
        }
        if ((father_node->_right_son == node) &&
            (grand_node->_left_son == father_node)) {
            // Case 5: Father is left son while this is a right son.
            // Perform two rotations, on the father and on the grand node.
            _spli_left_rotation(father_node);
            _spli_right_rotation(grand_node);
        }
        // The node always takes its grand's place.
        new_curr_node = grand_node;
    }
    return new_curr_node;
}

/**
 * Upon deletion, joins two subtrees and returns the new root. 
 *
 * @param left_root Pointer to the root node of the left subtree.
 * @param right_root Pointer to the root node of the right subtree.
 * @return Pointer to the new root node.
 */
SplayIntNode *_spli_join(SplayIntNode *left_root, SplayIntNode *right_root) {
    // Easy cases: one or both subtrees are missing.
    if ((left_root == NULL) && (right_root == NULL)) return NULL;
    if (left_root == NULL) return right_root;
    if (right_root == NULL) return left_root;
    // Not-so-easy case: splay the largest-key node in the left subtree and
    // then join the right as right subtree.
    SplayIntNode *left_max;
    left_max = _spli_max_key_son(left_root);
    while (left_root != left_max)
        left_max = _spli_splay(left_max);
    _spli_insert_right_subtree(left_root, right_root);
    return left_root;
}
