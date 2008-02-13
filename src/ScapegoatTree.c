#include "ScapegoatTree.h"
#include <string.h>
#include <math.h>
#include "debug.h"

const double alpha = 0.667;

struct Node
{
    struct Node *left;
    struct Node *right;
    void *key;
    void *value;
};

static void free_node(ScapegoatTree *tree, struct Node *node)
{
    if (node != NULL)
    {
        tree->key_free(node->key);
        tree->value_free(node->value);
        free_node(tree, node->left);
        free_node(tree, node->right);
        free(node);
    }
}

static struct Node **find(
    EA_cmp key_cmp, struct Node **node, const void *key, int *depth )
{
    int diff;

    if (*node == NULL)
        return node;

    if (depth != NULL)
        *depth += 1;

    /* Compute difference */
    diff = key_cmp(key, (*node)->key);
    if (diff < 0)
        return find(key_cmp, &(*node)->left, key, depth);
    if (diff > 0)
        return find(key_cmp, &(*node)->right, key, depth);
    return node;
}

static int iterate(struct Node *node, ST_it_callback callback, void *arg)
{
    int res;

    if (node != NULL)
    {
        res = iterate(node->left, callback, arg);
        if (res != 0)
            return res;

        res = callback(arg, node->key, node->value);
        if (res != 0)
            return res;

        res = iterate(node->right, callback, arg);
        if (res != 0)
            return res;
    }

    return 0;
}

static size_t subtree_size(struct Node *node)
{
    if (node == NULL)
        return 0;

    return 1 + subtree_size(node->left) + subtree_size(node->right);
}

/* If given a tree 'x' and a linked list (linked using the 'right' pointers),
   returns a linked list with the nodes in x traversed in-order followed
   by the nodes in y. */
static struct Node *flatten(struct Node *x, struct Node *y)
{
    if (x == NULL)
        return y;
    x->right = flatten(x->right, y);
    return flatten(x->left, x);
}

/* Builds a balanced binary tree from the first n nodes of the linked list x.
   The n+1th node is returned, with its left pointer set to the root of
   the created subtree. */
static struct Node *build_tree(size_t n, struct Node *x)
{
    struct Node *r, *s;

    if (n == 0)
    {
        x->left = NULL;
        return x;
    }
    else
    {
        r = build_tree(n/2, x);
        s = build_tree((n - 1)/2, r->right);
        r->right = s->left;
        s->left  = r;
        return s;
    }
}

static void rebuild(struct Node **node)
{
    struct Node *list, *p, w = { NULL, NULL };
    size_t n = 0;

    list = flatten(*node, &w);
    for (p = list; p != &w; p = p->right)
        ++n;
    build_tree(n, list);
    *node = w.left;
}

/* Either finds a scapegoat node and returns (size_t)-1, or returns the size of the subtree. */
static size_t rebuild_scapegoat(struct Node **node, EA_cmp key_cmp, const void *key)
{
    int n, m, o;
    struct Node **l, *r;

    if (*node == NULL)
        return 1;

    /* Set 'l' to node in path, and 'r' to the other node */
    if (key_cmp(key, (*node)->key) <= 0)
        l = &(*node)->left, r = (*node)->right;
    else
        l = &(*node)->right, r = (*node)->left;

    /* Determine sizes of subtrees */
    n = rebuild_scapegoat(l, key_cmp, key);
    if (n == (size_t)-1)
        return n;
    m = subtree_size(r);

    /* Check if this is the scapegoat node */
    o = 1 + n + m;
    if (n > alpha*o || m > alpha*o)
    {
        rebuild(node);
        return (size_t)-1;
    }

    return o;
}

/* Initializes a tree data structure. */
void ST_create(ScapegoatTree *tree,
    EA_cmp key_cmp, EA_dup key_dup, EA_free key_free,
    EA_dup value_dup, EA_free value_free)
{
    assert(tree != NULL);
    tree->size       = tree->max_size = 0;
    tree->root       = NULL;
    tree->key_cmp    = key_cmp;
    tree->key_dup    = key_dup == NULL ? EA_no_dup : key_dup;
    tree->key_free   = key_free == NULL ? EA_no_free : key_free;
    tree->value_dup  = value_dup == NULL ? EA_no_dup : value_dup;
    tree->value_free = value_free == NULL ? EA_no_free : value_free;
}

