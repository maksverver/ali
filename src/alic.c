#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "strings.h"
#include "debug.h"
#include "interpreter.h"
#include "opcodes.h"
#include "Array.h"
#include "ScapegoatTree.h"

extern char *yytext;
extern int lineno;
int yyparse();

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
static Array ar_symbols = AR_INIT(sizeof(char*));
static ScapegoatTree st_symbols = ST_INIT(
    (EA_cmp)strcmp, (EA_dup)strdup, (EA_free)free, EA_no_dup, EA_no_free );

/* Fragment table */
static Array ar_fragments = AR_INIT(sizeof(Fragment));
static ScapegoatTree st_fragments = ST_INIT(
    (EA_cmp)strcmp, (EA_dup)strdup, (EA_free)free, EA_no_dup, EA_no_free );

/* Used when parsing a fragment definition: */
static Fragment fragment;

/* Function table */
static Array ar_functions = AR_INIT(sizeof(Function));
static ScapegoatTree st_functions = ST_INIT(
    (EA_cmp)strcmp, (EA_dup)strdup, (EA_free)free, EA_no_dup, EA_no_free );

/* Used when parsing a function definition: */
static char *func_name;
static Array func_params = AR_INIT(sizeof(char*));
static int func_nlocal;
static Array func_body = AR_INIT(sizeof(Instruction));
static Array inv_stack = AR_INIT(sizeof(int));

/* Built-in functions are declared here. */

static Function builtin_functions[] = {
    /* id  args */
    {  -1,  0,  0, NULL },  /* quit */
    {  -2,  1,  0, NULL },  /* write */
    {   0,  0,  0, NULL } };

static const char *builtin_names[] = {
    "quit", "write",
    NULL };


void yyerror(const char *str)
{
    fprintf(stderr,"Parse error on line %d: %s [%s]\n", lineno + 1, str, yytext);
}

int yywrap()
{
    return 1;
}


void begin_function(const char *id)
{
    assert(func_name == NULL);

    func_name   = strdup(id);
    func_nlocal = 0;
}

void add_parameter(const char *id)
{
    char *str = strdup(id);
    AR_append(&func_params, &str);
}

void emit(int opcode, int arg)
{
    Instruction i = { opcode, arg };
    AR_append(&func_body, &i);
}

void end_function()
{
    Function f;
    int n;

    /* Ensure function ends with return */
    /* NOTE: the if-clause below is not enough, because a code fragment like
       "if (false) { return; }" generates code that ends with a RET instruction,
       but this instruction is jumped over. */
    /*
    if (AR_empty(&func_body) ||
        ((Instruction*)AR_last(&func_body))->opcode != OP_RET)
    */
    {
        emit(OP_LLI, 0);
        emit(OP_RET, 0);
    }

    /* Create function definition */
    f.id     = AR_size(&ar_functions);
    f.nparam = AR_size(&func_params) - func_nlocal;
    f.ninstr = AR_size(&func_body) + func_nlocal;
    f.instrs = malloc(f.ninstr*sizeof(Instruction));

    /* Add local variables */
    for (n = 0; n < func_nlocal; ++n)
    {
        f.instrs[n].opcode   = OP_LLI;
        f.instrs[n].argument = 0;
    }

    /* Copy parsed instructions */
    memcpy(f.instrs + func_nlocal, AR_data(&func_body),
        AR_size(&func_body)*sizeof(Instruction));
    AR_clear(&func_body);

    /* Add to function map and array */
    if (ST_insert(&st_functions, func_name, AR_append(&ar_functions, &f)))
    {
        error("Redefinition of function \"%s\".", func_name);
        exit(1);
    }

    /* Free allocated resources */
    free(func_name);
    func_name = NULL;
    for (n = 0; n < AR_size(&func_params); ++n)
        free(*(char**)AR_at(&func_params, n));
    AR_clear(&func_params);
}

void patch_jmp(int offset)
{
    size_t pos = AR_size(&func_body);
    Instruction *code = (Instruction*)AR_data(&func_body) + pos;
    assert(offset <= 0 && pos >= -offset);
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
    const void *value = (void*)ST_size(&st_vars);
    const void *key   = str;
    if (!ST_find_or_insert_entry(&st_vars, &key, &value))
        AR_append(&ar_vars, &key);
    return (long)value;
}

int resolve_local(const char *id)
{
    int n;

    for (n = 0; n < AR_size(&func_params); ++n)
        if (strcmp(id, *(char**)AR_at(&func_params, n)) == 0)
            break;
    if (n == AR_size(&func_params))
    {
        char *str = strdup(id);
        AR_append(&func_params, &str);
        ++func_nlocal;
    }
    return n;
}

int resolve_function(const char *id)
{
    const Function *f;
    if (!ST_find(&st_functions, id, (const void **)&f))
    {
        error("Reference to undeclared function \"%s\".", id);
        exit(1);
    }
    return f->id;
}

