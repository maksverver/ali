#include "debug.h"
#include "io.h"
#include "interpreter.h"
#include "opcodes.h"
#include "strings.h"
#include <string.h>

const Value val_true = 1, val_false = 0, val_nil = -1;

/* Limit on the size of the script's execution stack.
   Useful to prevent infinite recusion, but also necessary since the interpreter
   uses the C stack to invoke functions, so setting this value too high can
   crash the interpreter. */
#define MAX_STACK_SIZE 1000

/* Limit on the number of words in a command.
   Useful to keep parsing relatively efficient. */
#define MAX_COMMAND_WORDS 50

typedef Value (*Builtin)(Interpreter *I, int narg, Value *args);

static Value builtin_write   (Interpreter *I, int narg, Value *args);
static Value builtin_writeln (Interpreter *I, int narg, Value *args);
static Value builtin_writef  (Interpreter *I, int narg, Value *args);
static Value builtin_pause   (Interpreter *I, int narg, Value *args);
static Value builtin_quit    (Interpreter *I, int narg, Value *args);
static Value builtin_reset   (Interpreter *I, int narg, Value *args);

Builtin builtins[NUM_BUILTIN_FUNCS] = {
    builtin_write, builtin_writeln, builtin_writef,
    builtin_pause, builtin_quit, builtin_reset };

const char * const builtin_func_names[NUM_BUILTIN_FUNCS + 1] = {
    "write", "writeln", "writef", "pause", "quit", "reset", NULL };

const char * const builtin_var_names[NUM_BUILTIN_VARS + 1] = {
    "title",        "subtitle",     "RESERVED02",   "RESERVED03",
    "RESERVED04",   "RESERVED05",   "RESERVED06",   "RESERVED07", NULL };


/* Invokes a function.
   The initial height of the stack must be at least args.

   Stack lay-out on entry is as follows:
   +---+~ ~+------+----+~ ~+----+
   | 0 |   | func | a1 |   | aN |
   +---+~ ~+------+----+~ ~+----+
           |<------ nargs ----->|

   And on exit:
   +---+~ ~+----+----+~ ~+----+
   | 0 |   | r1 | r2 |   | rN |
   +---+~ ~+----+----+~ ~+----+
           |<----- nret ----->|

   i.e. the arguments including the function number are replaced by the
   results of the function.
*/
static void invoke(Interpreter *I, int nargs, int nret);

#if 0
static void stack_dump(FILE *fp, Array *stack)
{
    /*
            +----+----+
            | 12 | 34 |
            +----+----+
               1    2
    */
    int n;
    fprintf(fp, "  +");
    for (n = 0; n < (int)AR_size(stack); ++n)
        fprintf(fp, "----+");
    fprintf(fp, "\n");
    fprintf(fp, "  |");
    for (n = 0; n < (int)AR_size(stack); ++n)
        fprintf(fp, " %2d |", (int)*(Value*)AR_at(stack, n));
    fprintf(fp, "\n");
    fprintf(fp, "  +");
    for (n = 0; n < (int)AR_size(stack); ++n)
        fprintf(fp, "----+");
    fprintf(fp, "\n");
    fprintf(fp, "   ");
    for (n = 0; n < (int)AR_size(stack); ++n)
        fprintf(fp, " %2d  ", n);
    fprintf(fp, "\n");
}
#endif

static bool eq_word(const char *a, const char *b)
{
    while (*a && *b && *a == *b) ++a, ++b;
    return (*a == '\0' || *a == ' ') && (*b == '\0' || *b == ' ');
}

unsigned hash_word(const char *str)
{
    unsigned h = 2166136261u;
    while (*str != '\0' && *str != ' ')
    {
        h *= 16777619u;
        h ^= *(unsigned char*)str;
        ++str;
    }
    return h;
}

static bool skip(IOStream *ios, size_t size)
{
    while (size-- > 0)
        if (!read_int8(ios, NULL))
            return false;
    return true;
}

static size_t pad_chunk_size(size_t chunk_size)
{
    return chunk_size + (chunk_size&1);
}

