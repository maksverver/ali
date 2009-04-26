/* TODO: clean up and document! */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "strings.h"
#include "debug.h"
#include "io.h"
#include "interpreter.h"
#include "opcodes.h"
#include "Array.h"
#include "ScapegoatTree.h"

extern char *yytext;
extern int lineno;
int yyparse();

/* Data structure used to represent a parsed fragment pattern */
typedef enum PatternNodeType {
    PN_FRAG, PN_SEQ, PN_ALT, PN_OPT, PN_WORD
} PatternNodeType;

typedef struct PatternNode PatternNode;
struct PatternNode
{
    PatternNodeType type;
    char            *text;
    PatternNode     *left, *right;
};


/* Removes occurences of FRAG and OPT from pattern node */
PatternNode *pattern_normalize(PatternNode *node);

/* Default path */
const char *output_path = "module.alo";

static int num_verbs = 0, num_prepositions = 0, num_entities = 0;

/* Global variable table */
static Array ar_vars = AR_INIT(sizeof(char*));
static ScapegoatTree st_vars = ST_INIT(
    (EA_cmp)strcmp, (EA_dup)strdup, (EA_free)free, EA_no_dup, EA_no_free );

/* Property table */
static Array ar_properties = AR_INIT(sizeof(char*));
static ScapegoatTree st_properties = ST_INIT(
    (EA_cmp)strcmp, (EA_dup)strdup, (EA_free)free, EA_no_dup, EA_no_free );

/* String table */
static Array ar_strings = AR_INIT(sizeof(char*));
static ScapegoatTree st_strings = ST_INIT(
    (EA_cmp)strcmp, (EA_dup)strdup, (EA_free)free, EA_no_dup, EA_no_free );

/* Symbol table */
static int next_symbol_id = -1;  /* symbols are numbered -1, -2, etc. */
static ScapegoatTree st_symbols = ST_INIT(
    (EA_cmp)strcmp, (EA_dup)strdup, (EA_free)free, EA_no_dup, EA_no_free );

/* OLD: Used when parsing a fragment definition: (TODO: to be removed) */
typedef enum FragmentType {
    F_VERB = 0, F_PREPOSITION = 1, F_ENTITY = 2
} FragmentType;

typedef struct Fragment
{
    FragmentType type;
    int          id;
    const char   *str;
    bool         canon;
} Fragment;

static Fragment fragment;

/* Patterns used to refer to verbs/prepositions/entities */
static Array ar_verbs   = AR_INIT(sizeof(PatternNode*));
static Array ar_preps   = AR_INIT(sizeof(PatternNode*));
static Array ar_ents    = AR_INIT(sizeof(PatternNode*));

/* Function table */
static Array ar_functions = AR_INIT(sizeof(Function));
static ScapegoatTree st_functions = ST_INIT(
    (EA_cmp)strcmp, (EA_dup)strdup, (EA_free)free, EA_no_dup, EA_no_free );

/* Command table */
static Array ar_commands = AR_INIT(sizeof(Command));

/* Word table; similar to string table, but contains words recognized by the
   parser instead of strings used in the code. */
static Array ar_words = AR_INIT(sizeof(char*));
/* TODO: later add ScapegoatTree to lookup words faster? */

/* Grammar rules.
   NB: rules must be stored in increasing order of left-hand-side nonterminal.
*/
static Array ar_grammar = AR_INIT(sizeof(GrammarRuleSet*));
/* TODO: later add ScapegoatTree to look op rules/rulesets faster? */


/* Used when parsing a function definition: */
static char *func_name = NULL;
static Array func_params = AR_INIT(sizeof(char*));
static int func_nlocal = 0, func_nret = 0;
static Array func_body = AR_INIT(sizeof(Instruction));
static Array inv_stack = AR_INIT(sizeof(int));

/* Used when parsing strings */
static char *str_buf = NULL;
static size_t str_len = 0;

/* Used when parsing fragment patterns */
#define MAX_PATTERN_STACK 100
struct PatternNode *pattern_stack[MAX_PATTERN_STACK];
int pattern_stack_size = 0;


void yyerror(const char *str)
{
    fprintf(stderr, "Parse error on line %d: %s [%s]\n", lineno + 1, str, yytext);
}

int yywrap()
{
    return 1;
}


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