int resolve_string(const char *str)
{
    const void *value = (void*)ST_size(&st_strings);
    const void *key   = str;
    if (!ST_find_or_insert_entry(&st_strings, &key, &value))
        AR_append(&ar_strings, &key);
    return (long)value;
}

int resolve_symbol(const char *str)
{
    const void *value = (void*)ST_size(&st_symbols);
    const void *key   = str;
    if (!ST_find_or_insert_entry(&st_symbols, &key, &value))
        AR_append(&ar_symbols, &key);
    return (long)value;
}

int resolve_property(const char *str)
{
    const void *value = (void*)ST_size(&st_properties);
    const void *key   = str;
    if (!ST_find_or_insert_entry(&st_properties, &key, &value))
        AR_append(&ar_properties, &key);
    return (long)value;
}

int parse_string(const char *token)
{
    char *str = strdup(token + 1);
    char *p, *q;
    int result;

    assert(token[0] == '"');
    assert(token[strlen(token) - 1] == '"');
    str[strlen(str) - 1] = '\0';

    /* Unescape */
    for (p = q = str; *p; ++p)
        *q++ = (*p == '\\' ? *++p : *p);
    *q = '\0';
    result = resolve_string(str);
    free(str);
    return result;
}

void begin_verb()
{
    fragment.type = F_VERB;
    fragment.id   = num_verbs++;
}

void begin_preposition()
{
    fragment.type = F_PREPOSITION;
    fragment.id   = num_prepositions++;
}

void begin_entity()
{
    fragment.type = F_ENTITY;
    fragment.id   = num_entities++;
}

void add_fragment(const char *str)
{
    Fragment *f = AR_append(&ar_fragments, &fragment);
    f->str = str;
    if (ST_insert_entry(&st_fragments, (const void**)&f->str, (const void**)&f))
    {
        error("Reference to undeclared fragment \"%s\".", str);
        exit(1);
    }
}

int resolve_fragment(const char *str, int type)
{
    const Fragment *f;
    if (!ST_find(&st_fragments, str, (const void **)&f))
    {
        error("Reference to undeclared fragment \"%s\".", str);
        exit(1);
    }
    if (type != -1 && type != f->type)
    {
        error("Fragment referenced by \"%s\" has wrong type.", str);
        exit(1);
    }
    return f->id;
}

int parse_fragment(const char *token, int type)
{
    char *str;
    int result;

    str = strdup(token);
    normalize(str);
    result = resolve_fragment(str, type);
    free(str);

    return result;
}

void push_args()
{
    int nargs = 0;
    AR_push(&inv_stack, &nargs);
}

void count_arg()
{
    assert(AR_size(&inv_stack) > 0);
    ++*(int*)AR_last(&inv_stack);
}

int pop_args()
{
    int nargs;
    assert(AR_size(&inv_stack) > 0);
    AR_pop(&inv_stack, &nargs);
    return nargs;
}

static bool write_int8(FILE *fp, int i)
{
    unsigned char bytes[1] = { i&255 };
    return fwrite(bytes, 1, 1, fp);
}

static bool write_int16(FILE *fp, int i)
{
    unsigned char bytes[2] = { (i>>8)&255, i&255 };
    return fwrite(bytes, 2, 1, fp);
}

static bool write_int24(FILE *fp, int i)
{
    unsigned char bytes[3] = { (i>>16)&255, (i>>8)&255, i&255 };
    return fwrite(bytes, 3, 1, fp);
}

static bool write_int32(FILE *fp, int i)
{
    unsigned char bytes[4] = { (i>>24)&255, (i>>16)&255, (i>>8)&255, i&255 };
    return fwrite(bytes, 4, 1, fp);
}

static bool write_alio_header(FILE *fp)
{
    const Function *entry = NULL;
    ST_find(&st_functions, "initialize", (const void **)&entry);

    return
        write_int32(fp, 32) &&      /* header size: 32 bytes */
        write_int16(fp, 0x0100) &&  /* file version: 1.0 */
        write_int16(fp, 0) &&       /* reserved */
        write_int32(fp, num_verbs) &&
        write_int32(fp, num_prepositions) &&
        write_int32(fp, num_entities) &&
        write_int32(fp, AR_size(&ar_properties)) &&
        write_int32(fp, AR_size(&ar_vars)) &&
        write_int32(fp, entry ? entry->id : -1);
}