/* Reads the next chunk header from the input stream. */
static bool begin_chunk(IOStream *ios, char *id, size_t *size)
{
    int i;
    if (read_data(ios, id, 4) && read_int32(ios, &i))
    {
        *size = i;
        return true;
    }
    return false;
}

/* Skips data until the start of the next chunk. */
static bool end_chunk(IOStream *ios, size_t chunk_size)
{
    return skip(ios, pad_chunk_size(chunk_size) - chunk_size);
}

static bool read_header(IOStream *ios, Module *mod, size_t size)
{
    int version;
    if (!read_int16(ios, &version))
        return false;

    if ((version&0xff00) != 0x0100)
    {
        error("Invalid module file version: %d.%d (expected: 1.x)",
              (version>>8)&0xff, version&0xff);
        return false;
    }

    return
        read_int16(ios, NULL) && /* skip reserved data */
        read_int32(ios, &mod->num_globals) &&
            mod->num_globals >= NUM_BUILTIN_VARS &&
        read_int32(ios, &mod->num_entities) &&
        read_int32(ios, &mod->num_properties) &&
        read_int32(ios, &mod->init_func) &&
        skip(ios, size - 20);
}

static bool read_strings( IOStream *ios, size_t size,
                          int *nstring, char ***strings, void **string_data )
{
    int entries, e;

    if (size < 4 || !read_int32(ios, &entries) || entries < 0)
        return false;
    size -= 4;

    if (entries == 0)
    {
        *nstring     = 0;
        *strings     = NULL;
        *string_data = NULL;
        return true;
    }

    *nstring     = entries;
    *strings     = malloc(entries*sizeof(char*));
    *string_data = malloc(size);

    /* Read zero-terminated string data */
    if (size == 0 || !read_data(ios, *string_data, size) ||
        ((char*)*string_data)[size - 1] != '\0')
        return false;

    char *p = *string_data;
    for (e = 0; e < entries; ++e)
    {
        if ((size_t)(p - (char*)*string_data) > size)
            return false;   /* no more string data */
        (*strings)[e] = p;
        p += strlen(p) + 1;
    }
    return true;
}

static bool read_string_table(IOStream *ios, Module *mod, size_t size)
{
    return read_strings(ios, size, &mod->nstring, &mod->strings, &mod->string_data);
}

static bool read_function_table(IOStream *ios, Module *mod, size_t size)
{
    int entries, ninstr, n;
    Instruction *instrs;

    if (size < 4 || size%4 != 0 || !read_int32(ios, &entries) ||
        entries < 0 || (size - 4)/8 < (size_t)entries)
        return false;
    size -= 4;

    if (entries == 0)
    {
        mod->nfunction     = 0;
        mod->functions     = NULL;
        mod->function_data = NULL;
        return true;
    }

    ninstr = (size - 4*entries)/4;
    if (ninstr <= 0)
        return false;
    instrs = malloc(ninstr*sizeof(Instruction));

    mod->nfunction     = entries;
    mod->functions     = malloc(entries*sizeof(Function));
    mod->function_data = (void*)instrs;

    for (n = 0; n < entries; ++n)
    {
        int nret, nparam;

        if (!read_int16(ios, NULL)) /* skip reserved bytes */
            return false;

        if (!read_int8(ios, &nret) || nret < 0)
            return false;
        if (!read_int8(ios, &nparam) || nparam < 0)
            return false;

        mod->functions[n].id     = n;
        mod->functions[n].ninstr = 0;  /* this is set to a real value below */
        mod->functions[n].nparam = nparam;
        mod->functions[n].nret   = nret;
    }

    /* Read instructions */
    int start = 0, entry = 0;
    for (n = 0; n < ninstr; ++n)
    {
        int opcode, argument;
        if (!read_int8(ios, &opcode) || !read_int24(ios, &argument))
            return false;
        instrs[n].opcode   = opcode;
        instrs[n].argument = argument;
        if (opcode == 0 && argument == 0)
        {
            if (entry >= entries)
                return false;
            fflush(stdout);
            mod->functions[entry].ninstr = n - start;
            mod->functions[entry].instrs = &instrs[start];
            entry += 1;
            start = n + 1;
        }
    }

    return entry == entries;
}

