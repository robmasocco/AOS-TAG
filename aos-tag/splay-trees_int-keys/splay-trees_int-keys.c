/*
 * @brief Splay Tree data structure library source code.
 *
 * @author Roberto Masocco
 *
 * @date April 4, 2021
 */
/* 
 * This code is released under the MIT license.
 * See the attached LICENSE file.
 */

#include <stdlib.h>
#include <limits.h>
#include "splay-trees_int-keys.h"

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
void _spli_inodfs(SplayIntNode *root_node, void ***int_ptr, int int_opt);
void _spli_preodfs(SplayIntNode *root_node, void ***int_ptr, int int_opt);
void _spli_postodfs(SplayIntNode *root_node, void ***int_ptr, int int_opt);

// USER FUNCTIONS //
/*
 * Creates a new AVL Tree in the heap.
 *
 * @return Pointer to the newly created tree, NULL if allocation failed.
 */
SplayIntTree *create_splay_int_tree(void) {
    SplayIntTree *new_tree = (SplayIntTree *)malloc(sizeof(SplayIntTree));
    if (new_tree == NULL) return NULL;
    new_tree->_root = NULL;
    new_tree->nodes_count = 0;
    new_tree->max_nodes = ULONG_MAX;
    return new_tree;
}

/* 
 * Frees a given AVL Tree from the heap. Using options defined in the header,
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
        free(tree);
        return 0;
    }
    // Do a BFS to get all the nodes (less taxing on memory than a DFS).
    SplayIntNode **nodes =
        (SplayIntNode **)splay_int_bfs(tree, BFS_LEFT_FIRST, SEARCH_NODES);
    // Free the nodes and eventually their data.
    for (unsigned long int i = 0; i < tree->nodes_count; i++) {
        if (opts & DELETE_FREE_DATA) free((*(nodes[i]))._data);
        _spli_delete_node(nodes[i]);
    }
    // Free the nodes array and the tree, and that's it!
    free(nodes);
    free(tree);
    return 0;
}

/*
 * Searches for an entry with the specified key in the tree.
 *
 * @param tree Tree to search into.
 * @param key Key to look for.
 * @param opts Configures the behaviour of the search operation (see header).
 * @return Data stored in a node (if any) or pointer to the node (if any).
 */
void *splay_int_search(SplayIntTree *tree, int key, int opts) {
    if ((opts <= 0) || (tree == NULL)) return NULL;  // Sanity check.
    SplayIntNode *searched_node = _spli_search_node(tree, key);
    if (searched_node == NULL) return NULL;
    // Splay the searched node.
    if (opts & SEARCH_SPLAY)
        while (tree->_root != searched_node)
            searched_node = _spli_splay(searched_node);
    if (opts & SEARCH_DATA) return searched_node->_data;
    if (opts & SEARCH_NODES) return (void *)searched_node;
    return NULL;
}

/*
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
    SplayIntNode *to_delete = _spli_search_node(tree, key);
    if (to_delete != NULL) {
        // Splay the target node. Follow the content swaps!
        while (tree->_root != to_delete)
            to_delete = _spli_splay(to_delete);
        // Remove the new root from the tree, then join the two subtrees.
        SplayIntNode *left_sub = _spli_cut_left_subtree(to_delete);
        SplayIntNode *right_sub = _spli_cut_right_subtree(to_delete);
        tree->_root = _spli_join(left_sub, right_sub);
        // Apply eventual options to free keys and data, then free the node.
        if (opts & DELETE_FREE_DATA) free(to_delete->_data);
        free(to_delete);
        tree->nodes_count--;
        return 1;  // Found and deleted.
    }
    return 0;  // Not found.
}

/*
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
    SplayIntNode *new_node = _spli_create_node(new_key, new_data);
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

/* 
 * Performs a depth-first search of the tree, the type of which can be
 * specified using the options defined in the header.
 * Depending on the option specified, returns an array of:
 * - Pointers to the nodes.
 * - Keys.
 * - Data.
 * See the header for the definitions of such options.
 * Remember to free the returned array afterwards!
 *
 * @param tree Pointer to the tree to operate on.
 * @param type Type of DFS to perform (see header).
 * @param opts Type of data to return (see header).
 * @return Pointer to an array with the result of the search correctly ordered.
 */