void ST_destroy(ScapegoatTree *tree)
{
    free_node(tree, tree->root);
}

size_t ST_size(ScapegoatTree *tree)
{
    return tree->size;
}

static void create_node(ScapegoatTree *tree,
    struct Node **node, int depth, const void *key, const void *value )
{
    /* Allocate new node */
    *node = dmalloc(sizeof(struct Node));
    assert(*node != NULL);
    (*node)->left  = NULL;
    (*node)->right = NULL;
    (*node)->key   = tree->key_dup(key);
    (*node)->value = tree->value_dup(value);

    /* Increment size of tree */
    tree->size += 1;
    if (tree->size > tree->max_size)
        tree->max_size = tree->size;

    /* Rebalance */
    if (depth > log(tree->size)/log(1.0/alpha))
        rebuild_scapegoat(&tree->root, tree->key_cmp, key);
}

bool ST_find(const ScapegoatTree *tree, const void *key, const void **value)
{
    return ST_find_entry(tree, &key, value);
}

bool ST_find_entry(const ScapegoatTree *tree, const void **key, const void **value)
{
    struct Node **node = find(tree->key_cmp, (struct Node**)&tree->root, *key, NULL);
    if (*node == NULL)
        return false;
    *key = (*node)->key;
    if (value != NULL)
        *value = (*node)->value;
    return true;
}

bool ST_insert(ScapegoatTree *tree, const void *key, const void *value)
{
    return ST_insert_entry(tree, &key, &value);
}

bool ST_insert_entry(ScapegoatTree *tree, const void **key, const void **value)
{
    bool result;
    int depth = 0;
    struct Node **node = find(tree->key_cmp, &tree->root, *key, &depth);

    if (*node == NULL)
    {
        create_node(tree, node, depth, *key, *value);
        result = false;
    }
    else
    {
        /* Overwrite existing value */
        tree->value_free((*node)->value);
        (*node)->value = tree->value_dup(*value);
        result = true;
    }

    *key   = (*node)->key;
    *value = (*node)->value;
    return result;
}

bool ST_find_or_insert(ScapegoatTree *tree, const void *key, const void **value)
{
    return ST_find_or_insert_entry(tree, &key, value);
}

bool ST_find_or_insert_entry(ScapegoatTree *tree, const void **key, const void **value)
{
    int depth = 0;
    struct Node **node = find(tree->key_cmp, &tree->root, *key, &depth);
    bool found = (*node != NULL);

    if (!found)
        create_node(tree, node, depth, *key, *value);

    *key   = (*node)->key;
    *value = (*node)->value;

    return found;
}

/* Erases an item in the tree. Returns true if the item was present, or
   false otherwise. */
bool ST_erase(ScapegoatTree *tree, const void *key, size_t key_size)
{
    struct Node **node = find(tree->key_cmp, &tree->root, key, NULL);
    struct Node *old, **pred;

    if (!*node)
        return false;

    old = *node;

    if (old->left == NULL)
        *node = old->right;
    else
    if (old->right == NULL)
        *node = old->left;
    else
    {
        /* Find predecessor (in left subtree) */
        pred = &old->left;
        while ((*pred)->right != NULL)
            pred = &(*pred)->right;
        (*pred)->left  = old->left;
        (*pred)->right = old->right;
        *node = *pred;
        *pred = NULL;
    }

    tree->value_free(old->value);
    tree->key_free(old->key);
    free(old);

    tree->size -= 1;
    if (tree->size < alpha*tree->max_size)
    {
        rebuild(&tree->root);
        tree->max_size = tree->size;
    }

    return true;
}

int ST_iterate(const ScapegoatTree *tree, ST_it_callback callback, void *arg)
{
    return iterate(tree->root, callback, arg);
}