static bool read_word_table(IOStream *ios, Module *mod, size_t size)
{
    if (!read_strings(ios, size, &mod->nword, &mod->words, &mod->word_data))
        return false;

    /* ensure all words are non-empty and in canonical form */
    int n;
    for (n = 0; n < mod->nword; ++n)
        if (normalize(mod->words[n])[0] == '\0')
            return false;

    /* create hash-table index */
    mod->word_index_size = 2*mod->nword + 1;  /* FIXME: possible overflow here */
    mod->word_index      = malloc(sizeof(int)*mod->word_index_size);
    size_t i;
    for (i = 0; i < mod->word_index_size; ++i)
        mod->word_index[i] = -1;
    for (n = 0; n < mod->nword; ++n)
    {
        i = hash_word(mod->words[n])%mod->word_index_size;
        while (mod->word_index[i] >= 0)
            if (++i == mod->word_index_size)
                i = 0;
        mod->word_index[i] = n;
    }

    return true;
}

static bool parse_symref(Module *mod, int i, SymbolRef *ref)
{
    if (i < 0 && i >= -mod->nword)
    {
        ref->type  = SYM_TERMINAL;
        ref->index = -1 - i;
        return true;
    }

    if (i > 0 && i <= mod->nsymbol)
    {
        ref->type  = SYM_NONTERMINAL;
        ref->index = i - 1;
        return true;
    }

    return false;
}

static bool read_grammar_table(IOStream *ios, Module *mod, size_t size)
{
    int nnonterm, tot_rules, tot_symrefs;
    if (size < 12 || size%4 != 0 ||
        !read_int32(ios, &nnonterm)    || nnonterm    < 0 ||
        !read_int32(ios, &tot_rules)   || tot_rules   < 0 ||
        !read_int32(ios, &tot_symrefs) || tot_symrefs < 0)
        return false;
    size -= 12;
    if (size/4 != (size_t)nnonterm + tot_rules + tot_symrefs)  /* FIXME: possible overflow here */
        return false;

    /* FIXME: this assumes all structs are aligned to similar boundaries. */
    size_t data_size = sizeof(GrammarRuleSet)*nnonterm +
                       sizeof(SymbolRefList*)*tot_rules +
                       sizeof(SymbolRefList)*tot_rules +
                       sizeof(SymbolRef)*tot_symrefs;
    char *data = malloc(data_size);
    if (data == NULL)
        return false;

    mod->nsymbol      = nnonterm;
    mod->symbol_rules = (GrammarRuleSet*)data;
    data += sizeof(GrammarRuleSet)*nnonterm;

    SymbolRefList **ruleptr_data = (SymbolRefList**)data;
    data += sizeof(SymbolRefList*)*tot_rules;

    SymbolRefList *rule_data = (SymbolRefList*)data;
    data += sizeof(SymbolRefList)*tot_rules;

    SymbolRef *symref_data = (SymbolRef*)data;
    data += sizeof(SymbolRef)*tot_symrefs;

    int n, r, s;
    for (r = 0; r < tot_rules; ++r)
        ruleptr_data[r] = rule_data + r;
    for (n = 0; n < nnonterm; ++n)
    {
        int nrule;
        if (!read_int32(ios, &nrule) || nrule < 0 || nrule > tot_rules)
            return false;
        mod->symbol_rules[n].sym.type  = SYM_NONTERMINAL;
        mod->symbol_rules[n].sym.index = n;
        mod->symbol_rules[n].nrule = nrule;
        mod->symbol_rules[n].rules = ruleptr_data;
        ruleptr_data += nrule;
        tot_rules -= nrule;

        for (r = 0; r < nrule; ++r)
        {
            int nref;
            if (!read_int32(ios, &nref) || nref < 0 || nref > tot_symrefs)
                return false;

            mod->symbol_rules[n].rules[r]->nref = nref;
            mod->symbol_rules[n].rules[r]->refs = symref_data;
            symref_data += nref;
            tot_symrefs -= nref;

            for (s = 0; s < nref; ++s)
            {
                SymbolRef ref;
                int i;
                if (!read_int32(ios, &i) || !parse_symref(mod, i, &ref))
                    return false;
                if (ref.type == SYM_NONTERMINAL && ref.index >= n)
                    return false;  /* no recursive rules allowed yet! */
                mod->symbol_rules[n].rules[r]->refs[s] = ref;
            }
        }
    }

    /* Compute nullability */
    mod->symbol_nullable = malloc(mod->nsymbol * sizeof(bool));
    if (mod->symbol_nullable == NULL)
        return false;

    /* NB: the current algorithm only works because we recursive rules and
           forward references are forbidden! */
    for (n = 0; n < mod->nsymbol; ++n)
    {
        mod->symbol_nullable[n] = false;

        /* Find a nullable rule */
        for (r = 0; (size_t)r < mod->symbol_rules[n].nrule; ++r)
        {
            for (s = 0; (size_t)s < mod->symbol_rules[n].rules[r]->nref; ++s)
            {
                SymbolRef *ref = &mod->symbol_rules[n].rules[r]->refs[s];
                if (ref->type == SYM_TERMINAL)
                    goto non_null;
                assert(ref->type == SYM_NONTERMINAL);
                if (!mod->symbol_nullable[ref->index])
                    goto non_null;
            }
            mod->symbol_nullable[n] = true;
            break;
        non_null:
            continue;
        }
    }

    return true;
}

