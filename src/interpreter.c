#include "debug.h"
#include "io.h"
#include "interpreter.h"
#include "opcodes.h"
#include <string.h>

const Value val_true = 1, val_false = 0, val_nil = -1;

/* Limit on the size of the script's execution stack.
   Useful to prevent infinite recusion, but also necessary since the interpreter
   uses the C stack to invoke functions, so setting this value too high can
   crash the interpreter. */
#define MAX_STACK_SIZE 1000

typedef Value (*Builtin)(Interpreter *I, int narg, Value *args);

static Value builtin_write   (Interpreter *I, int narg, Value *args);
static Value builtin_writeln (Interpreter *I, int narg, Value *args);
static Value builtin_writef  (Interpreter *I, int narg, Value *args);
static Value builtin_pause   (Interpreter *I, int narg, Value *args);
static Value builtin_quit    (Interpreter *I, int narg, Value *args);
static Value builtin_reset   (Interpreter *I, int narg, Value *args);

#define NBUILTIN 6
Builtin builtins[NBUILTIN] = {
    builtin_write, builtin_writeln, builtin_writef,
    builtin_pause, builtin_quit, builtin_reset };

const char * const builtin_names[] = {
    "write", "writeln", "writef", "pause", "quit", "reset", NULL };



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

static bool skip(IOStream *ios, int size)
{
    while (size-- > 0)
        if (!read_int8(ios, NULL))
            return false;
    return true;
}

static bool read_header(IOStream *ios, Module *mod)
{
    int size, version;

    if (!read_int32(ios, &size) || size < 32)
        return false;

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
        read_int32(ios, &mod->num_verbs) &&
        read_int32(ios, &mod->num_prepositions) &&
        read_int32(ios, &mod->num_entities) &&
        read_int32(ios, &mod->num_properties) &&
        read_int32(ios, &mod->num_globals) &&
        read_int32(ios, &mod->init_func) &&
        skip(ios, size - 32);
}

static bool read_fragment_table(IOStream *ios, Module *mod)
{
    int size, entries, n;

    if (!read_int32(ios, &size) || size < 8)
        return false;
    if (!read_int32(ios, &entries) || (size - 8)/8 < entries)
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

        if (!read_int8(ios, &type) || !read_int24(ios, &id))
            return false;
        flags = (type >> 4)&0xf0;
        type  = (type >> 0)&0x0f;
        if (id < 0 || id > (type == F_VERB ? mod->num_verbs :
                            type == F_PREPOSITION ? mod->num_prepositions :
                            type == F_ENTITY ? mod->num_entities : -1) )
            return false;

        if (!read_int32(ios, &offset) ||
            offset < 8 + 8*entries || offset >= size)
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
    if (!read_data(ios, mod->fragment_data, size))
        return false;
    if (((char*)mod->fragment_data)[size - 1] != '\0')
        return false;

    return true;
}

static bool read_string_table(IOStream *ios, Module *mod)
{
    int size, entries, n;

    if (!read_int32(ios, &size) || size < 8)
        return false;
    if (!read_int32(ios, &entries) || (size - 8)/4 < entries)
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
        int offset;
        if (!read_int32(ios, &offset) ||
            offset < 8 + 4*entries || offset >= size)
            return false;
        mod->strings[n] = (char*)mod->string_data + offset - (8 + 4*entries);
    }

    size -= 8 + 4*entries;
    if (size <= 0)
        return false;
    if (!read_data(ios, mod->string_data, size))
        return false;
    if (((char*)mod->string_data)[size - 1] != '\0')
        return false;

    return true;
}

static bool read_function_table(IOStream *ios, Module *mod)
{
    int size, entries, ninstr, n;
    Instruction *instrs;

    if (!read_int32(ios, &size) || size < 8 || size%4 != 0)
        return false;
    if (!read_int32(ios, &entries) || (size - 8)/4 < entries)
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

        if (!read_int16(ios, NULL)) /* skip reserved bytes */
            return false;

        if (!read_int8(ios, &nret) || nret < 0)
            return false;
        if (!read_int8(ios, &nparam) || nparam < 0)
            return false;

        if (!read_int32(ios, &offset) ||
            offset < 8 + 8*entries || offset >= size || offset%4 != 0)
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
        int opcode, argument;
        if (!read_int8(ios, &opcode) || !read_int24(ios, &argument))
            return false;
        instrs[n].opcode   = opcode;
        instrs[n].argument = argument;
    }

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

