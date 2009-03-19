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

/* Command table */
static Array ar_commands = AR_INIT(sizeof(Command));


/* Used when parsing a function definition: */
static char *func_name = NULL;
static Array func_params = AR_INIT(sizeof(char*));
static int func_nlocal = 0, func_nret = 0;
static Array func_body = AR_INIT(sizeof(Instruction));
static Array inv_stack = AR_INIT(sizeof(int));

/* Used when parsing strings */
static char *str_buf = NULL;
static size_t str_len = 0;


/* Built-in functions are declared here. */
static const char *builtin_names[] = {
    "write", "writeln", "writef", "pause", "reset", "quit", NULL };

void yyerror(const char *str)
{
    fprintf(stderr, "Parse error on line %d: %s [%s]\n", lineno + 1, str, yytext);
}

int yywrap()
{
    return 1;
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
    {
        if (strcmp(id, *(char**)AR_at(&func_params, n)) == 0)
            break;
    }
    if (n == AR_size(&func_params))
    {
        char *str = strdup(id);
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

void parse_string(const char *token)
{
    /* Extend string buffer to accommodate token */
    size_t token_len = strlen(token);
    assert(token[0] == '"');
    assert(token[token_len - 1] == '"');
    str_buf = realloc(str_buf, str_len + token_len - 2 + 1);
    assert(str_buf != NULL);

    /* Add token to string buffer, unescaping in the process. */
    size_t pos;
    for (pos = 1; pos < token_len - 1; ++pos)
    {
        if (token[pos] != '\\')
        {
            str_buf[str_len++] = token[pos];
        }
        else
        {
            char ch;
            switch (token[++pos])
            {
            case 'n': ch = '\n'; break;
            default: ch = token[pos]; break;
            }
            str_buf[str_len++] = ch;
        }
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
    char *str = strdup(id);
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
    for (n = 0; n < AR_size(&func_params); ++n)
        free(*(char**)AR_at(&func_params, n));
    AR_clear(&func_params);
    func_nlocal = 0;
}

/* Parses a <preposition> <entity> sequence. */
static bool parse_command3(char *str, Command *command)
{
    const void *f_idx, *g_idx;
    const Fragment *f, *g;
    char *sep;
    bool ok;

    sep = str;
    while (*sep) ++sep;

    for (;;)
    {
        do --sep; while (sep > str && *sep != ' ');
        if (sep <= str) break;

        *sep = '\0';
        ok = ST_find(&st_fragments, str, &f_idx) &&
             (f = AR_at(&ar_fragments, (long)f_idx))->type == F_PREPOSITION &&
             ST_find(&st_fragments, sep + 1, &g_idx) &&
             (g = AR_at(&ar_fragments, (long)g_idx))->type == F_ENTITY;
        *sep = ' ';

        if (ok)
        {
            /* FORM 2: <verb> <entity> <preposition> <entity> */
            command->form = 2;
            command->part[2] = f->id;
            command->part[3] = g->id;
            return true;
        }
        *sep = ' ';
    }

    return false;
}

/* Parses the part after "verb" in a command.
   This either an entity reference, or an entity/preposition/reference. */
static bool parse_command2(char *str, Command *command)
{
    const void *f_idx;
    Fragment *f;
    char *sep;
    bool ok;

    if (ST_find(&st_fragments, str, &f_idx) &&
        (f = AR_at(&ar_fragments, (long)f_idx))->type == F_ENTITY)
    {
        /* FORM 1: <verb> <entity> */
        command->form = 1;
        command->part[1] = f->id;
        command->part[2] = -1;
        command->part[3] = -1;
        return true;
    }

    sep = str;
    while (*sep) ++sep;
    for (;;)
    {
        do --sep; while (sep > str && *sep != ' ');
        if (sep <= str) break;

        *sep = '\0';
        ok = ST_find(&st_fragments, str, &f_idx) &&
             (f = AR_at(&ar_fragments, (long)f_idx))->type == F_ENTITY &&
             parse_command3(sep + 1, command);
        *sep = ' ';

        if (ok)
        {
            command->part[1] = f->id;
            return true;
        }
    }

    return false;
}

static bool parse_command(char *str, Command *command)
{
    const void *f_idx;
    const Fragment *f;
    char *sep;
    bool ok;

    if (ST_find(&st_fragments, str, &f_idx) &&
        (f = AR_at(&ar_fragments, (long)f_idx))->type == F_VERB)
    {
        /* FORM 0: <verb> */
        command->form = 0;
        command->part[0] = f->id;
        command->part[1] = -1;
        command->part[2] = -1;
        command->part[3] = -1;
        return true;
    }

    sep = str;
    while (*sep) ++sep;
    for (;;)
    {
        do --sep; while (sep > str && *sep != ' ');
        if (sep <= str) break;

        *sep = '\0';
        ok = ST_find(&st_fragments, str, &f_idx) &&
             (f = AR_at(&ar_fragments, (long)f_idx))->type == F_VERB &&
             parse_command2(sep + 1, command);
        *sep = ' ';

        if (ok)
        {
            command->part[0] = f->id;
            return true;
        }
    }

    return false;
}

void begin_command(const char *str)
{
    char *fragment;
    Command command;

    fragment = strdup(str);
    normalize(fragment);
    assert(fragment != NULL);
    if (!parse_command(fragment, &command))
    {
        fatal("Could not parse command \"%s\" on line %d.",
            fragment, lineno + 1);
    }
    free(fragment);

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

void add_synonym(const char *token)
{
    const void *idx = (void*)AR_size(&ar_fragments);
    char *str = strdup(token);
    normalize(str);
    fragment.str = str;  /* will be re-allocated after ST_insert_entry() */
    if (ST_insert_entry(&st_fragments, (const void**)&fragment.str, &idx))
    {
        fatal("Redeclaration of fragment \"%s\" on line %d.",
            str, lineno + 1);
    }
    free(str);
    AR_append(&ar_fragments, &fragment);

    fragment.canon = false;  /* next fragment will be non-canonical */
}

int resolve_fragment(const char *token, int type)
{
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
    if (type != -1 && type != f->type)
    {
        fatal("Fragment referenced by \"%s\" has wrong type on line %d.",
            str, lineno + 1);
    }
    return f->id;
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
    int init_func = -1;
    const void *init_idx;
    if (ST_find(&st_functions, "initialize", &init_idx))
        init_func = (long)init_idx;

    return
        write_int32(fp, 32) &&      /* header size: 32 bytes */
        write_int16(fp, 0x0100) &&  /* file version: 1.0 */
        write_int16(fp, 0) &&       /* reserved */
        write_int32(fp, num_verbs) &&
        write_int32(fp, num_prepositions) &&
        write_int32(fp, num_entities) &&
        write_int32(fp, AR_size(&ar_properties)) &&
        write_int32(fp, AR_size(&ar_vars)) &&
        write_int32(fp, init_func);
}

static int cmp_fragments(const void *a, const void *b)
{
    const Fragment *f = a, *g =b;
    int d = strcmp(f->str, g->str);
    if (d == 0) d = f->type - f->type;
    if (d == 0) d = g->id - g->id;
    return d;
}

static bool write_alio_fragments(FILE *fp)
{
    int table_size, offset;
    Fragment *fragments;
    int nfragment, n, padding;

    nfragment = (int)AR_size(&ar_fragments);
    fragments = (Fragment*)AR_data(&ar_fragments);

    /* Sort fragments */
    qsort(fragments, nfragment, sizeof(Fragment), &cmp_fragments);

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
        int flags_type = fragments[n].type;
        if (fragments[n].canon) flags_type |= 0x10;
        if (!(write_int8(fp, flags_type) &&
              write_int24(fp, fragments[n].id) &&
              write_int32(fp, offset)))
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
        if (!(write_int16(fp, 0)))
            return false;
        if (!write_int8(fp, functions[n].nret))
            return false;
        if (!write_int8(fp, functions[n].nparam))
            return false;
        if (!write_int32(fp, offset))
            return false;
        offset += 4*(functions[n].ninstr + 1);
    }

    /* Write function instructions */
    for (n = 0; n < nfunction; ++n)
    {
        Instruction *ins = functions[n].instrs;
        for (i = 0; i < functions[n].ninstr; ++i)
        {
            assert(ins[i].opcode == (ins[i].opcode&255));
            assert(ins[i].argument >= -0x00800000 && ins[i].argument <= 0x007fffff);
            if (!(write_int8(fp, ins[i].opcode) && write_int24(fp,ins[i].argument)))
                return false;
        }
        if (!write_int32(fp, 0))
            return false;
    }

    return true;
}

static int cmp_commands(const void *a, const void *b)
{
    const Command *c = a, *d =b;
    if (c->form    - d->form    != 0) return c->form    - d->form;
    if (c->part[0] - d->part[0] != 0) return c->part[0] - d->part[0];
    if (c->part[1] - d->part[1] != 0) return c->part[1] - d->part[1];
    if (c->part[2] - d->part[2] != 0) return c->part[2] - d->part[2];
    if (c->part[3] - d->part[3] != 0) return c->part[3] - d->part[3];
    return 0;
}

static bool write_alio_commands(FILE *fp)
{
    int ncommand, total_args, n, m;
    int form_to_nargs[3] = { 1, 2, 4 };
    Command *commands;

    commands = AR_data(&ar_commands);
    ncommand = AR_size(&ar_commands);

    /* Sort commands */
    qsort(commands, ncommand, sizeof(Command), &cmp_commands);

    /* Compute and write command table size */
    total_args = 0;
    for (n = 0; n < ncommand; ++n)
        total_args += form_to_nargs[commands[n].form];
    if (!write_int32(fp, 8 + 12*ncommand + 4*total_args))
        return false;

    /* Write all commands */
    if (!write_int32(fp, ncommand))
        return false;
    for (n = 0; n < ncommand; ++n)
    {
        if (!write_int16(fp, commands[n].form)) return false;
        if (!write_int16(fp, form_to_nargs[commands[n].form])) return false;
        for (m = 0; m < form_to_nargs[commands[n].form]; ++m)
        {
            if (!write_int32(fp, commands[n].part[m])) return false;
        }
        if (!write_int32(fp, commands[n].guard)) return false;
        if (!write_int32(fp, commands[n].function)) return false;
    }

    return true;
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
        fatal("Unable to open output file \"%s\".", output_path);
    if (!write_alio(fp))
        fatal("Unable to write output file \"%s\".", output_path);
    fclose(fp);
}

void parser_create()
{
    /* Register built-in functions */
    const char **name;
    long func_id = 0;
    for (name = builtin_names; *name != NULL; ++name)
        ST_insert(&st_functions, *name, (void*)--func_id);
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