static bool read_command_table(IOStream *ios, Module *mod, size_t size)
{
    int command_sets;
    if (size < 4 || !read_int32(ios, &command_sets) || command_sets < 1)
        return false;
    size -= 4;

    if (size/4 < (size_t)command_sets)
        return false;

    /* Parse only first command set for now: */
    if (size < 4 || !read_int32(ios, &mod->ncommand))
        return false;
    size -= 4;
    if (size/12 < (size_t)mod->ncommand)
        return false;

    mod->commands = malloc(mod->ncommand * sizeof(Command));
    if (mod->commands == NULL)
        return false;

    int n;
    for (n = 0; n < mod->ncommand; ++n)
    {
        int i;
        if (!read_int32(ios, &i) ||
            !parse_symref(mod, i, &mod->commands[n].symbol) ||
            !read_int32(ios, &mod->commands[n].guard) ||
            !read_int32(ios, &mod->commands[n].function))
            return false;
    }
    return true;
}

void free_module(Module *mod)
{
    /* Free string table */
    free(mod->strings);
    mod->strings = NULL;
    free(mod->string_data);
    mod->string_data = NULL;

    /* Function table */
    free(mod->functions);
    mod->functions = NULL;
    free(mod->function_data);
    mod->function_data = NULL;

    /* Free word table */
    free(mod->words);
    mod->words = NULL;
    free(mod->word_data);
    mod->word_data = NULL;
    free(mod->word_index);
    mod->word_index = NULL;

    /* Free grammar table */
    free(mod->symbol_rules);
    mod->symbol_rules = NULL;
    free(mod->symbol_nullable);
    mod->symbol_nullable = NULL;

    /* Free command table */
    free(mod->commands);
    mod->commands = NULL;
}

