/* Test code for generating fragments from a regular-expression-like pattern.

   Patterns are either:
    - simple fragments
    - optional patterns enclosed in square braces
    - grouped patterns enclosed in parentheses
    - alternative patterns separated by slash characters
   Example:
     [THE] (PINK/PURPLE) BAG/LUGGAGE
   Would generate:
     PINK BAG
     PURPLE BAG
     THE PINK BAG
     THE PURPLE BAG
     LUGGAGE
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

typedef struct PatternNode PatternNode;

struct PatternNode
{
    enum { PN_FRAG, PN_SEQ, PN_ALT, PN_OPT } type;
    char *text;
    PatternNode *left, *right;
};

typedef struct FragmentList FragmentList;

struct FragmentList
{
    FragmentList *next;
    char *text;
};

#define MAX_PATTERN_STACK 100
struct PatternNode *pattern_stack[MAX_PATTERN_STACK];
int pattern_stack_size = 0;

PatternNode *make_pattern_node(int type, const char *text,
                               PatternNode *left, PatternNode *right)
{
    PatternNode *node = malloc(sizeof(PatternNode));
    assert(node != NULL);
    node->type = type;
    node->text = (text == NULL) ? NULL : strdup(text);
    node->left = left;
    node->right = right;
    return node;
}

void free_pattern_node(PatternNode *node)
{
    if (node != NULL)
    {
        free_pattern_node(node->left);
        free_pattern_node(node->right);
        free(node->text);
        free(node);
    }
}

FragmentList *make_fragment_list(FragmentList *next, const char *text)
{
    FragmentList *list = malloc(sizeof(FragmentList));
    assert(list != NULL);
    list->next = next;
    list->text = (text == NULL) ? NULL : strdup(text);
    return list;
}

void free_fragment_list(FragmentList *list)
{
    if (list != NULL)
    {
        free_fragment_list(list->next);
        free(list->text);
        free(list);
    }
}

void pattern_push(const char *str)
{
    assert(pattern_stack_size < MAX_PATTERN_STACK);
    pattern_stack[pattern_stack_size++] =
        make_pattern_node(PN_FRAG, str, NULL, NULL);
}

void pattern_alt()
{
    assert(pattern_stack_size >= 2);
    PatternNode *right = pattern_stack[--pattern_stack_size];
    PatternNode *left  = pattern_stack[--pattern_stack_size];
    pattern_stack[pattern_stack_size++] =
        make_pattern_node(PN_ALT, NULL, left, right);
}

void pattern_seq()
{
    assert(pattern_stack_size >= 2);
    PatternNode *right = pattern_stack[--pattern_stack_size];
    PatternNode *left  = pattern_stack[--pattern_stack_size];
    pattern_stack[pattern_stack_size++] =
        make_pattern_node(PN_SEQ, NULL, left, right);
}

void pattern_opt()
{
    assert(pattern_stack_size >= 1);
    PatternNode *left = pattern_stack[--pattern_stack_size];
    pattern_stack[pattern_stack_size++] =
        make_pattern_node(PN_OPT, NULL, left, NULL);
}

static char *join_fragments(const char *s, const char *t)
{
    if (s == NULL && t == NULL) return NULL;
    if (s == NULL) return strdup(t);
    if (t == NULL) return strdup(s);

    /* Join strings with a space in between */
    size_t len = strlen(s) + strlen(t) + 2;
    char *res = malloc(len);
    assert(res != NULL);
    snprintf(res, len, "%s %s", s, t);
    return res;
}

static bool fragments_contains_null(FragmentList *list)
{
    for ( ; list != NULL; list = list->next)
    {
        if (list->text == NULL)
            return true;
    }
    return false;
}

FragmentList *pattern_to_fragments(PatternNode *node)
{
    switch (node->type)
    {
    case PN_FRAG:
        {
            return make_fragment_list(NULL, node->text);
        }

    case PN_SEQ:
        {
            FragmentList *a, *b, *i, *j,*r = NULL;
            a = pattern_to_fragments(node->left);
            b = pattern_to_fragments(node->right);
            for (i = a; i != NULL; i = i->next)
            {
                for (j = b; j != NULL; j = j->next)
                {
                    char *text = join_fragments(i->text, j->text);
                    r = make_fragment_list(r, text);
                    free(text);
                }
            }
            free_fragment_list(a);
            free_fragment_list(b);
            return r;
        }

    case PN_ALT:
        {
            FragmentList *a, *b, **p;
            a = pattern_to_fragments(node->left);
            b = pattern_to_fragments(node->right);
            p = &a;
            while (*p != NULL) p = &(*p)->next;
            *p = b;
            return a;
        }

    case PN_OPT:
        {
            FragmentList *a = pattern_to_fragments(node->left);
            return fragments_contains_null(a) ? a : make_fragment_list(a, NULL);
        } break;
    }
    return NULL;
}


int yyparse();

void yyerror(const char *str)
{
    fprintf(stderr, "Parse error!\n");
    exit(1);
}

int yywrap()
{
    return 1;
}


int main()
{
    yyparse();
    assert(pattern_stack_size == 1);
    PatternNode *root = pattern_stack[--pattern_stack_size];
    FragmentList *all = pattern_to_fragments(root);
    if (fragments_contains_null(all))
        printf("ERROR: fragment pattern matches empty strings!\n");
    FragmentList *p;
    for (p = all; p != NULL; p = p->next)
        if (p->text != NULL)
            printf("%s\n",  p->text);
    free_pattern_node(root);
    free_fragment_list(all);
    return 0;
}
