#ifndef SCAPEGOAT_TREE_H
#define SCAPEGOAT_TREE_H

#include <stdbool.h>
#include <stdlib.h>
#include "elements.h"

/* TODO: document more clearly, especially how memory management works.
*/

typedef int (*ST_it_callback) (void *arg, const void *key, const void *value);

typedef struct ScapegoatTree
{
    size_t  size;
    size_t  max_size;
    struct  Node *root;
    EA_cmp  key_cmp;
    EA_dup  key_dup;
    EA_free key_free;
    EA_dup  value_dup;
    EA_free value_free;
} ScapegoatTree;

/* Initializer for tree data structure(analogous to constructor)
   described below. */
#define ST_INIT(key_cmp, key_dup, key_free, value_dup, value_free) \
    { 0, 0, NULL, key_cmp, key_dup, key_free, value_dup, value_free }

/* Initializes a tree data structure. */
void ST_create(ScapegoatTree *tree,
    EA_cmp key_cmp, EA_dup key_dup, EA_free key_free,
    EA_dup value_dup, EA_free value_free);

/* Destroys a tree data structure and deallocats all associated resources. */
void ST_destroy(ScapegoatTree *tree);

/* Returns the number of nodes in the tree. */
size_t ST_size(ScapegoatTree *tree);

/* Searches for an item in the tree, returns TRUE and updates value if found. */
bool ST_find(const ScapegoatTree *tree, const void *key, const void **value);
bool ST_find_entry(const ScapegoatTree *tree, const void **key, const void **value);

/* Inserts an entry in the tree. Returns TRUE if an existing item was
   overwritten, FALSE otherwise. */
bool ST_insert(ScapegoatTree *tree, const void *key, const void *value);
bool ST_insert_entry(ScapegoatTree *tree, const void **key, const void **value);

/* Inserts an entry only if no entry with the same key exists.
   Returns wether an old version existed.
*/
bool ST_find_or_insert(ScapegoatTree *tree, const void *key, const void **value);
bool ST_find_or_insert_entry(ScapegoatTree *tree, const void **key, const void **value);

/* Erases an item in the tree. Returns TRUE if the item was present, or
   FALSE otherwise. */
bool ST_erase(ScapegoatTree *tree, const void *key, size_t key_size);

/* Iterate over the contents of the tree.
   The callback is called for every value. If it returns non-zero, iteration
   is aborted, and the value is returned by ST_iterate.
   'arg' is a dummy argument. */
int ST_iterate(const ScapegoatTree *tree, ST_it_callback callback, void *arg);

#endif /* ndef SCAPEGOAT_TREE_H */