Module *load_module(IOStream *ios)
{
    Module *mod = malloc(sizeof(Module));
    if (mod == NULL)
        return NULL;
    memset(mod, 0, sizeof(Module));

    const char *chunk_types[7] = {
        "FORM", "MOD ", "STR ", "FUN ", "WRD ", "GRM ", "CMD " };

    int chunk;
    for (chunk = 0; chunk < 7; ++chunk)
    {
        char chunk_type[4];
        size_t chunk_size;

        if (!begin_chunk(ios, chunk_type, &chunk_size))
        {
            error("Unable to read chunk header.");
            goto failed;
        }

        if (memcmp(chunk_type, chunk_types[chunk], 4) != 0)
        {
            error("Expected %.4s chunk!", chunk_types[chunk]);
            goto failed;
        }

        switch (chunk)
        {
        case 0: /* FORM */
            {
                char id[4];
                if (!read_data(ios, id, 4) || memcmp(id, "ALI ", 4) != 0)
                {
                    error("Unsupported FORM type (%.4s); expected ALI.", id);
                    goto failed;
                }
            } break;

        case 1: /* MOD  */
            if (!read_header(ios, mod, chunk_size))
            {
                error("Failed to load module header.");
                goto failed;
            }
            break;

        case 2: /* STR  */
            if (!read_string_table(ios, mod, chunk_size))
            {
                error("Failed to read module string table.");
                goto failed;
            }
            break;

        case 3: /* FUN  */
            if (!read_function_table(ios, mod, chunk_size))
            {
                error("Failed to read module function table.");
                goto failed;
            }
            break;

        case 4: /* WRD  */
            if (!read_word_table(ios, mod, chunk_size))
            {
                error("Failed to read module word table.");
                goto failed;
            }
            break;

        case 5: /* GRM  */
            if (!read_grammar_table(ios, mod, chunk_size))
            {
                error("Failed to read module grammar table.");
                goto failed;
            }
            break;

        case 6: /* CMD  */
            if (!read_command_table(ios, mod, chunk_size))
            {
                error("Failed to read module command table.");
                goto failed;
            }
            break;
        }

        if (!end_chunk(ios, chunk_size))
        {
            error("Unable to read chunk footer.");
            goto failed;
        }
    }

    return mod;

failed:
    free_module(mod);
    return NULL;
}

Variables *alloc_vars(Module *mod)
{
    int nval = mod->num_entities*mod->num_properties + mod->num_globals;

    /* Allocate memory for variables */
    Variables *vars = malloc(sizeof(Variables) + nval*sizeof(Value));
    if (vars == NULL)
        return NULL;

    /* Initialize */
    vars->nval = nval;
    vars->vals = (void*)((char*)vars + sizeof(Variables));
    clear_vars(vars);

    return vars;
}

void free_vars(Variables *vars)
{
    free(vars);
}

void clear_vars(Variables *vars)
{
    int n;
    for (n = 0; n < vars->nval; ++n)
        vars->vals[n] = val_nil;
}

Variables *dup_vars(Variables *vars)
{
    Variables *new_vars = malloc(sizeof(Variables) + vars->nval*sizeof(Value));
    if (new_vars == NULL)
        return NULL;

    memcpy(new_vars, vars, sizeof(Variables) + vars->nval*sizeof(Value));
    return new_vars;
}

int cmp_vars(Variables *vars1, Variables *vars2)
{
    assert(vars1->nval == vars2->nval);
    return memcmp(vars1->vals, vars2->vals, vars1->nval*sizeof(Value));
}

static void push_stack(Array *stack, Value value)
{
    if (AR_size(stack) == MAX_STACK_SIZE)
        fatal("Stack limit exceeded when pushing a value.");
    AR_push(stack, &value);
}

void reinitialize(Interpreter *I)
{
    /* Reset variables */
    clear_vars(I->vars);

    /* Call initialization function (if we have one) */
    if (I->mod->init_func != -1)
    {
        push_stack(I->stack, (Value)I->mod->init_func);
        invoke(I, 1, 0);
    }
}

