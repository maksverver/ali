#include "debug.h"
#include "opcodes.h"
#include "interpreter.h"
#include "Array.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

typedef int Value;
static const Value val_true = 1, val_false = 0, val_nil = -1;

typedef struct Module
{
    int num_verbs, num_prepositions, num_entities,
        num_properties, num_globals, init_func;

    /* Fragment table */
    int nfragment;
    Fragment *fragments;
    void *fragment_data;

    /* String table */
    int nstring;
    char **strings;
    void *string_data;

    /* Function table */
    int nfunction;
    Function *functions;
    void **function_data;

} Module;

typedef struct State
{
    struct Module *mod;
    Value *vars;
    int nvar;
} State;

typedef Value (*Builtin)(State *state, Array *stack, int narg, Value *args);

Value builtin_quit(State *state, Array *stack, int narg, Value *args);
Value builtin_write(State *state, Array *stack, int narg, Value *args);

#define NBUILTIN 2
Builtin builtins[NBUILTIN] = {
    builtin_quit, builtin_write };



/* Invokes a function.
   The initial height of the stack must be at least args.

   Stack lay-out on entry is as follows:
   +---+~ ~+------+----+~ ~+--+
   | 0 |   | func | a1 |   |aN|   args == n + 1
   +---+~ ~+------+----+~ ~+--+
           |<--- args -------->

   And on exit:
   +---+~ ~+--------+
   | 0 |   | result |
   +---+~ ~+--------+

   i.e. the arguments including the function are replaced by the
   result of the function.
*/
void invoke(State *state, Array *stack, int args);

void stack_dump(FILE *fp, Array *stack)
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


int read_int8(FILE *fp)
{
    return (signed char)fgetc(fp);
}

int read_int16(FILE *fp)
{
    return ((signed char)fgetc(fp) << 8) | fgetc(fp);
}

int read_int24(FILE *fp)
{
    return ((signed char)fgetc(fp) << 16) | (fgetc(fp) << 8) | fgetc(fp);
}

int read_int32(FILE *fp)
{
    return ((signed char)fgetc(fp) << 24) | (fgetc(fp) << 16) | (fgetc(fp) << 8) | fgetc(fp);
}

static void skip(FILE *fp, int size)
{
    while (size-- > 0)
        fgetc(fp);
}

static bool read_header(FILE *fp, Module *mod)
{
    int size;
    int version;

    size = read_int32(fp);
    if (size < 32)
        return false;

    version = read_int16(fp);
    if ((version&0xff00) != 0x0100)
    {
        error("Invalid module file version: %d.%d (expected: 1.x)",
              (version>>8)&0xff, version&0xff);
        return false;
    }
    read_int16(fp); /* skip reserved data */

    mod->num_verbs        = read_int32(fp);
    mod->num_prepositions = read_int32(fp);
    mod->num_entities     = read_int32(fp);
    mod->num_properties   = read_int32(fp);
    mod->num_globals      = read_int32(fp);
    mod->init_func        = read_int32(fp);

    skip(fp, size - 32);

    return true;
}

static bool read_fragment_table(FILE *fp, Module *mod)
{
    int size, entries, n;

    size = read_int32(fp);
    if (size < 8)
        return false;
    entries = read_int32(fp);
    if ((size - 8)/8 < entries)
        return false;

    if (entries == 0)
    {
        mod->nfragment     = entries;
        mod->fragments     = NULL;
        mod->fragment_data = NULL;
        return true;
    }

    mod->nfragment     = entries;
    mod->fragments     = malloc(entries*sizeof(Fragment));
    mod->fragment_data = malloc(size - (8 + 8*entries));

    for (n = 0; n < entries; ++n)
    {
        int type, id, offset;

        type = read_int8(fp);
        id   = read_int24(fp);
        if (id < 0 || id > (type == F_VERB ? mod->num_verbs :
                            type == F_PREPOSITION ? mod->num_prepositions :
                            type == F_ENTITY ? mod->num_entities : -1) )
            return false;

        offset = read_int32(fp);
        if (offset < 8 + 8*entries || offset >= size)
            return false;
        offset -= 8 + 8*entries;

        mod->fragments[n].type = type;
        mod->fragments[n].id   = type;
        mod->fragments[n].str  = (char*)mod->fragment_data + offset;
    }

    size -= 8 + 8*entries;
    if (size <= 0)
        return false;
    if (fread(mod->fragment_data, 1, size, fp) != size)
        return false;
    if (((char*)mod->fragment_data)[size - 1] != '\0')
        return false;

    return true;
}