PatternNode *clone_pattern_node(PatternNode *node)
{
    if (node == NULL)
        return NULL;

    return make_pattern_node( node->type, node->text,
                              clone_pattern_node(node->left),
                              clone_pattern_node(node->right) );
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


/* Emit an instruction (by adding it to the current function body) */
void emit(int opcode, int arg)
{
    Instruction i = { opcode, arg };
    AR_append(&func_body, &i);
}

/* Patch a jump opcode with target -1 by setting its target to the end of the
   current instruction list, searching for jumps backward starting from the end
   of the instructions + offset. (offset should <= 0) */
void patch_jmp(int offset)
{
    size_t pos = AR_size(&func_body);
    Instruction *code = (Instruction*)AR_data(&func_body) + pos;
    assert(offset <= 0 && pos >= (size_t)-offset);
    pos += offset, code += offset;
    while (pos)
    {
        --pos, --code;
        if ((code->opcode == OP_JMP || code->opcode == OP_JNP) && code->argument == -1)
        {
            code->argument = AR_size(&func_body) - pos - 1;
            return;
        }
    }
    assert(0);
}

int resolve_global(const char *str)
{
    assert(str[0] == '@');
    const void *value = (void*)ST_size(&st_vars);
    const void *key   = str + 1;
    if (!ST_find_or_insert_entry(&st_vars, &key, &value))
        AR_append(&ar_vars, &key);
    return (long)value;
}

int resolve_local(const char *id)
{
    assert(id[0] == '$');
    int n;

    for (n = 0; n < (int)AR_size(&func_params); ++n)
    {
        if (strcmp(id + 1, *(char**)AR_at(&func_params, n)) == 0)
            break;
    }
    if (n == (int)AR_size(&func_params))
    {
        char *str = strdup(id + 1);
        AR_append(&func_params, &str);
        ++func_nlocal;
    }
    return n;
}

int resolve_function(const char *id, int call_nret)
{
    int index = -1, nret = -1;

    /* Look up registered function */
    const void *f_idx;
    if (ST_find(&st_functions, id, &f_idx))
    {
        index = (long)f_idx;
        if (index < 0)
            nret = 0;  // built-in procedure
        else
            nret  = ((Function*)AR_at(&ar_functions, index))->nret;
    }
    else
    if (func_name != NULL && strcmp(id, func_name) == 0)
    {
        /* Recursive call to current function */
        index = AR_size(&ar_functions);
        nret  = func_nret;
    }
    else
    {
        fatal("Reference to undeclared function \"%s\" on line %d.",
            id, lineno + 1);
    }

    if (nret == 1 && call_nret == 0)
        fatal("Function called from statement on line %d.", lineno + 1);

    if (nret == 0 && call_nret == 1)
        fatal("Procedure called from expression on line %d.", lineno + 1);

    assert(nret == call_nret);

    return index;
}

int resolve_string()
{
    const void *value = (void*)ST_size(&st_strings);
    const void *key   = str_buf;
    if (key == NULL) yyerror("key NULL");
    assert(key != NULL);
    if (!ST_find_or_insert_entry(&st_strings, &key, &value))
        AR_append(&ar_strings, &key);
    free(str_buf);
    str_buf = NULL;
    str_len = 0;
    return (long)value;
}

void write_string()
{
    /* Pass current string literal to write */
    emit(OP_LLI, resolve_function("write", 0));
    emit(OP_LLI, resolve_string());
    emit(OP_CAL, 2);
}

int resolve_symbol(const char *str)
{
    assert(str[0] == ':');
    const void *value = (void*)(long)next_symbol_id;
    const void *key   = str + 1;
    if (!ST_find_or_insert_entry(&st_symbols, &key, &value))
        --next_symbol_id;
    return (long)value;
}

int resolve_property(const char *str)
{
    assert(str[0] == '.');
    const void *value = (void*)ST_size(&st_properties);
    const void *key   = str + 1;
    if (!ST_find_or_insert_entry(&st_properties, &key, &value))
        AR_append(&ar_properties, &key);
    return (long)value;
}

void parse_string(const char *token)
{
    /* Extend string buffer to accommodate token */
    size_t token_len = strlen(token);
    if (token[0] == '"')
    {
        token++;
        token_len--;
        assert(token[token_len - 1] == '"');
        token_len--;
    }
    else
    if (token[0] == '!')
    {
        token++;
        token_len--;
        if (token[token_len - 1] == '\n')
            token_len--;
    }
    else
    {
        assert(false);
        return;
    }

    /* Allocate space for the new string */
    str_buf = realloc(str_buf, str_len + token_len + 1);
    assert(str_buf != NULL);

    /* Add token to string buffer, unescaping in the process. */
    size_t pos = 0;
    while (pos < token_len)
    {
        if (token[pos] == '\\')
        {
            if (token[pos + 1] == '\\')
            {
                str_buf[str_len++] = '\\';
                pos += 2;
                continue;
            }
            if (token[pos + 1] == 'n')
            {
                str_buf[str_len++] = '\n';
                pos += 2;
                continue;
            }
            if (token[pos + 1] == 't')
            {
                str_buf[str_len++] = '\t';
                pos += 2;
                continue;
            }
        }
        str_buf[str_len++] = token[pos++];
    }

    /* Zero-terminate string */
    str_buf[str_len] = '\0';
}

void begin_function(const char *id, int nret)
{
    assert(func_name == NULL);

    func_name   = (id == NULL ? NULL : strdup(id));
    func_nlocal = 0;
    func_nret   = nret;
}

void add_parameter(const char *id)
{
    assert(id[0] == '$');
    char *str = strdup(id + 1);
    AR_append(&func_params, &str);
}

void end_function()
{
    Function f;
    int n;

    /* Emit return instruction */
    assert(func_nret == 0 || func_nret == 1);
    emit(OP_RET, func_nret);

    /* Create function definition */
    f.id     = AR_size(&ar_functions);
    f.nparam = AR_size(&func_params) - func_nlocal;
    f.nret   = func_nret;
    f.ninstr = AR_size(&func_body) + func_nlocal;
    f.instrs = malloc(f.ninstr*sizeof(Instruction));

    /* Add local variables */
    for (n = 0; n < func_nlocal; ++n)
    {
        f.instrs[n].opcode   = OP_LLI;
        f.instrs[n].argument = -1;
    }

    /* Copy parsed instructions */
    memcpy(f.instrs + func_nlocal, AR_data(&func_body),
        AR_size(&func_body)*sizeof(Instruction));
    AR_clear(&func_body);

    /* Add to function map and array */
    if (func_name != NULL &&
        ST_insert(&st_functions, func_name, (void*)AR_size(&ar_functions)))
    {
        fatal("Redefinition of function \"%s\" on line %d.",
            func_name, lineno + 1);
    }
    AR_append(&ar_functions, &f);

    /* Free allocated resources */
    free(func_name);
    func_name = NULL;
    for (n = 0; n < (int)AR_size(&func_params); ++n)
        free(*(char**)AR_at(&func_params, n));
    AR_clear(&func_params);
    func_nlocal = 0;
}

void begin_verb()
{
    fragment.type  = F_VERB;
    fragment.id    = num_verbs++;
    fragment.canon = true;
}

void begin_preposition()
{
    fragment.type  = F_PREPOSITION;
    fragment.id    = num_prepositions++;
    fragment.canon = true;
}

void begin_entity()
{
    fragment.type  = F_ENTITY;
    fragment.id    = num_entities++;
    fragment.canon = true;
}

void add_synonym(PatternNode *node)
{
    node = pattern_normalize(node);
    assert(node != NULL);

    Array *ar;
    switch (fragment.type)
    {
    case F_VERB:
        ar = &ar_verbs;
        break;

    case F_PREPOSITION:
        ar = &ar_preps;
        break;

    case F_ENTITY:
        ar = &ar_ents;
        break;

    default:
        fatal("invalid fragment type");
        break;
    }

    if (fragment.canon)
    {
        AR_append(ar, &node);
        fragment.canon = false;
    }
    else
    {
        PatternNode **prev = AR_last(ar);
        node = make_pattern_node(PN_ALT, NULL, *prev, node);
        assert(node != NULL);
        *prev = node;
    }
}

/* Returns true if `text' starts with `word'; i.e. if `text' is equal to `word',
   or starts with `word' followed by a space character. */
static bool starts_with(const char *text, const char *word)
{
    while (*word != '\0' && *text != '\0' && *word == *text) ++word, ++text;
    return *word == '\0' && (*text == '\0' || *text == ' ');
}

/* Skip a word, and return a pointer to the start of the next word, or the
   end of the string if `text' contains a single word (or is empty). */
static const char *skip_word(const char *text)
{
    assert(text != NULL);
    const char *k = text;
    while (*k && *k != ' ') ++k;
    while (*k == ' ') ++k;
    return k;
}

bool match_pattern(PatternNode *node, const char *i, const char *j)
{
    assert(node != NULL);
    switch (node->type)
    {
    case PN_WORD:
        if (starts_with(i, node->text))
        {
            return skip_word(i) == j;
        }
        return false;

    case PN_SEQ:
        {
            const char *k = i;
            for (;;)
            {
                if (match_pattern(node->left,  i, k) &&
                    match_pattern(node->right, k, j))
                {
                    return true;
                }
                if (*k == '\0') break;
                k = skip_word(k);
            }
        } break;

    case PN_ALT:
        return match_pattern(node->left,  i, j) ||
               match_pattern(node->right, i, j);

    case PN_OPT:
        return i == j || match_pattern(node->left, i, j);

    default:
        assert(false);
    }
    return false;
}

/* Return index of a pattern that matches fragment */
int find_fragment(Array *ar_patterns, const char *i, const char *j)
{
    PatternNode **patterns = AR_data(ar_patterns);
    size_t npattern = AR_size(ar_patterns), n;
    int res = -1;

    for (n = 0; n < npattern; ++n)
    {
        if (!match_pattern(patterns[n], i, j))
            continue;
        if (res != -1)
            return -2;
        res = n;
    }

    if (res == -1)
        return -1;

    return res;
}

/* Resolve fragment token given by string [i:j) of type `type'.
   Returns -1 if not match found, -2 for multiple matches, >= 0 otherwise. */
int resolve_fragment(int type, const char *i, const char *j)
{
/*
    const void *f_idx;
    const Fragment *f;
    char *str = strdup(token);
    normalize(str);
    if (!ST_find(&st_fragments, str, &f_idx))
    {
        fatal("Reference to undeclared fragment \"%s\" on line %d.",
            str, lineno + 1);
    }
    free(str);
    f = AR_at(&ar_fragments, (long)f_idx);
    if (type != -1 && type != (int)f->type)
    {
        fatal("Fragment referenced by \"%s\" has wrong type on line %d.",
            str, lineno + 1);
    }
    return f->id;
*/
    switch (type)
    {
    case F_VERB:
        return find_fragment(&ar_verbs, i, j);

    case F_PREPOSITION:
        return find_fragment(&ar_preps, i, j);

    case F_ENTITY:
        return find_fragment(&ar_ents, i, j);

    default:
        fatal("invalid fragment type");
        return -1;
    }
}

int resolve_entity(const char *text)
{
    int res = resolve_fragment(F_ENTITY, text, text + strlen(text));
    if (res == -1)
        fatal("Couldn't match fragment \"%s\" on line %d.", text, lineno + 1);
    if (res == -2)
        fatal("Ambiguous fragment \"%s\" on line %d.", text, lineno + 1);
    assert(res >= 0);
    return res;
}

SymbolRef pattern_to_grammar(PatternNode *node)
{
    GrammarRuleSet *ruleset;

    switch (node->type)
    {
    case PN_WORD:
        {
            int nwords = (int)AR_size(&ar_words);
            const char **words = AR_data(&ar_words);

            /* Try to find an existing equal terminal symbol */
            SymbolRef res = { SYM_TERMINAL, 0 };
            for (res.index = 0; res.index < nwords; ++res.index)
                if (strcmp(words[res.index], node->text) == 0)
                    break;

            /* Add a new terminal symbol if none existed. */
            if (res.index == nwords)
            {
                char *str = strdup(node->text);
                AR_push(&ar_words, &str);
            }

            return res;
        }

    case PN_SEQ:
        {
            ruleset = ruleset_create(1);
            assert(ruleset != NULL);
            ruleset->rules[0] = symrefs_create(2);
            assert(ruleset->rules[0] != NULL);
            ruleset->rules[0]->refs[0] = pattern_to_grammar(node->left);
            ruleset->rules[0]->refs[1] = pattern_to_grammar(node->right);
        } break;

    case PN_ALT:
        {
            ruleset = ruleset_create(2);
            assert(ruleset != NULL);
            ruleset->rules[0] = symrefs_create(1);
            assert(ruleset->rules[0] != NULL);
            ruleset->rules[1] = symrefs_create(1);
            assert(ruleset->rules[1] != NULL);
            ruleset->rules[0]->refs[0] = pattern_to_grammar(node->left);
            ruleset->rules[1]->refs[0] = pattern_to_grammar(node->right);
        } break;

    case PN_OPT:
        {
            ruleset = ruleset_create(2);
            assert(ruleset != NULL);
            ruleset->rules[0] = symrefs_create(0);
            assert(ruleset->rules[0] != NULL);
            ruleset->rules[1] = symrefs_create(1);
            assert(ruleset->rules[1] != NULL);
            ruleset->rules[1]->refs[0] = pattern_to_grammar(node->left);
        } break;

    default:
        assert(false);
    }

    ruleset_sort(ruleset);

    /* See if the rule set matches an existing symbol's rule set */
    GrammarRuleSet **rulesets = AR_data(&ar_grammar);
    size_t nruleset = AR_size(&ar_grammar), n;
    for (n = 0; n < nruleset; ++n)
        if (ruleset_cmp(rulesets[n], ruleset) == 0)
            break;

    if (n < nruleset)
        ruleset_destroy(ruleset);
    else
        AR_push(&ar_grammar, &ruleset);

    SymbolRef res = { SYM_NONTERMINAL, (int)n };
    return res;
}

static bool parse_command(char *str, PatternNode **pattern)
{
    const char *end = str + strlen(str);
    int verb, ent1, prep, ent2;
    const char *p, *q, *r;

    /* Resolve form 1: VERB */
    verb = resolve_fragment(F_VERB, str, end);
    if (verb >= 0)
    {
        *pattern = *(PatternNode**)AR_at(&ar_verbs, verb);
        return true;
    }

    /* Resolve form 2: VERB ENTITY */
    verb = ent1 = -1;
    for (p = skip_word(str); *p != '\0'; p = skip_word(p))
    {
        int v = resolve_fragment(F_VERB, str, p);
        int e = resolve_fragment(F_ENTITY, p, end);
        if (v < 0 || e < 0) continue;

        verb = v;
        ent1 = e;
        // don't break here, so we find the longest matching verb
    }
    if (verb >= 0 && ent1 >= 0)
    {
        PatternNode *verb_node = *(PatternNode**)AR_at(&ar_verbs, verb);
        PatternNode *ent1_node = *(PatternNode**)AR_at(&ar_ents,  ent1);
        assert(verb_node != NULL);
        assert(ent1_node != NULL);
        *pattern = make_pattern_node(PN_SEQ, NULL, verb_node, ent1_node);
        return true;
    }

    /* Resolve form 3: VERB ENTITY PREPOSITION ENTITY */
    verb = ent1 = prep = ent2 = -1;
    for (p = skip_word(str); *p != '\0'; p = skip_word(p))
    {
        int v = resolve_fragment(F_VERB, str, p);
        if (v < 0) continue;

        for (q = skip_word(p); *q != '\0'; q = skip_word(q))
        {
            int e1 = resolve_fragment(F_ENTITY, p, q);
            if (e1 < 0) continue;

            for (r = skip_word(q); *r != '\0'; r = skip_word(r))
            {
                int p  = resolve_fragment(F_PREPOSITION, q, r);
                int e2 = resolve_fragment(F_ENTITY, r, end);
                if (p < 0 || e2 < 0) continue;

                // Match found:
                verb = v;
                ent1 = e1;
                prep = p;
                ent2 = e2;
                // don't break here, so we find the longest matching verb
            }
        }
    }
    if (verb >= 0 && ent1 >= 0 && prep >= 0 && ent2 >= 0)
    {
        PatternNode *verb_node = *(PatternNode**)AR_at(&ar_verbs, verb);
        PatternNode *ent1_node = *(PatternNode**)AR_at(&ar_ents,  ent1);
        PatternNode *prep_node = *(PatternNode**)AR_at(&ar_preps, prep);
        PatternNode *ent2_node = *(PatternNode**)AR_at(&ar_ents,  ent2);
        assert(verb_node != NULL);
        assert(ent1_node != NULL);
        assert(prep_node != NULL);
        assert(ent2_node != NULL);
        PatternNode *a = make_pattern_node(PN_SEQ, NULL, verb_node, ent1_node);
        PatternNode *b = make_pattern_node(PN_SEQ, NULL, prep_node, ent2_node);
        *pattern = make_pattern_node(PN_SEQ, NULL, a, b);
        return true;
    }

    return false;
}

void begin_command(const char *str)
{
    PatternNode *node = NULL;
    Command command;

    /* Normalize and then parse fragment */
    char *fragment = strdup(str);
    normalize(fragment);
    assert(fragment != NULL);
    if (!parse_command(fragment, &node))
    {
        fatal("Could not parse command \"%s\" on line %d.",
            fragment, lineno + 1);
    }
    free(fragment);
    assert(node != NULL);

    /* Convert fragment node to grammar rule */
    command.symbol = pattern_to_grammar(node);

    /* Add partial command to command array */
    command.guard    = -1;
    command.function = -1;
    AR_append(&ar_commands, &command);
}

void end_guard()
{
    assert(func_name == NULL);

    /* Terminate guard function */
    size_t guard = AR_size(&ar_functions);
    func_nret = 1;
    end_function();

    /* Add guard to open commands */
    size_t n = AR_size(&ar_commands);
    while (n-- > 0)
    {
        Command *cmd = AR_at(&ar_commands, n);
        if (cmd->function >= 0) break;  /* complete function found */
        cmd->guard = guard;
    }
}

void end_command()
{
    /* Terminate body function */
    size_t function = AR_size(&ar_functions);
    end_function(0);

    /* Add function to open commands */
    size_t n = AR_size(&ar_commands);
    while (n-- > 0)
    {
        Command *cmd = AR_at(&ar_commands, n);
        if (cmd->function >= 0) break;  /* complete function found */
        cmd->function = function;
    }
}

void begin_call(const char *name, int nret)
{
    int nargs = 0;
    emit(OP_LLI, resolve_function(name, nret));
    AR_push(&inv_stack, &nargs);
}

void count_arg()
{
    assert(AR_size(&inv_stack) > 0);
    ++*(int*)AR_last(&inv_stack);
}

void end_call(int nret)
{
    int nargs;
    assert(AR_size(&inv_stack) > 0);
    AR_pop(&inv_stack, &nargs);
    emit(OP_CAL, 256*nret + (1 + nargs));
}

/* Bind a new symbol to the current entity. */
void bind_sym_ent_ref(const char *str)
{
    const void *value = (void*)(long)fragment.id;
    const void *key   = str;
    if (ST_find_or_insert_entry(&st_symbols, &key, &value))
    {
        fatal("Attempt to rebind symbol %s on line %d.\n", str, lineno + 1);
    }
    return;
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

void add_synonyms()
{
    assert(pattern_stack_size == 1);
    add_synonym(pattern_stack[--pattern_stack_size]);
}

PatternNode *frag_make_words(char *text)
{
    char *p = strchr(text, ' ');
    if (p == NULL)
    {
        return make_pattern_node(PN_WORD, text, NULL, NULL);
    }
    else
    {
        *p = '\0';
        return make_pattern_node( PN_SEQ, NULL,
                                  make_pattern_node(PN_WORD, text, NULL, NULL),
                                  frag_make_words(p + 1) );
    }
}

PatternNode *pattern_make_words(PatternNode *node)
{
    switch (node->type)
    {
    case PN_FRAG:
        {
            normalize(node->text);
            PatternNode *res = frag_make_words(node->text);
            free_pattern_node(node);
            return res;
        }
        break;

    case PN_SEQ:
        node->left  = pattern_make_words(node->left);
        node->right = pattern_make_words(node->right);
        return node;

    case PN_ALT:
        node->left  = pattern_make_words(node->left);
        node->right = pattern_make_words(node->right);
        return node;

    case PN_OPT:
        node->left = pattern_make_words(node->left);
        assert(node->right == NULL);
        return node;

    case PN_WORD:
        return node;

    default:
        assert(false);
    }
}

PatternNode *pattern_remove_opts(PatternNode *node, bool *empty)
{
    switch (node->type)
    {
    case PN_FRAG:
    case PN_WORD:
        *empty = false;
        return node;

    case PN_SEQ:
        {
            PatternNode *v, *w;
            bool p, q;
            v = pattern_remove_opts(node->left, &p);
            w = pattern_remove_opts(node->right, &q);
            node->left = node->right = NULL;
            free_pattern_node(node);

            PatternNode *res = NULL;
            if (!p && !q)
            {
                *empty = false;
                res = make_pattern_node(PN_SEQ, NULL, v, w);
            }
            else
            if (!p && q)
            {
                *empty = false;
                res = make_pattern_node( PN_ALT, NULL,
                        clone_pattern_node(v),
                        make_pattern_node(PN_SEQ, NULL, v, w) );
            }
            else
            if (p && !q)
            {
                *empty = false;
                res = make_pattern_node( PN_ALT, NULL,
                        make_pattern_node(PN_SEQ, NULL, v, w),
                        clone_pattern_node(w) );
            }
            else
            if (p && q)
            {
                *empty = true;
                res = make_pattern_node( PN_ALT, NULL,
                        clone_pattern_node(v),
                        make_pattern_node( PN_ALT, NULL,
                            clone_pattern_node(w),
                            make_pattern_node(PN_SEQ, NULL, v, w) ) );
            }
            assert(res != NULL);
            return res;
        }

    case PN_ALT:
        {
            PatternNode *v, *w;
            bool p, q;
            v = pattern_remove_opts(node->left, &p);
            w = pattern_remove_opts(node->right, &q);
            node->left = node->right = NULL;
            free_pattern_node(node);
            *empty = p || q;
            PatternNode *res = make_pattern_node(PN_ALT, NULL, v, w);
            return res;
        }

    case PN_OPT:
        {
            assert(node->right == NULL);
            bool dummy;
            PatternNode *res = pattern_remove_opts(node->left, &dummy);
            node->left = NULL;
            free_pattern_node(node);
            *empty = true;
            return res;
        }

    default:
        assert(false);
    }
}

PatternNode *pattern_normalize(PatternNode *node)
{
    node = pattern_make_words(node);
    return node;
}

/* Writes an IFF chunk header, consisting of a four-byte type identifier,
   followed by the 32-bit chunk size (including the chunk header) */
static bool chunk_begin(IOStream *ios, const char *type, size_t size)
{
    assert(strlen(type) == 4);
    return
        write_int8(ios, type[0]) &&
        write_int8(ios, type[1]) &&
        write_int8(ios, type[2]) &&
        write_int8(ios, type[3]) &&
        write_int32(ios, (int)size);
}

/* Terminates an IFF chunk header, by padding to a 2-byte boundary. */
static bool chunk_end(IOStream *ios, size_t size)
{
    return (size&1) ? write_int8(ios, 0) : true;
}

static size_t get_MOD_chunk_size()
{
    return 20;
}

static bool write_MOD_chunk(IOStream *ios, size_t chunk_size)
{
    int init_func = -1;
    const void *init_idx;
    if (ST_find(&st_functions, "initialize", &init_idx))
        init_func = (long)init_idx;

    return
        chunk_begin(ios, "MOD ", chunk_size) &&
        write_int16(ios, 0x0100) &&  /* file version: 1.0 */
        write_int16(ios, 0) &&       /* reserved */
        write_int32(ios, AR_size(&ar_vars)) &&
        write_int32(ios, num_entities) &&
        write_int32(ios, AR_size(&ar_properties)) &&
        write_int32(ios, init_func) &&
        chunk_end(ios, chunk_size);
}

/* Returns size of a string table chunk (either STR or WRD chunk) */
static size_t get_string_chunk_size(Array *ar)
{
    size_t nstr = AR_size(ar);
    char **strs = (char**)AR_data(ar);
    size_t chunk_size = 4, n;
    for (n = 0; n < nstr; ++n)
        chunk_size += strlen(strs[n]) + 1;
    return chunk_size;
}

/* Writes a string table chunk (either STR or WRD chunk) */
static size_t write_string_chunk(IOStream *ios, size_t chunk_size,
                                 Array *ar, const char *type)
{
    if (!chunk_begin(ios, type, chunk_size))
        return false;

    size_t nstr = AR_size(ar), n;
    if (!write_int32(ios, (int)nstr))
        return false;

    char **strs = (char**)AR_data(ar);
    for (n = 0; n < nstr; ++n)
        if (!write_data(ios, strs[n], strlen(strs[n]) + 1))
            return false;

    return chunk_end(ios, chunk_size);
}

static size_t get_STR_chunk_size()
{
    return get_string_chunk_size(&ar_strings);
}

static bool write_STR_chunk(IOStream *ios, size_t chunk_size)
{
    return write_string_chunk(ios, chunk_size, &ar_strings, "STR ");
}

static size_t get_FUN_chunk_size()
{
    size_t nfunction = AR_size(&ar_functions);
    Function *functions = (Function*)AR_data(&ar_functions);
    size_t table_size = 4 + 4*nfunction, n;
    for (n = 0; n < nfunction; ++n)
        table_size += 4*(functions[n].ninstr + 1);
    return table_size;
}

static bool write_FUN_chunk(IOStream *ios, size_t chunk_size)
{
    if (!chunk_begin(ios, "FUN ", chunk_size))
        return false;

    Function *functions = (Function*)AR_data(&ar_functions);
    size_t nfunction = AR_size(&ar_functions), n, i;

    if (!write_int32(ios, (int)nfunction))
        return false;

    /* Write function headers */
    for (n = 0; n < nfunction; ++n)
    {
        if (!write_int16(ios, 0) ||
            !write_int8(ios, functions[n].nret) ||
            !write_int8(ios, functions[n].nparam))
        {
            return false;
        }
    }

    /* Write function instructions */
    for (n = 0; n < nfunction; ++n)
    {
        Instruction *ins = functions[n].instrs;
        for (i = 0; (int)i < functions[n].ninstr; ++i)
        {
            assert(ins[i].opcode == (ins[i].opcode&255));
            assert(ins[i].argument >= -0x00800000 && ins[i].argument <= 0x007fffff);
            if (!write_int8(ios, ins[i].opcode) ||
                !write_int24(ios, ins[i].argument))
                return false;
        }
        if (!write_int32(ios, 0))
            return false;
    }

    return chunk_end(ios, chunk_size);
}

static size_t get_WRD_chunk_size()
{
    return get_string_chunk_size(&ar_words);
}

static bool write_WRD_chunk(IOStream *ios, size_t chunk_size)
{
    return write_string_chunk(ios, chunk_size, &ar_words, "WRD ");
}

/* Returns total number of non-terminal symbols. */
static int get_num_nonterm()
{
    return (int)AR_size(&ar_grammar);
}

/* Returns total number of symbol refs in grammar table. */
static int get_num_symrefs()
{
    int cnt = 0;
    GrammarRuleSet **rulesets = AR_data(&ar_grammar);
    size_t nruleset = AR_size(&ar_grammar), n, m;
    for (n = 0; n < nruleset; ++n)
        for (m = 0; m < rulesets[n]->nrule; ++m)
            cnt += rulesets[n]->rules[m]->nref;
    return cnt;
}

/* Returns total number of rules in grammar table. */
static int get_num_rules()
{
    int cnt = 0;
    GrammarRuleSet **rulesets = AR_data(&ar_grammar);
    size_t nruleset = AR_size(&ar_grammar), n;
    for (n = 0; n < nruleset; ++n)
        cnt += rulesets[n]->nrule;
    return cnt;
}

static size_t get_GRM_chunk_size()
{
    return 12 + 4*get_num_nonterm() + 4*get_num_rules() + 4*get_num_symrefs();
}

static bool write_grammar_symbol(IOStream *ios, SymbolRef *ref)
{
    switch (ref->type)
    {
    case SYM_NONE:
        return write_int32(ios, 0);

    case SYM_TERMINAL:
        return write_int32(ios, -1 - ref->index);

    case SYM_NONTERMINAL:
        return write_int32(ios,  1 + ref->index);

    default:
        assert(false);
    }
}

static bool write_grammar_rule(IOStream *ios, SymbolRefList *rule)
{
    if (!write_int32(ios, rule->nref))
        return false;
    size_t n;
    for (n = 0; n < rule->nref; ++n)
        if (!write_grammar_symbol(ios, &rule->refs[n]))
            return false;
    return true;
}

static bool write_GRM_chunk(IOStream *ios, size_t chunk_size)
{
    if (!chunk_begin(ios, "GRM ", chunk_size) ||
        !write_int32(ios, get_num_nonterm()) ||
        !write_int32(ios, get_num_rules()) ||
        !write_int32(ios, get_num_symrefs()))
        return false;

    GrammarRuleSet **rulesets = AR_data(&ar_grammar);
    size_t nruleset = AR_size(&ar_grammar), n, m;
    for (n = 0; n < nruleset; ++n)
    {
        if (!write_int32(ios, (int)rulesets[n]->nrule))
            return false;
        for (m = 0; m < rulesets[n]->nrule; ++m)
        {
            if (!write_grammar_rule(ios, rulesets[n]->rules[m]))
                return false;
        }
    }
    return chunk_end(ios, chunk_size);
}

static size_t get_CMD_chunk_size()
{
    return 8 + 12*AR_size(&ar_commands);
}

static bool write_CMD_chunk(IOStream *ios, size_t chunk_size)
{
    if (!chunk_begin(ios, "CMD ", chunk_size))
        return false;

    Command *commands = AR_data(&ar_commands);
    size_t ncommand = AR_size(&ar_commands);

    if (!write_int32(ios, 1) || !write_int32(ios, (int)ncommand))
        return false;

    size_t n;
    for (n = 0; n < ncommand; ++n)
    {
        if (!write_grammar_symbol(ios, &commands[n].symbol) ||
            !write_int32(ios, commands[n].guard) ||
            !write_int32(ios, commands[n].function))
            return false;
    }

    return chunk_end(ios, chunk_size);
}

static bool write_alio(IOStream *ios)
{
    int MOD_size = get_MOD_chunk_size();
    int STR_size = get_STR_chunk_size();
    int FUN_size = get_FUN_chunk_size();
    int WRD_size = get_WRD_chunk_size();
    int GRM_size = get_GRM_chunk_size();
    int CMD_size = get_CMD_chunk_size();

    size_t FRM_size = 4;
    FRM_size += 8 + MOD_size + (MOD_size&1);
    FRM_size += 8 + STR_size + (STR_size&1);
    FRM_size += 8 + FUN_size + (FUN_size&1);
    FRM_size += 8 + WRD_size + (WRD_size&1);
    FRM_size += 8 + GRM_size + (GRM_size&1);
    FRM_size += 8 + CMD_size + (CMD_size&1);

    return
        chunk_begin(ios, "FORM", FRM_size) &&
        write_data(ios, "ALI ", 4) &&
        write_MOD_chunk(ios, MOD_size) &&
        write_STR_chunk(ios, STR_size) &&
        write_FUN_chunk(ios, FUN_size) &&
        write_WRD_chunk(ios, WRD_size) &&
        write_GRM_chunk(ios, GRM_size) &&
        write_CMD_chunk(ios, CMD_size) &&
        chunk_end(ios, FRM_size);
}

void create_object_file()
{
    IOStream ios;
    if (!ios_open(&ios, output_path, IOM_WRONLY, IOC_COPY))
        fatal("Unable to open output file \"%s\".", output_path);
    if (!write_alio(&ios))
        fatal("Unable to write output file \"%s\".", output_path);
    ios_close(&ios);
}

void parser_create()
{
    /* Register built-in variables */
    {
        const char * const *name;
        for (name = builtin_var_names; *name != NULL; ++name)
        {
            const void *key   = *name;
            const void *value = (void*)ST_size(&st_vars);
            ST_insert_entry(&st_vars, &key, &value);
            AR_append(&ar_vars, &key);
        }
        assert(ST_size(&st_vars) == AR_size(&ar_vars));
    }

    /* Register built-in functions */
    {
        const char * const *name;
        long func_id = 0;
        for (name = builtin_func_names; *name != NULL; ++name)
            ST_insert(&st_functions, *name, (void*)--func_id);
    }
}

void parser_destroy()
{
    if (func_name) end_function(0);
    if (str_buf != NULL)
    {
        free(str_buf);
        str_buf = NULL;
    }
    AR_destroy(&ar_vars);
    ST_destroy(&st_vars);
    AR_destroy(&ar_properties);
    ST_destroy(&st_properties);
    AR_destroy(&ar_strings);
    ST_destroy(&st_strings);
    ST_destroy(&st_symbols);
    AR_destroy(&ar_verbs);
    AR_destroy(&ar_ents);
    AR_destroy(&ar_preps);
    while (!AR_empty(&ar_functions))
    {
        Function *f = AR_last(&ar_functions);
        free(f->instrs);
        AR_pop(&ar_functions, NULL);
    }
    AR_destroy(&ar_functions);
    ST_destroy(&st_functions);
    AR_destroy(&ar_commands);
    AR_destroy(&func_body);
    AR_destroy(&inv_stack);
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: alic <source>\n");
        return 0;
    }

    if (strcmp(argv[1], "-") != 0)
    {
        /* Open source file */
        if (freopen(argv[1], "rt", stdin) == NULL)
            fatal("Unable to open file \"%s\" for reading.");
    }

    parser_create();
    yyparse();
    create_object_file();
    parser_destroy();

    return 0;
}