static Value exec_function(Interpreter *I, const Function *f, int stack_base)
{
    const Instruction *i = f->instrs;
    Value val, val2;

    /* Interpreter loop */
    while (i >= f->instrs && i < f->instrs + f->ninstr)
    {
        int opcode      = i->opcode;
        int argument    = i->argument;
        int frame_size  = (int)AR_size(I->stack) - stack_base;
        /* info("Instruction %d", i - (Instruction*)I->mod->function_data); */
        ++i;

        switch(opcode)
        {
        case OP_LLI:
            push_stack(I->stack, (Value)argument);
            break;

        case OP_POP:
            if (argument < 0 || argument > frame_size)
                goto invalid;
            AR_resize(I->stack, AR_size(I->stack) - argument);
            break;

        case OP_LDL:
            if (argument < 0 || argument >= frame_size)
                goto invalid;
            push_stack(I->stack, *(Value*)AR_at(I->stack, stack_base + argument));
            break;

        case OP_STL:
            if (argument < 0 || argument >= frame_size - 1)
                goto invalid;
            AR_pop(I->stack, &val);
            *(Value*)AR_at(I->stack, stack_base + argument) = val;
            break;

        case OP_LDG:
            if (argument < 0 || argument >= I->vars->nval)
                goto invalid;
            push_stack(I->stack, I->vars->vals[argument]);
            break;

        case OP_STG:
            if (argument < 0 || argument >= I->vars->nval)
                goto invalid;
            AR_pop(I->stack, &I->vars->vals[argument]);
            break;

        case OP_LDI:
            {
                int index;
                if (frame_size < 1)
                    goto invalid;
                AR_pop(I->stack, &val);
                index = I->mod->num_globals
                      + I->mod->num_properties*(int)val
                      + argument;
                if (index < 0 || index >= I->vars->nval)
                    goto invalid;
                push_stack(I->stack, I->vars->vals[index]);
            } break;

        case OP_STI:
            {
                int index;
                Value *v;
                if (frame_size < 2)
                    goto invalid;
                v = (Value*)AR_at(I->stack, AR_size(I->stack) - 2);
                index = I->mod->num_globals
                      + I->mod->num_properties*(int)v[0]
                      + argument;
                if (index < 0 || index >= I->vars->nval)
                    goto invalid;
                I->vars->vals[index] = v[1];
                AR_resize(I->stack, AR_size(I->stack) - 2);
            } break;

        case OP_JNP:
            if (frame_size < 1)
                goto invalid;
            AR_pop(I->stack, &val);
            if (!VAL_TO_BOOL(val))
                i += argument;
            break;

        case OP_JMP:
            i += argument;
            break;

        case OP_OP1:
            if (frame_size < 1)
                goto invalid;
            AR_pop(I->stack, &val);
            switch (argument)
            {
            case OP1_NOT:
                val = VAL_TO_BOOL(val) ? val_false : val_true;
                break;
            default:
                goto invalid;
            }
            AR_push(I->stack, &val);
            break;

        case OP_OP2:
            if (AR_size(I->stack) - stack_base < 2)
                goto invalid;
            AR_pop(I->stack, &val);
            AR_pop(I->stack, &val2);
            switch (argument)
            {
            case OP2_AND:
                val = BOOL_TO_VAL(VAL_TO_BOOL(val) && VAL_TO_BOOL(val2));
                break;
            case OP2_OR:
                val = BOOL_TO_VAL(VAL_TO_BOOL(val) || VAL_TO_BOOL(val2));
                break;
            case OP2_EQ:
                val = BOOL_TO_VAL(val == val2);
                break;
            case OP2_NEQ:
                val = BOOL_TO_VAL(val != val2);
                break;
            default:
                goto invalid;
            }
            AR_push(I->stack, &val);
            break;

        case OP_OP3:
            if (AR_size(I->stack) - stack_base < 3)
                goto invalid;
            switch (argument)
            {
            default:
                goto invalid;
            }
            break;

        case OP_CAL:
            if (argument%256 > frame_size)
                goto invalid;
            invoke(I, argument%256, argument/256);
            break;

        case OP_RET:
            if (argument > frame_size)
                goto invalid;
            switch (argument)
            {
            case 0:
                return val_nil;
            case 1:
                AR_pop(I->stack, &val);
                return val;
            default:
                goto invalid;
            }

        default:
            goto invalid;
        }
    }

invalid:
    fatal("Instruction %d (opcode %d, argument: %d) could not be executed.\n"
          "Stack frame size was %d (%d - %d).",
        i - (Instruction*)I->mod->function_data - 1,
        (i - 1)->opcode, (i - 1)->argument,
        AR_size(I->stack) - stack_base, AR_size(I->stack), stack_base);
    return val_nil;
}