void **splay_int_dfs(SplayIntTree *tree, int type, int opts) {
    // Sanity check for the input arguments.
    if ((type <= 0) || (opts <= 0)) return NULL;
    if ((tree == NULL) || (tree->_root == NULL)) return NULL;
    // Allocate memory according to options.
    void **dfs_res;
    int int_opt;
    if (opts & SEARCH_DATA) {
        int_opt = SEARCH_DATA;
        dfs_res = calloc(tree->nodes_count, sizeof(void *));
    } else if (opts & SEARCH_KEYS) {
        int_opt = SEARCH_KEYS;
        dfs_res = calloc(tree->nodes_count, sizeof(int));
    } else if (opts & SEARCH_NODES) {
        int_opt = SEARCH_NODES;
        dfs_res = calloc(tree->nodes_count, sizeof(SplayIntNode *));
    } else return NULL;  // Invalid option.
    if (dfs_res == NULL) return NULL;  // calloc failed.
    // Launch the requested DFS according to type.
    void **int_ptr = dfs_res;
    if (type & DFS_PRE_ORDER) {
        _spli_preodfs(tree->_root, &int_ptr, int_opt);
    } else if (type & DFS_IN_ORDER) {
        _spli_inodfs(tree->_root, &int_ptr, int_opt);
    } else if (type & DFS_POST_ORDER) {
        _spli_postodfs(tree->_root, &int_ptr, int_opt);
    } else {
        // Invalid type.
        free(dfs_res);
        return NULL;
    }
    // The array is now filled with the requested data.
    return dfs_res;
}

/*
 * Performs a breadth-first search of the tree, the type of which can be
 * specified using the options defined in the header (left or right son
 * visited first).
 * Depending on the option specified, returns an array of:
 * - Pointers to the nodes.
 * - Keys.
 * - Data.
 * See the header for the definitions of such options.
 * Remember to free the returned array afterwards!
 * 
 * @param tree Pointer to the tree to operate on.
 * @param type Type of BFS to perform (see header).
 * @param opts Type of data to return (see header).
 * @return Pointer to an array with the result of the search correctly ordered.
 */
