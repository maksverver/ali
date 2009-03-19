#include "debug.h"
#include "opcodes.h"
#include "interpreter.h"
#include "strings.h"
#include "Array.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/* Limit on the size of the script's execution stack.
   Useful to prevent infinite recusion, but also necessary since the interpreter
   uses the C stack to invoke functions, so setting this value too high can
   crash the interpreter. */
#define MAX_STACK_SIZE 1000

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

    /* Command table */
    int ncommand;
    Command *commands;

} Module;

typedef struct Variables
{
    int nval;
    Value *vals;
} Variables;

typedef struct Interpreter
{
    Module      *mod;
    Variables   *vars;
    Array       *stack;

    int         num_newlines;
} Interpreter;


typedef Value (*Builtin)(Interpreter *I, int narg, Value *args);

Value builtin_write   (Interpreter *I, int narg, Value *args);
Value builtin_writeln (Interpreter *I, int narg, Value *args);
Value builtin_writef  (Interpreter *I, int narg, Value *args);
Value builtin_pause   (Interpreter *I, int narg, Value *args);
Value builtin_reset   (Interpreter *I, int narg, Value *args);
Value builtin_quit    (Interpreter *I, int narg, Value *args);

#define NBUILTIN 6
Builtin builtins[NBUILTIN] = {
    builtin_write, builtin_writeln, builtin_writef,
    builtin_pause, builtin_reset,   builtin_quit };



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
void invoke(Interpreter *I, int nargs, int nret);

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
        mod->nfragment     = 0;
        mod->fragments     = NULL;
        mod->fragment_data = NULL;
        return true;
    }

    mod->nfragment     = entries;
    mod->fragments     = malloc(entries*sizeof(Fragment));
    mod->fragment_data = malloc(size - (8 + 8*entries));

    for (n = 0; n < entries; ++n)
    {
        int type, flags, id, offset;

        type  = read_int8(fp);
        flags = (type >> 4)&0xf0;
        type  = (type >> 0)&0x0f;
        id    = read_int24(fp);
        if (id < 0 || id > (type == F_VERB ? mod->num_verbs :
                            type == F_PREPOSITION ? mod->num_prepositions :
                            type == F_ENTITY ? mod->num_entities : -1) )
            return false;

        offset = read_int32(fp);
        if (offset < 8 + 8*entries || offset >= size)
            return false;
        offset -= 8 + 8*entries;

        mod->fragments[n].type  = type;
        mod->fragments[n].id    = id;
        mod->fragments[n].str   = (char*)mod->fragment_data + offset;
        mod->fragments[n].canon = (flags&1) != 0;
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
        mod->nstring     = 0;
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
        mod->nfunction     = 0;
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
        int nret, nparam, offset;

        read_int16(fp); /* Skip reserved bytes */
        nret = read_int8(fp);
        if (nret < 0)
            return false;
        nparam = read_int8(fp);
        if (nparam < 0)
            return false;

        offset = read_int32(fp);
        if (offset < 8 + 8*entries || offset >= size || offset%4 != 0)
            return false;

        mod->functions[n].id     = n;
        mod->functions[n].ninstr = 0;  /* this is set to a real value below */
        mod->functions[n].nparam = nparam;
        mod->functions[n].nret   = nret;
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
    int size, entries, n;

    size = read_int32(fp);
    if (size < 8 || size%4 != 0)
        return false;
    entries = read_int32(fp);
    if ((size - 8)/4 < entries)
        return false;

    if (entries == 0)
    {
        mod->ncommand     = 0;
        mod->commands     = NULL;
        return true;
    }

    assert(mod->commands == NULL);
    mod->commands = malloc(entries*sizeof(Command));
    if (mod->commands == NULL)
        return false;
    mod->ncommand = entries;

    int offset = 8;
    for (n = 0; n < entries; ++n)
    {
        if (offset + 4 > size)
            return false;
        offset += 4;

        int form = read_int16(fp);
        int narg = read_int16(fp);

        if (offset + 4*narg + 8 > size)
            return false;
        offset += 4*narg + 8;

        if ( (form == 0 && narg != 1) ||
             (form == 1 && narg != 2) ||
             (form == 2 && narg != 4) )
        {
            return false;
        }

        mod->commands[n].form = form;
        mod->commands[n].part[0] = (narg > 0) ? read_int32(fp) : -1;
        mod->commands[n].part[1] = (narg > 1) ? read_int32(fp) : -1;
        mod->commands[n].part[2] = (narg > 2) ? read_int32(fp) : -1;
        mod->commands[n].part[3] = (narg > 3) ? read_int32(fp) : -1;
        mod->commands[n].guard    = read_int32(fp);
        mod->commands[n].function = read_int32(fp);
    }
    return offset == size;
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
    free(mod->commands);
    mod->commands = NULL;
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

Variables *alloc_vars(Module *mod)
{
    Variables *vars = malloc(sizeof(Variables));
    vars->nval = mod->num_entities*mod->num_properties + mod->num_globals;
    vars->vals = malloc(vars->nval*sizeof(Value));
    return vars;
}

void free_vars(Variables *vars)
{
    free(vars->vals);
    free(vars);
}

void push_stack(Array *stack, Value value)
{
    if (AR_size(stack) == MAX_STACK_SIZE)
        fatal("Stack limit exceeded when pushing a value.");
    AR_push(stack, &value);
}

void restart(Interpreter *I)
{
    int n;

    /* Initialize global variables to nil */
    for (n = 0; n < I->vars->nval; ++n)
        I->vars->vals[n] = val_nil;

    /* Reset output formatting */
    fputc('\n', stdout);
    I->num_newlines = 2;

    /* Call initialization function (if we have one) */
    if (I->mod->init_func != -1)
    {
        push_stack(I->stack, (Value)I->mod->init_func);
        invoke(I, 1, 0);
    }
}

Value exec_function(Interpreter *I, const Function *f, int stack_base)
{
    const Instruction *i, *j;
    Value val, val2;

    /* Interpreter loop */
    for (i = f->instrs; ;)
    {
        int opcode   = i->opcode;
        int argument = i->argument;
        /* info("Instruction %d", i - (Instruction*)I->mod->function_data); */
        ++i;

        switch(opcode)
        {
        case OP_LLI:
            push_stack(I->stack, (Value)argument);
            break;

        case OP_POP:
            if (argument < 0 || stack_base + argument > AR_size(I->stack))
                goto invalid;
            AR_resize(I->stack, AR_size(I->stack) - argument);
            break;

        case OP_LDL:
            if (argument < 0 || stack_base + argument >= AR_size(I->stack))
                goto invalid;
            push_stack(I->stack, *(Value*)AR_at(I->stack, stack_base + argument));
            break;

        case OP_STL:
            if (argument < 0 || stack_base + argument >= AR_size(I->stack) - 1)
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
                if (AR_size(I->stack) - stack_base < 1)
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
                if (AR_size(I->stack) - stack_base < 2)
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
            if (AR_size(I->stack) - stack_base < 1)
                goto invalid;
            AR_pop(I->stack, &val);
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
            if (AR_size(I->stack) - stack_base < 1)
                goto invalid;
            AR_pop(I->stack, &val);
            switch (argument)
            {
            case OP1_NOT:
                /* FIXME: is this right? (Must stay in sync with OP_JNP above!) */
                val = (val > 0) ? val_false : val_true;
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
            if (AR_size(I->stack) - stack_base < argument%256)
                goto invalid;
            invoke(I, argument%256, argument/256);
            break;

        case OP_RET:
            switch (argument)
            {
            case 0:
                return val_nil;
            case 1:
                if (AR_size(I->stack) <= stack_base)
                    fatal("Empty stack at end of invocation!\n");
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
          "Stack height was %d (%d - %d).",
        i - (Instruction*)I->mod->function_data - 1, (i - 1)->opcode, (i - 1)->argument,
        AR_size(I->stack) - stack_base, AR_size(I->stack), stack_base);
    return val_nil;
}

void invoke(Interpreter *I, int nargs, int nret)
{
    int func_id;
    int stack_base;
    Value result;

    if (nargs <= 0)
        fatal("Too few arguments for function call (%d)", nargs);

    if (nret < 0)
        fatal("Too few return values for function call (%d)", nret);

    if (nret > 1)
        fatal("Too many return values for function call (%d)", nret);

    if (nargs > AR_size(I->stack))
        fatal("Too many arguments for function call (%d; stack height is %d).",
            nargs, AR_size(I->stack));

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
        if (func_id > NBUILTIN)
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

/* Outputs a single character (but with a little formatting) */
void write_ch(Interpreter *I, char ch)
{
    switch (ch)
    {
    case ' ':
        if (I->num_newlines == 0)
            fputc(' ', stdout);
        break;

    case '\t':
        /* Tab can be used to indent */
        fputs("    ", stdout);
        I->num_newlines = 0;
        break;

    case '\n':
        if (I->num_newlines < 2)
        {
            fputc('\n', stdout);
            I->num_newlines++;
        }
        break;

    default:
        fputc(ch, stdout);
        I->num_newlines = 0;
        break;
    }
}

/* Writes a line-wrapped string */
void write_str(Interpreter *I, const char *i, const char *j)
{
    while (i != j) write_ch(I, *i++);
}

Value builtin_write(Interpreter *I, int narg, Value *args)
{
    int n;
    for (n = 0; n < narg; ++n)
    {
        write_ch(I, ' ');
        const char *s = get_string(I, args[n]);
        write_str(I, s, s + strlen(s));
    }
    return val_nil;
}

Value builtin_writeln(Interpreter *I, int narg, Value *args)
{
    builtin_write(I, narg, args);
    write_ch(I, '\n');
    return val_nil;
}

Value builtin_writef(Interpreter *I, int narg, Value *args)
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
                    write_str(I, buf, buf + strlen(buf));
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
                    const char *s = get_string(I, args[a++]);
                    write_str(I, s, s + strlen(s));
                }
            }

            ++p;  /* skip formatting character too */
        }

        const char *q = p;
        while (*q != '%' && *q != '\0') ++q;
        write_str(I, p, q);
        p = q;
    }

    if (a < narg)
    {
        warn("Too many arguments in call to writef()");
    }

    return val_nil;
}