static bool read_string_table(FILE *fp, Module *mod)
{
    int size, entries, n;

    size = read_int32(fp);
    if (size < 8)
        return false;
    entries = read_int32(fp);
    if ((size - 8)/4 < entries)
        return false;

    if (entries == 0)
    {
        mod->nstring     = entries;
        mod->strings     = NULL;
        mod->string_data = NULL;
        return true;
    }

    mod->nstring     = entries;
    mod->strings     = malloc(entries*sizeof(char*));
    mod->string_data = malloc(size - (8 + 4*entries));

    for (n = 0; n < entries; ++n)
    {
        int offset = read_int32(fp);
        if (offset < 8 + 4*entries || offset >= size)
            return false;
        mod->strings[n] = (char*)mod->string_data + offset - (8 + 4*entries);
    }

    size -= 8 + 4*entries;
    if (size <= 0)
        return false;
    if (fread(mod->string_data, 1, size, fp) != size)
        return false;
    if (((char*)mod->string_data)[size - 1] != '\0')
        return false;

    return true;
}

static bool read_function_table(FILE *fp, Module *mod)
{
    int size, entries, ninstr, n;
    Instruction *instrs;

    size = read_int32(fp);
    if (size < 8 || size%4 != 0)
        return false;
    entries = read_int32(fp);
    if ((size - 8)/4 < entries)
        return false;

    if (entries == 0)
    {
        mod->nfunction     = entries;
        mod->functions     = NULL;
        mod->function_data = NULL;
        return true;
    }

    ninstr = (size - (8 + 8*entries))/4;
    if (ninstr <= 0)
        return false;
    instrs = malloc(ninstr*sizeof(Instruction));

    mod->nfunction     = entries;
    mod->functions     = malloc(entries*sizeof(Function));
    mod->function_data = (void*)instrs;

    for (n = 0; n < entries; ++n)
    {
        int nparam, offset;

        read_int24(fp); /* Skip reserved bytes */
        nparam = read_int8(fp);
        if (nparam < 0)
            return false;

        offset = read_int32(fp);
        if (offset < 8 + 8*entries || offset >= size || offset%4 != 0)
            return false;

        mod->functions[n].id     = n;
        mod->functions[n].ninstr = 0;  /* this is set to a real value below */
        mod->functions[n].nparam = nparam;
        mod->functions[n].instrs = (Instruction*)mod->function_data + (offset - (8 + 8*entries))/4;
    }

    /* Read instructions */
    for (n = 0; n < ninstr; ++n)
    {
        instrs[n].opcode   = read_int8(fp);
        instrs[n].argument = read_int24(fp);
    }
    if (instrs[ninstr - 1].opcode != 0 || instrs[ninstr - 1].argument != 0)
        return false;

    /* Determine out number of instructions in each function */
    for (n = 0; n < entries; ++n)
    {
        Instruction *i = mod->functions[n].instrs;
        while (i->opcode != 0 || i->argument != 0)
            ++i;
        mod->functions[n].ninstr = i - mod->functions[n].instrs;
    }

    return true;
}

static bool read_command_table(FILE *fp, Module *mod)
{
    /* TODO */
    return true;
}

void free_module(Module *mod)
{
    free(mod->fragments);
    mod->fragments = NULL;
    free(mod->fragment_data);
    mod->fragment_data = NULL;
    free(mod->strings);
    mod->strings = NULL;
    free(mod->string_data);
    mod->string_data = NULL;
    free(mod->functions);
    mod->functions = NULL;
    free(mod->function_data);
    mod->function_data = NULL;
    free(mod);
}

Module *load_module(FILE *fp)
{
    Module *mod = malloc(sizeof(Module));
    int sig = read_int32(fp);

    memset(mod, 0, sizeof(Module));

    if (ferror(fp))
    {
        error("Unable to read from module file.");
        goto failed;
    }

    if (sig != 0x616c696f)
    {
        error("Module file has incorrect signature.");
        goto failed;
    }

    if (!read_header(fp, mod) || ferror(fp))
    {
        error("Failed to load module header.");
        goto failed;
    }

    if (!read_fragment_table(fp, mod) || ferror(fp))
    {
        error("Failed to read module fragment table.");
        goto failed;
    }

    if (!read_string_table(fp, mod) || ferror(fp))
    {
        error("Failed to read module string table.");
        goto failed;
    }

    if (!read_function_table(fp, mod) || ferror(fp))
    {
        error("Failed to read module function table.");
        goto failed;
    }

    if (!read_command_table(fp, mod) || ferror(fp))
    {
        error("Failed to read module command table.");
        goto failed;
    }

    return mod;

failed:
    free_module(mod);
    return NULL;
}

