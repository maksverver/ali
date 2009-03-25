#ifndef INTERPRETER_H_INCLUDED
#define INTERPRETER_H_INCLUDED

#include <stdbool.h>
#include <stdio.h>
#include "Array.h"

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
    int form, part[4], guard, function;
} Command;


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


/* Values */
typedef int Value;
extern const Value val_true, val_false, val_nil;
#define VAL_TO_BOOL(v)  ((v) > 0 ? true : false)

/* List of built-in functions (terminated by NULL) */
#define NUM_BUILTINS (6)
extern const char * const builtin_names[NUM_BUILTINS + 1];


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