Value builtin_pause(Interpreter *I, int narg, Value *args)
{
    char line[1024];
    fputs("Press Enter to continue...\n", stdout);
    fflush(stdout);
    fgets(line, sizeof(line), stdin);
    return val_nil;
}

Value builtin_reset(Interpreter *I, int narg, Value *args)
{
    /* Initialize global variables to nil */
    int n;
    for (n = 0; n < I->vars->nval; ++n)
        I->vars->vals[n] = val_nil;
    return val_nil;
}

__attribute__((__noreturn__))
void quit(Interpreter *I, int status)
{
    /* Clean up allocated memory */
    free_vars(I->vars);
    free_module(I->mod);
    AR_destroy(I->stack);

    /* Exit */
    exit(status);
}

Value builtin_quit(Interpreter *I, int narg, Value *args)
{
    quit(I, 0);
}

/* Returns the index of a fragment of a matching fragment. */
static int find_fragment(const Module *mod, const char *text, FragmentType type)
{
    /* Binary search for a matching fragment in range [lo,hi) */
    int lo = 0, hi = mod->nfragment;
    while (lo < hi)
    {
        int mid = lo + (hi - lo)/2;

        int d = strcmp(mod->fragments[mid].str, text);
        if (d == 0)
            d = mod->fragments[mid].type - type;

        if (d < 0)
            lo = mid + 1;
        else
        if (d > 0)
            hi = mid;
        else
            return mod->fragments[mid].id;  /* found */
    }
    return -1;  /* not found */
}