static bool read_command_table(IOStream *ios, Module *mod)
{
    int size, entries, n;

    if (!read_int32(ios, &size) || size < 8 || size%4 != 0)
        return false;
    if (!read_int32(ios, &entries) || (size - 8)/4 < entries)
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

        int form, narg;
        if (!read_int16(ios, &form) || !read_int16(ios, &narg))
            return false;

        if (offset + 4*narg + 8 > size)
            return false;
        offset += 4*narg + 8;

        if ( (form == 0 && narg != 1) ||
             (form == 1 && narg != 2) ||
             (form == 2 && narg != 4) )
        {
            return false;
        }

        int part0 = -1, part1 = -1, part2 = -1, part3 = -1, guard, function;
        if ((narg > 0 && !read_int32(ios, &part0)) ||
            (narg > 1 && !read_int32(ios, &part1)) ||
            (narg > 2 && !read_int32(ios, &part2)) ||
            (narg > 3 && !read_int32(ios, &part3)) ||
            !read_int32(ios, &guard) || !read_int32(ios, &function))
        {
            return false;
        }
        mod->commands[n].form    = form;
        mod->commands[n].part[0] = part0;
        mod->commands[n].part[1] = part1;
        mod->commands[n].part[2] = part2;
        mod->commands[n].part[3] = part3;
        mod->commands[n].guard    = guard;
        mod->commands[n].function = function;
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

Module *load_module(IOStream *ios)
{
    Module *mod = malloc(sizeof(Module));
    int sig;

    memset(mod, 0, sizeof(Module));

    if (!read_int32(ios, &sig))
    {
        error("Unable to read from module file.");
        goto failed;
    }

    if (sig != 0x616c696f)
    {
        error("Module file has incorrect signature.");
        goto failed;
    }

    if (!read_header(ios, mod))
    {
        error("Failed to load module header.");
        goto failed;
    }

    if (!read_fragment_table(ios, mod))
    {
        error("Failed to read module fragment table.");
        goto failed;
    }

    if (!read_string_table(ios, mod))
    {
        error("Failed to read module string table.");
        goto failed;
    }

    if (!read_function_table(ios, mod))
    {
        error("Failed to read module function table.");
        goto failed;
    }

    if (!read_command_table(ios, mod))
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
            if (!VAL_TO_BOOL(val))
                i += argument;
            break;

        case OP_JMP:
            i += argument;
            break;

        case OP_OP1:
            if (AR_size(I->stack) - stack_base < 1)
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
                val = (VAL_TO_BOOL(val) && VAL_TO_BOOL(val2))
                    ? val_true : val_false;
                break;
            case OP2_OR:
                val = (VAL_TO_BOOL(val) || VAL_TO_BOOL(val2))
                    ? val_true : val_false;
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

static void invoke(Interpreter *I, int nargs, int nret)
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
    if (narg > 0)
        warn("Arguments to pause() ignored.\n");
    if (I->callbacks != NULL && I->callbacks->pause != NULL)
        (*I->callbacks->pause)(I);
    return val_nil;
}

static Value builtin_quit(Interpreter *I, int narg, Value *args)
{
    if (narg > 0)
        warn("Arguments to quit() ignored.\n");
    if (I->callbacks != NULL && I->callbacks->quit != NULL)
        (*I->callbacks->quit)(I, 0);
    return val_nil;
}

static Value builtin_reset(Interpreter *I, int narg, Value *args)
{
    if (narg > 0)
        warn("Arguments to reset() ignored.\n");
    clear_vars(I->vars);
    return val_nil;
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
    eol = line;
    while (*eol != '\0') ++eol;

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
    return VAL_TO_BOOL(val);
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

void process_command(Interpreter *I, char *line)
{
    AR_clear(I->output);

    Command cmd;
    if (!parse_command(I->mod, line, &cmd))
    {
        write_str(I, "I didn't understand that.\n");
        return;
    }

    int num_matched = 0, num_active = 0, n, cmd_func = -1;
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
        write_str(I, "You can't do that in this game.\n");
        return;
    }

    if (num_active == 0)
    {
        write_str(I, "That's not possible right now.\n");
        return;
    }

    if (num_active  > 1)
    {
        write_str(I, "That command is ambiguous.\n");
        return;
    }

    /* Invoke the command function */
    push_stack(I->stack, (Value)cmd_func);
    invoke(I, 1, 0);
}