static void invoke(Interpreter *I, int nargs, int nret)
{
    int func_id;
    int stack_base;
    Value result;

    if (nargs <= 0 || nargs > (int)AR_size(I->stack))
        fatal("Invalid number of arguments for function call "
              "(%d; stack height is %d)", nargs, (int)AR_size(I->stack));

    if (nret < 0 || nret > 1)
        fatal("Invalid number of return values for function call (%d)", nret);

    /* This currently can't happen since nargs >= 1 and nret <= 1:
    if (AR_size(I->stack) - nargs + nret > MAX_STACK_SIZE)
        fatal("Stack limit exceeded when invoking a function.");
    */

    /* Figure out which function to call */
    func_id = (int)*(Value*)AR_at(I->stack, AR_size(I->stack) - nargs);
    nargs -= 1;
    stack_base = AR_size(I->stack) - nargs;

    if (func_id < 0)
    {
        func_id = -func_id - 1;
        if (func_id > NUM_BUILTIN_FUNCS)
            fatal("Invalid system call (%d).", func_id);
        result = builtins[func_id](I, nargs,
            (Value*)AR_data(I->stack) + AR_size(I->stack) - nargs);
    }
    else
    if (func_id >= I->mod->nfunction)
    {
        error("Non-existent function %d invoked!", func_id);
        result = val_nil;
    }
    else
    {
        const Function *f = &I->mod->functions[func_id];

        /* Check number of arguments and adjust stack frame if necessary */
        if (nargs != f->nparam)
        {
            warn("Function %d has %d parameters, but was invoked with %d arguments!",
                func_id, f->nparam, nargs);

            /* Add arguments if arguments < parameters */
            for ( ; nargs < f->nparam; ++nargs)
                AR_push(I->stack, &val_nil);

            /* Remove arguments if arguments > parameters */
            for ( ; nargs > f->nparam; --nargs)
                AR_pop(I->stack, NULL);
        }

        result = exec_function(I, f, stack_base);

        /* Check number of return values */
        if (nret != f->nret)
        {
            warn("Function %d returns %d values, but caller expects %d values!",
                func_id, f->nret, nret);
        }
    }

    /* Remove arguments and function id */
    AR_resize(I->stack, stack_base - 1);

    /* Add return value, if requested */
    if (nret == 1)
        AR_push(I->stack, &result);
}

static const char *get_string(const Interpreter *I, Value v)
{
    if (v == val_nil)
        return "(nil)";

    int n = (int)v;
    if (n < 0 || n >= I->mod->nstring)
        return "(err)";

    return I->mod->strings[n];
}

static void write_ch(Interpreter *I, char ch)
{
    AR_push(I->output, &ch);
}

static void write_buf(Interpreter *I, const char *i, const char *j)
{
    while (i != j) write_ch(I, *i++);
}

static void write_str(Interpreter *I, const char *s)
{
    while (*s != '\0') write_ch(I, *s++);
}

static Value builtin_write(Interpreter *I, int narg, Value *args)
{
    int n;
    for (n = 0; n < narg; ++n)
    {
        write_ch(I, ' ');
        write_str(I, get_string(I, args[n]));
    }
    return val_nil;
}

static Value builtin_writeln(Interpreter *I, int narg, Value *args)
{
    builtin_write(I, narg, args);
    write_ch(I, '\n');
    return val_nil;
}

static Value builtin_writef(Interpreter *I, int narg, Value *args)
{
    if (narg == 0)
    {
        error("writef() called without arguments");
        return val_nil;
    }

    int a = 1;
    const char *p = get_string(I, args[0]);
    while (*p != '\0')
    {
        /* Do substitutions */
        while (*p == '%')
        {
            ++p;  /* skip escape character */

            if (*p == '\0')
            {
                /* Single % at the end of a string */
                write_ch(I, '%');
                break;
            }

            if (*p == '%')
            {
                /* Write literal percent sign */
                write_ch(I, '%');
            }
            else
            if (*p == 'd' || *p == 'i')
            {
                if (a == narg)
                {
                    warn("Too few arguments in call to writef()");
                }
                else
                {
                    /* Write integer argument */
                    int i = (int)args[a++];
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d", i);
                    write_str(I, buf);
                }
            }
            else
            if (*p == 's')
            {
                if (a == narg)
                {
                    warn("Too few arguments in call to writef()");
                }
                else
                {
                    /* Write string argument */
                    write_str(I, get_string(I, args[a++]));
                }
            }

            ++p;  /* skip formatting character too */
        }

        const char *q = p;
        while (*q != '%' && *q != '\0') ++q;
        write_buf(I, p, q);
        p = q;
    }

    if (a < narg)
        warn("Too many arguments in call to writef()");

    return val_nil;
}