static bool parse_command(const Module *mod, char *line, Command *cmd)
{
    int verb, ent1, prep, ent2;
    char *eol, *p, *q, *r;

    /* Find end of line */
    for (eol = line; *eol; ++eol) { }

    if ((verb = find_fragment(mod, line, F_VERB)) >= 0)
    {
        cmd->form = 0;
        cmd->part[0] = verb;
        cmd->part[1] = -1;
        cmd->part[2] = -1;
        cmd->part[3] = -1;
        return true;
    }

    /* Split string at `p' */
    for (p = eol; p >= line; --p)
    {
        if (*p != ' ') continue;
        *p = '\0';
        if ((verb = find_fragment(mod, line, F_VERB)) >= 0)
        {
            if ((ent1 = find_fragment(mod, p + 1, F_ENTITY)) >= 0)
            {
                cmd->form = 1;
                cmd->part[0] = verb;
                cmd->part[1] = ent1;
                cmd->part[2] = -1;
                cmd->part[3] = -1;
                return true;
            }

            for (q = eol; q >= p; --q)
            {
                if (*q != ' ') continue;
                *q = '\0';
                if ((ent1 = find_fragment(mod, p + 1, F_ENTITY)) >= 0)
                {
                    for (r = eol; r >= q; --r)
                    {
                        if (*r != ' ') continue;
                        *r = '\0';
                        if (((prep = find_fragment(mod, q + 1, F_PREPOSITION)) >= 0) &&
                            ((ent2 = find_fragment(mod, r + 1, F_ENTITY)) >= 0))
                        {
                            cmd->form = 2;
                            cmd->part[0] = verb;
                            cmd->part[1] = ent1;
                            cmd->part[2] = prep;
                            cmd->part[3] = ent2;
                            return true;
                        }
                        *r = ' ';
                    }
                }
                *q= ' ';
            }
        }
        *p = ' ';
    }
    return false;
}