static bool write_alio_fragments(FILE *fp)
{
    int table_size, offset;
    Fragment *fragments;
    int nfragment, n, padding;

    nfragment = (int)AR_size(&ar_fragments);
    fragments = (Fragment*)AR_data(&ar_fragments);

    /* Compute size of table. */
    table_size = 8 + 8*nfragment;
    for (n = 0; n < nfragment; ++n)
        table_size += strlen(fragments[n].str) + 1;
    padding = 0;
    while ((table_size + padding)%4 != 0)
        ++padding;

    if (!(write_int32(fp, table_size + padding) && write_int32(fp, nfragment)))
        return false;

    /* Write fragment headers */
    offset = 8 + 8*nfragment;
    for (n = 0; n < nfragment; ++n)
    {
        if (!(write_int8(fp, fragments[n].type) && write_int24(fp, fragments[n].id)
              && write_int32(fp, offset)))
            return false;
        offset += strlen(fragments[n].str) + 1;
    }

    /* Write fragment strings */
    for (n = 0; n < nfragment; ++n)
        if (!fwrite(fragments[n].str, strlen(fragments[n].str) + 1, 1, fp))
            return false;

    /* Write padding */
    for (n = 0; n < padding; ++n)
        if (!write_int8(fp, 0))
            return false;

    return true;
}

static bool write_alio_strings(FILE *fp)
{
    int table_size, offset;
    char **strings;
    int nstring, n, padding;

    nstring = (int)AR_size(&ar_strings);
    strings = (char**)AR_data(&ar_strings);

    /* Compute size of table. */
    table_size = 8 + 4*nstring;
    for (n = 0; n < nstring; ++n)
        table_size += strlen(strings[n]) + 1;
    padding = 0;
    while ((table_size + padding)%4 != 0)
        ++padding;

    if (!(write_int32(fp, table_size + padding) && write_int32(fp, nstring)))
        return false;

    /* Write offsets */
    offset = 8 + 4*nstring;
    for (n = 0; n < nstring; ++n)
    {
        if (!write_int32(fp, offset))
            return false;
        offset += strlen(strings[n]) + 1;
    }

    /* Write strings */
    for (n = 0; n < nstring; ++n)
        if (!fwrite(strings[n], strlen(strings[n]) + 1, 1, fp))
            return false;

    /* Write padding */
    for (n = 0; n < padding; ++n)
        if (!write_int8(fp, 0))
            return false;

    return true;
}

static bool write_alio_functions(FILE *fp)
{
    int table_size, offset;
    Function *functions;
    int n, i, nfunction;

    functions = (Function*)AR_data(&ar_functions);
    nfunction = AR_size(&ar_functions);

    /* Compute size of table. */
    table_size = 8 + 8*nfunction;
    for (n = 0; n < nfunction; ++n)
        table_size += 4*(functions[n].ninstr + 1);

    if (!(write_int32(fp, table_size) && write_int32(fp, nfunction)))
        return false;

    /* Write function headers */
    offset = 8 + 8*nfunction;
    for (n = 0; n < nfunction; ++n)
    {
        if (!(write_int16(fp, 0) && write_int8(fp, 0) && write_int8(fp, functions[n].nparam)))
            return false;
        if (!write_int32(fp, offset))
            return false;
        offset += 4*(functions[n].ninstr + 1);
    }

    /* Write function instructions*/
    for (n = 0; n < nfunction; ++n)
    {
        Instruction *ins = functions[n].instrs;
        for (i = 0; i < functions[n].ninstr; ++i)
        {
            assert(ins[i].opcode == (ins[i].opcode&255));
            assert(ins[i].opcode >= -0x00800000 && ins[i].opcode <= 0x007fffff);
            if (!(write_int8(fp, ins[i].opcode) && write_int24(fp,ins[i].argument)))
                return false;
        }
        if (!write_int32(fp, 0))
            return false;
    }

    return true;
}

static bool write_alio_commands(FILE *fp)
{
    /* TODO */
    return write_int32(fp, 8) && write_int32(fp, 0);
}

static bool write_alio(FILE *fp)
{
    if (fwrite("alio", 4, 1, fp) != 1)
        return false;

    return write_alio_header(fp) &&
           write_alio_fragments(fp) &&
           write_alio_strings(fp) &&
           write_alio_functions(fp) &&
           write_alio_commands(fp);
}

void create_object_file()
{
    FILE *fp = fopen(output_path, "wb");
    if (!fp)
    {
        error("Unable to open output file \"%s\".", output_path);
        exit(1);
    }

    if (!write_alio(fp))
    {
        fclose(fp);
        error("Unable to write output file \"%s\".", output_path);
        exit(1);
    }

    fclose(fp);
}

void parser_create()
{
    Function *func;
    const char **name;

    func = builtin_functions;
    name = builtin_names;
    while (func->id != 0)
    {
        assert(*name != NULL);
        ST_insert(&st_functions, *name, func);
        ++func, ++name;
    }
    assert(*name == NULL);
}

void parser_destroy()
{
    if (func_name)
        end_function();
    AR_destroy(&ar_vars);
    ST_destroy(&st_vars);
    AR_destroy(&ar_properties);
    ST_destroy(&st_properties);
    AR_destroy(&ar_strings);
    ST_destroy(&st_strings);
    AR_destroy(&ar_symbols);
    ST_destroy(&st_symbols);
    AR_destroy(&ar_fragments);
    ST_destroy(&st_fragments);
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
