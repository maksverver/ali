#ifndef INTERPRETER_H_INCLUDED
#define INTERPRETER_H_INCLUDED

#include <stdbool.h>
#include <stdio.h>
#include "Array.h"
#include "parser.h"

typedef struct Instruction
{
    int opcode, argument;
} Instruction;


typedef struct Function
{
    int id, nparam, nret, ninstr;
    Instruction *instrs;
} Function;

typedef struct Command
{
    SymbolRef   symbol;
    int         guard;
    int         function;
} Command;


/* Values */
typedef int Value;
extern const Value val_true, val_false, val_nil;
#define VAL_TO_BOOL(v)  ((v) > 0 ? true : false)
#define BOOL_TO_VAL(b)  ((b) ? val_true: val_false)

/* List of built-in function names (terminated by NULL) */
#define NUM_BUILTIN_FUNCS (6)
extern const char * const builtin_func_names[NUM_BUILTIN_FUNCS + 1];

/* List of built-in variable names (terminated by NULL) */
#define NUM_BUILTIN_VARS (8)
extern const char * const builtin_var_names[NUM_BUILTIN_VARS + 1];
enum builtin_var_ids { var_title, var_subtitle };

typedef struct Module
{
    int num_entities, num_properties, num_globals, init_func;

    /* String table */
    int             nstring;
    char            **strings;
    void            *string_data;

    /* Function table */
    int             nfunction;
    Function        *functions;
    void            **function_data;

    /* Word table */
    int             nword;
    char            **words;
    void            *word_data;
    int             *word_index;       /* closed hash table of word indices */
    size_t          word_index_size;   /* size of hash table */

    /* Grammar table */
    int             nsymbol;
    GrammarRuleSet  *symbol_rules;
    bool            *symbol_nullable;

    /* Command table */
    int             ncommand;
    Command         *commands;

} Module;


typedef struct Variables
{
    int nval;
    Value *vals;
} Variables;


struct Interpreter;

typedef struct Callbacks
{
    void (*quit)(struct Interpreter *I, int code);
    void (*pause)(struct Interpreter *I);
} Callbacks;

typedef struct Interpreter
{
    Module      *mod;           /* loaded with load_module() */
    Variables   *vars;          /* allocated with alloc_vars() */
    Array       *stack;         /* array of Values */
    Array       *output;        /* array of chars */
    Callbacks   *callbacks;     /* optional callback functions */
    void        *aux;           /* auxiliary data (useful for callbacks) */
} Interpreter;


/* Module loding */
struct IOStream;
Module *load_module(struct IOStream *ios);
void free_module(Module *mod);

/* Variables allocation (variables are cleared on allocation) */
Variables *alloc_vars(Module *mod);
void free_vars(Variables *vars);
void clear_vars(Variables *vars);
Variables *dup_vars(Variables *vars);
int cmp_vars(Variables *vars1, Variables *vars2);

/* Interpreter functions */
void process_command(Interpreter *I, char *command);
void reinitialize(Interpreter *I);

#endif /* ndef INTERPRETER_H_INCLUDED */
