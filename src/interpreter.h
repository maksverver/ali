#ifndef INTERPRETER_H_INCLUDED
#define INTERPRETER_H_INCLUDED

#include <stdbool.h>

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

#endif /* ndef INTERPRETER_H_INCLUDED */