void **splay_int_bfs(SplayIntTree *tree, int type, int opts) {
    // Sanity check on input arguments.
    if ((tree == NULL) || (tree->_root == NULL) ||
        (type <= 0) || (opts <= 0) ||
        !((type & BFS_LEFT_FIRST) || (type & BFS_RIGHT_FIRST)) ||
        !((opts & SEARCH_KEYS) || (opts & SEARCH_DATA) ||
        (opts & SEARCH_NODES))) return NULL;
    // Allocate memory in the heap.
    void **bfs_res = NULL;
    void **int_ptr;
    int *key_ptr;  // Used only if keys are searched.
    if (opts & SEARCH_DATA) {
        bfs_res = calloc(tree->nodes_count, sizeof(void *));
    } else if (opts & SEARCH_KEYS) {
        bfs_res = calloc(tree->nodes_count, sizeof(SplayIntNode *));
        key_ptr = (int *)bfs_res;
    } else if (opts & SEARCH_NODES) {
        bfs_res = calloc(tree->nodes_count, sizeof(SplayIntNode *));
    } else return NULL;  // Invalid option.
    if (bfs_res == NULL) return NULL;  // Calloc failed.
    int_ptr = bfs_res + 1;
    *bfs_res = (void *)(tree->_root);
    SplayIntNode *curr;
    // Start the visit, using the same array to return as a temporary queue
    // for the nodes.
    for (unsigned long int i = 0; i < tree->nodes_count; i++) {
        curr = (SplayIntNode *)bfs_res[i];
        // Visit the current node.
        if (opts & SEARCH_DATA) {
            bfs_res[i] = curr->_data;
        } else if (opts & SEARCH_KEYS) {
            *key_ptr = curr->_key;
            key_ptr++;
        } else if (opts & SEARCH_NODES) {
            bfs_res[i] = curr;
        }
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
        } else if (type & BFS_RIGHT_FIRST) {
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
    if (opts & SEARCH_KEYS) {
        // If keys were searched, part of the array (half of it on x86_64)
        // is totally unneeded, so we can release it.
        // reallocarray is used instead of realloc to account for possible size
        // computation overflows (see man).
        if ((bfs_res = reallocarray(bfs_res, (size_t)(tree->nodes_count),
                sizeof(int))) == NULL) {
            free(bfs_res);
            return NULL;
        }
    }
    return bfs_res;
}

// INTERNAL LIBRARY SUBROUTINES //
/*
 * Creates a new node in the heap. Requires an integer key and some data.
 *
 * @param new_key Key to add.
 * @param new_data Data to add.
 * @return Pointer to a new node, or NULL if allocation failed.
 */
SplayIntNode *_spli_create_node(int new_key, void *new_data) {
    SplayIntNode *new_node = (SplayIntNode *)malloc(sizeof(SplayIntNode));
    if (new_node == NULL) return NULL;
    new_node->_father = NULL;
    new_node->_left_son = NULL;
    new_node->_right_son = NULL;
    new_node->_key = new_key;
    new_node->_data = new_data;
    return new_node;
}

/*
 * Frees memory occupied by a node.
 *
 * @param node Node to release.
 */
void _spli_delete_node(SplayIntNode *node) {
    free(node);
}

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
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

/*
 * Performs a simple right rotation at the specified node.
 *
 * @param node Node to rotate onto.
 */
void _spli_right_rotation(SplayIntNode *node) {
    SplayIntNode *left_son = node->_left_son;
    // Swap the node and its son's contents to make it climb.
    _spli_swap_info(node, left_son);
    // Shrink the tree portion in subtrees.
    SplayIntNode *r_tree = _spli_cut_right_subtree(node);
    SplayIntNode *l_tree = _spli_cut_left_subtree(node);
    SplayIntNode *l_tree_l = _spli_cut_left_subtree(left_son);
    SplayIntNode *l_tree_r = _spli_cut_right_subtree(left_son);
    // Recombine portions to respect the search property.
    _spli_insert_right_subtree(l_tree, r_tree);
    _spli_insert_left_subtree(l_tree, l_tree_r);
    _spli_insert_right_subtree(node, l_tree);
    _spli_insert_left_subtree(node, l_tree_l);
}

/*
 * Performs a simple left rotation at the specified node.
 *
 * @param node Node to rotate onto.
 */
void _spli_left_rotation(SplayIntNode *node) {
    SplayIntNode *right_son = node->_right_son;
    // Swap the node and its son's contents to make it climb.
    _spli_swap_info(node, right_son);
    // Shrink the tree portion in subtrees.
    SplayIntNode *r_tree = _spli_cut_right_subtree(node);
    SplayIntNode *l_tree = _spli_cut_left_subtree(node);
    SplayIntNode *r_tree_l = _spli_cut_left_subtree(right_son);
    SplayIntNode *r_tree_r = _spli_cut_right_subtree(right_son);
    // Recombine portions to respect the search property.
    _spli_insert_left_subtree(r_tree, l_tree);
    _spli_insert_right_subtree(r_tree, r_tree_l);
    _spli_insert_left_subtree(node, r_tree);
    _spli_insert_right_subtree(node, r_tree_r);
}

/*
 * Performs a single splay step onto a given node.
 * Note that in order to fully splay a node, this has to be called until a node
 * becomes the tree's root.
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

/*
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
    SplayIntNode *left_max = _spli_max_key_son(left_root);
    while (left_root != left_max)
        left_max = _spli_splay(left_max);
    _spli_insert_right_subtree(left_root, right_root);
    return left_root;
}

/*
 * Performs an in-order, recursive DFS.
 *
 * @param root_node Root of the subtree to start the search onto.
 * @param int_ptr Internal pointer to a pointer to the return array.
 * @param int_opt Internal options passed value.
 */
void _spli_inodfs(SplayIntNode *root_node, void ***int_ptr, int int_opt) {
    // Recursion base step.
    if (root_node == NULL) {
        // Correctly update the pointer.
        if (int_opt & SEARCH_KEYS) {
            *(int_ptr) = (void **)((int *)*(int_ptr) - 1);
        } else *(int_ptr) = *(int_ptr) - 1;
        return;
    }
    // Recursive step: visit the left son.
    _spli_inodfs(root_node->_left_son, int_ptr, int_opt);
    // Correctly increment the internal pointer.
    if (int_opt & SEARCH_KEYS) {
        *(int_ptr) = (void **)((int *)*(int_ptr) + 1);
    } else *(int_ptr) = *(int_ptr) + 1;
    // Now visit the root node.
    if (int_opt & SEARCH_NODES) {
        **(int_ptr) = root_node;
    } else if (int_opt & SEARCH_KEYS) {
        *(int *)*(int_ptr) = root_node->_key;
    } else if (int_opt & SEARCH_DATA) {
        **(int_ptr) = root_node->_data;
    }
    // Correctly increment the internal pointer.
    if (int_opt & SEARCH_KEYS) {
        *(int_ptr) = (void **)((int *)*(int_ptr) + 1);
    } else *(int_ptr) = *(int_ptr) + 1;
    // Visit the right son and return.
    _spli_inodfs(root_node->_right_son, int_ptr, int_opt);
}

/*
 * Performs a pre-order, recursive DFS.
 *
 * @param root_node Root of the subtree to start the search onto.
 * @param int_ptr Internal pointer to a pointer to the return array.
 * @param int_opt Internal options passed value.
 */
void _spli_preodfs(SplayIntNode *root_node, void ***int_ptr, int int_opt) {
    // Recursion base step.
    if (root_node == NULL) {
        // Correctly update the pointer.
        if (int_opt & SEARCH_KEYS) {
            *(int_ptr) = (void **)((int *)*(int_ptr) - 1);
        } else *(int_ptr) = *(int_ptr) - 1;
        return;
    }
    // Recursive step.
    // Visit the root node.
    if (int_opt & SEARCH_NODES) {
        **(int_ptr) = root_node;
    } else if (int_opt & SEARCH_KEYS) {
        *(int *)*(int_ptr) = root_node->_key;
    } else if (int_opt & SEARCH_DATA) {
        **(int_ptr) = root_node->_data;
    }
    // Correctly increment the internal pointer.
    if (int_opt & SEARCH_KEYS) {
        *(int_ptr) = (void **)((int *)*(int_ptr) + 1);
    } else *(int_ptr) = *(int_ptr) + 1;
    // Now visit the left son.
    _spli_preodfs(root_node->_left_son, int_ptr, int_opt);
    // Correctly increment the internal pointer.
    if (int_opt & SEARCH_KEYS) {
        *(int_ptr) = (void **)((int *)*(int_ptr) + 1);
    } else *(int_ptr) = *(int_ptr) + 1;
    // Visit the right son and return.
    _spli_preodfs(root_node->_right_son, int_ptr, int_opt);
}

/*
 * Performs a post-order, recursive DFS.
 *
 * @param root_node Root of the subtree to start the search onto.
 * @param int_ptr Internal pointer to a pointer to the return array.
 * @param int_opt Internal options passed value.
 */
void _spli_postodfs(SplayIntNode *root_node, void ***int_ptr, int int_opt) {
    // Recursion base step.
    if (root_node == NULL) {
        // Correctly update the pointer.
        if (int_opt & SEARCH_KEYS) {
            *(int_ptr) = (void **)((int *)*(int_ptr) - 1);
        } else *(int_ptr) = *(int_ptr) - 1;
        return;
    }
    // Recursive step.
    // Visit the left son.
    _spli_postodfs(root_node->_left_son, int_ptr, int_opt);
    // Correctly increment the internal pointer.
    if (int_opt & SEARCH_KEYS) {
        *(int_ptr) = (void **)((int *)*(int_ptr) + 1);
    } else *(int_ptr) = *(int_ptr) + 1;
    // Visit the right son.
    _spli_postodfs(root_node->_right_son, int_ptr, int_opt);
    // Correctly increment the internal pointer.
    if (int_opt & SEARCH_KEYS) {
        *(int_ptr) = (void **)((int *)*(int_ptr) + 1);
    } else *(int_ptr) = *(int_ptr) + 1;
    // Visit the root node and return.
    if (int_opt & SEARCH_NODES) {
        **(int_ptr) = root_node;
    } else if (int_opt & SEARCH_KEYS) {
        *(int *)*(int_ptr) = root_node->_key;
    } else if (int_opt & SEARCH_DATA) {
        **(int_ptr) = root_node->_data;
    }
}