static bool evaluate_function(Interpreter *I, int func)
{
    if (func < 0 || func >= I->mod->nfunction)
        return false;

    Value val;
    push_stack(I->stack, (Value)func);
    invoke(I, 1, 1);
    AR_pop(I->stack, &val);
    return val > 0;
}

static int cmp_commands(const Command *c, const Command *d)
{
    if (c->form    - d->form    != 0) return c->form    - d->form;
    if (c->part[0] - d->part[0] != 0) return c->part[0] - d->part[0];
    if (c->part[1] - d->part[1] != 0) return c->part[1] - d->part[1];
    if (c->part[2] - d->part[2] != 0) return c->part[2] - d->part[2];
    if (c->part[3] - d->part[3] != 0) return c->part[3] - d->part[3];
    return 0;
}

/* Returns the index of the first command that is not less than `cmd' according
   to cmp_command() defined above, or mod->ncommand if none is found. This
   means that the caller should check that the result is less than mod->ncommand
   and that cmp_commands(mod->commands[result], cmd) == 0, to ensure that a
   matching element was foud. */
static int find_first_matching_command(const Module *mod, const Command *cmd)
{
    /* Binary search for command in range [lo,hi) */
    int lo = 0, hi = mod->ncommand;
    while (lo < hi)
    {
        int mid = lo + (hi - lo)/2;
        if (cmp_commands(&mod->commands[mid], cmd) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static void process_command(Interpreter *I, char *line)
{
    Command cmd;
    if (!parse_command(I->mod, line, &cmd))
    {
        printf("I didn't understand that.\n");
        return;
    }

    int num_matched = 0, num_active = 0, n, cmd_func;
    for (n = find_first_matching_command(I->mod, &cmd);
         n < I->mod->ncommand; ++n)
    {
        const Command *command = &I->mod->commands[n];
        if (cmp_commands(command, &cmd) != 0)
            break;

        ++num_matched;
        if (command->guard < 0 || evaluate_function(I, command->guard))
        {
            if (++num_active == 1)
                cmd_func = command->function;
        }
    }

    if (num_matched == 0)
    {
        printf("You can't do that in this game.\n");
        return;
    }

    if (num_active == 0)
    {
        printf("That's not possible right now.\n");
        return;
    }

    if (num_active  > 1)
    {
        printf("That command is ambiguous.\n");
        return;
    }

    /* Invoke the command function */
    push_stack(I->stack, (Value)cmd_func);
    invoke(I, 1, 0);

    /* TODO: save action to transcript? */
}

void command_loop(Interpreter *I)
{
    char line[1024];
    for (;;)
    {
        write_ch(I, '\n');
        write_ch(I, '\n');

        fputs("> ", stdout);
        fflush(stdout);
        char *line_ptr = fgets(line, sizeof(line), stdin);
        fputc('\n', stdout);
        if (line_ptr == NULL)
            break;

        char *eol = strchr(line, '\n');
        if (eol == NULL)
        {
            if (strlen(line) == sizeof(line) - 1)
                warn("Input line was truncated!");
        }
        else
        {
            *eol = '\0';
        }

        normalize(line);
        process_command(I, line);
    }
}

int main(int argc, char *argv[])
{
    Interpreter interpreter;
    Array stack = AR_INIT(sizeof(Value));
    const char *path;
    FILE *fp;

    if (argc > 2)
    {
        printf("Usage: ali [<module>]\n");
        return 0;
    }

    memset(&interpreter, 0, sizeof(interpreter));

    /* Attempt to load executable module */
    path = (argc == 2 ? argv[1] :"module.alo");
    fp = fopen(path, "rb");
    if (!fp)
        fatal("Unable to open file \"%s\" for reading.", path);
    interpreter.mod = load_module(fp);
    fclose(fp);
    if (interpreter.mod == NULL)
        fatal("Invalid module file: \"%s\".", path);

    /* Allocate stack */
    interpreter.stack = &stack;

    /* Allocate variables */
    interpreter.vars = alloc_vars(interpreter.mod);
    if (interpreter.vars == NULL)
        fatal("Could not create interpreter state.");

    /* Initialize vars */
    restart(&interpreter);
    command_loop(&interpreter);
    warn("Unexpected end of input!");
    quit(&interpreter, 1);
}