State *make_state(Module *mod)
{
    State *state = malloc(sizeof(State));

    state->nvar = mod->num_entities*mod->num_properties + mod->num_globals;
    state->vars = malloc(state->nvar*sizeof(Value));
    state->mod  = mod;

    return state;
}

void free_state(State *state)
{
    free(state->vars);
    free(state);
}

Value exec_function(State *state, Array *stack, const Function *f, int stack_base)
{
    const Instruction *i, *j;
    Value val, val2;

    /* Interpreter loop */
    for (i = f->instrs; ;)
    {
        int opcode   = i->opcode;
        int argument = i->argument;
        /* info("Instruction %d", i - (Instruction*)state->mod->function_data); */
        ++i;

        switch(opcode)
        {
        case OP_LLI:
            val = (Value)argument;
            AR_push(stack, &val);
            break;

        case OP_POP:
            if (argument < 0 || stack_base + argument > AR_size(stack))
                goto invalid;
            AR_resize(stack, AR_size(stack) - argument);
            break;

        case OP_LDL:
            if (argument < 0 || stack_base + argument >= AR_size(stack))
                goto invalid;
            AR_push(stack, AR_at(stack, stack_base + argument));
            break;

        case OP_STL:
            if (argument < 0 || stack_base + argument >= AR_size(stack) - 1)
                goto invalid;
            AR_pop(stack, &val);
            *(Value*)AR_at(stack, stack_base + argument) = val;
            break;

        case OP_LDG:
            if (argument < 0 || argument >= state->nvar)
                goto invalid;
            AR_push(stack, &state->vars[argument]);
            break;

        case OP_STG:
            if (argument < 0 || argument >= state->nvar)
                goto invalid;
            AR_pop(stack, &state->vars[argument]);
            break;

        case OP_LDI:
            {
                int index;
                if (AR_size(stack) - stack_base < 1)
                    goto invalid;
                AR_pop(stack, &val);
                index = state->mod->num_globals
                      + state->mod->num_properties*(int)val
                      + argument;
                if (index < 0 || index >= state->nvar)
                    goto invalid;
                AR_push(stack, state->vars + index);
            } break;

        case OP_STI:
            {
                int index;
                Value *v;
                if (AR_size(stack) - stack_base < 2)
                    goto invalid;
                v = (Value*)AR_at(stack, AR_size(stack) - 2);
                index = state->mod->num_globals
                      + state->mod->num_properties*(int)v[0]
                      + argument;
                if (index < 0 || index >= state->nvar)
                    goto invalid;
                state->vars[index] = v[1];
                AR_resize(stack, AR_size(stack) - 2);
            } break;

        case OP_JNP:
            if (AR_size(stack) - stack_base < 1)
                goto invalid;
            AR_pop(stack, &val);
            /* FIXME: is (val != val_false && val != val_nil) a better criterion? */
            if (val > 0)
                break;
            /* NOTE: intentionally falls through! */
        case OP_JMP:
            j = i + argument;
            if (j < f->instrs || j >= f->instrs + f->ninstr)
                goto invalid;
            i = j;
            break;

        case OP_OP1:
            if (AR_size(stack) - stack_base < 1)
                goto invalid;
            AR_pop(stack, &val);
            switch (argument)
            {
            case OP1_NOT:
                /* FIXME: is this right? (Must stay in sync with OP_JNP above!) */
                val = (val > 0) ? val_false : val_true;
                break;
            default:
                goto invalid;
            }
            AR_push(stack, &val);
            break;

        case OP_OP2:
            if (AR_size(stack) - stack_base < 2)
                goto invalid;
            AR_pop(stack, &val);
            AR_pop(stack, &val2);
            switch (argument)
            {
            case OP2_AND:
                val = (val && val2) ? val_true : val_false;
                break;
            case OP2_OR:
                val = (val || val2) ? val_true : val_false;
                break;
            case OP2_EQ:
                val = (val == val2) ? val_true : val_false;
                break;
            case OP2_NEQ:
                val = (val != val2) ? val_true : val_false;
                break;
            default:
                goto invalid;
            }
            AR_push(stack, &val);
            break;

        case OP_OP3:
            if (AR_size(stack) - stack_base < 3)
                goto invalid;
            switch (argument)
            {
            default:
                goto invalid;
            }
            break;

        case OP_CAL:
            if (AR_size(stack) - stack_base < argument)
                goto invalid;
            invoke(state, stack, argument);
            break;

        case OP_RET:
            goto returned;

        default:
            goto invalid;
        }
    }

invalid:
    fatal("Instruction %d (opcode %d, argument: %d) could not be executed.\n"
          "Stack height was %d (%d - %d).",
        i - (Instruction*)state->mod->function_data - 1, (i - 1)->opcode, (i - 1)->argument,
        AR_size(stack) - stack_base, AR_size(stack), stack_base);

returned:
    if (AR_size(stack) <= stack_base)
        fatal("Empty stack at end of invocation!\n");

    AR_pop(stack, &val);
    return val;
}