static Value builtin_pause(Interpreter *I, int narg, Value *args)
{
    (void)args;  /* unused */
    if (narg > 0)
        warn("Arguments to pause() ignored.\n");
    if (I->callbacks != NULL && I->callbacks->pause != NULL)
        (*I->callbacks->pause)(I);
    return val_nil;
}

static Value builtin_quit(Interpreter *I, int narg, Value *args)
{
    (void)args;  /* unused */
    if (narg > 0)
        warn("Arguments to quit() ignored.\n");
    if (I->callbacks != NULL && I->callbacks->quit != NULL)
        (*I->callbacks->quit)(I, 0);
    return val_nil;
}

static Value builtin_reset(Interpreter *I, int narg, Value *args)
{
    (void)args;  /* unused */
    if (narg > 0)
        warn("Arguments to reset() ignored.\n");
    clear_vars(I->vars);
    return val_nil;
}

static bool evaluate_function(Interpreter *I, int func)
{
    if (func < 0 || func >= I->mod->nfunction)
        return false;

    Value val;
    push_stack(I->stack, (Value)func);
    invoke(I, 1, 1);
    AR_pop(I->stack, &val);
    return VAL_TO_BOOL(val);
}

/* Match the first word of the given line, and return an index into the word
   table if found, or -1 if not found. */
static int match_word(Module *mod, const char *line)
{
    size_t i = hash_word(line)%mod->word_index_size;
    while (mod->word_index[i] >= 0)
    {
        if (eq_word(line, mod->words[mod->word_index[i]]))
            return mod->word_index[i];
        if (++i == mod->word_index_size)
            i = 0;
    }
    return -1;
}

void process_command(Interpreter *I, char *line)
{
    AR_clear(I->output);

    /* Tokenize command string, into a list of indices into the word table. */
    int words[MAX_COMMAND_WORDS], nword = 0;
    const char *pos;
    for (pos = line; *pos != '\0'; )
    {
        if (nword == MAX_COMMAND_WORDS)
        {
            write_str(I, "Too many words in command!\n");
            return;
        }
        int i = match_word(I->mod, pos);
        if (i < 0)
        {
            write_str(I, "Unknown word: ");
            while (*pos != '\0' && *pos != ' ')
                write_ch(I, *pos++);
            return;
        }
        while (*pos != ' ' && *pos != '\0') ++pos;
        if (*pos != '\0') ++pos;
        words[nword++] = i;
    }

    /* Find matching commands. */
    const GrammarRuleSet *grammar = I->mod->symbol_rules;
    int num_matched = 0, num_active = 0, cmd_func = -1, n;
    for (n = 0; n < I->mod->ncommand; ++n)
    {
        const Command *command = &I->mod->commands[n];
        if (parse_dumb(grammar, words, nword, &command->symbol))
        {
            ++num_matched;
            if (command->guard < 0 || evaluate_function(I, command->guard))
            {
                if (++num_active == 1)
                    cmd_func = command->function;
            }
        }
    }

    if (num_matched == 0)
    {
        write_str(I, "You can't do that in this game.\n");
        return;
    }

    if (num_active == 0)
    {
        write_str(I, "That's not possible right now.\n");
        return;
    }

    if (num_active > 1)
    {
        write_str(I, "That command is ambiguous.\n");
        return;
    }

    /* Invoke the command function */
    push_stack(I->stack, (Value)cmd_func);
    invoke(I, 1, 0);
}