void invoke(State *state, Array *stack, int args)
{
    int func_id;
    int stack_base;
    Value result;

    if (args <= 0)
        fatal("Too few arguments for function call (%d)", args);

    if (args > AR_size(stack))
        fatal("Too many arguments for function call (%d; stack height is %d).",
            args, AR_size(stack));

    /* Figure out which function to call */
    func_id = (int)*(Value*)AR_at(stack, AR_size(stack) - args);
    args -= 1;
    stack_base = AR_size(stack) - args;

    if (func_id < 0)
    {
        func_id = -func_id - 1;
        if (func_id > NBUILTIN)
            fatal("Invalid system call (%d).", func_id);
        result = builtins[func_id](state, stack,
            args, (Value*)AR_data(stack) + AR_size(stack) - args);
    }
    else
    if (func_id >= state->mod->nfunction)
    {
        error("Non-existent function %d invoked!", func_id);
        result = val_nil;
    }
    else
    {
        const Function *f = &state->mod->functions[func_id];

        /* Check number of arguments and adjust stack frame if necessary */
        if (args != f->nparam)
        {
            warn("Function %d has %d parameters, but was invoked with %d arguments!",
                func_id, f->nparam, args);

            /* Add arguments if arguments < parameters */
            for ( ; args < f->nparam; ++args)
                AR_push(stack, &val_nil);

            /* Remove arguments if arguments > parameters */
            for ( ; args > f->nparam; --args)
                AR_pop(stack, NULL);
        }

        result = exec_function(state, stack, f, stack_base);
    }

    AR_resize(stack, stack_base);
    *(Value*)AR_last(stack) = result;
}

Value builtin_quit(State *state, Array *stack, int narg, Value *args)
{
    exit(narg == 0 ? 0 : (int)args[0]);

    /* Should not get here. */
    return val_nil;
}

Value builtin_write(State *state, Array *stack, int narg, Value *args)
{
    int n, m;
    for (n = 0; n < narg; ++n)
    {
        m = (int)args[n];
        if (m < 0 || m >= state->mod->nstring)
        {
            error("write: invalid argument %d (string table has %d entries)",
                m, state->mod->nstring);
        }
        else
        {
            if (n > 0)
                fputc(' ', stdout);
            fputs(state->mod->strings[m], stdout);
        }
    }
    fputc('\n', stdout);
    return ferror(stdout) ? val_false : val_true;
}

int main(int argc, char *argv[])
{
    const char *path;
    FILE *fp;
    Module *mod;
    State *state;
    Array stack = AR_INIT(sizeof(Value));

    if (argc > 2)
    {
        printf("Usage: ali [<module>]\n");
        return 0;
    }

    if (argc == 2)
        path = argv[1];
    else
        path = "module.alo";

    fp = fopen(path, "rb");
    if (!fp)
        fatal("Unable to open file \"%s\" for reading.", path);

    mod = load_module(fp);
    if (!mod)
        fatal("Invalid module file: \"%s\".", path);
    fclose(fp);

    state = make_state(mod);
    if (!state)
        fatal("Could not create interpreter state.");

    if (mod->init_func != -1)
    {
        Value val = mod->init_func;
        AR_push(&stack, &val);
        invoke(state, &stack, 1);
    }

    return 0;
}
