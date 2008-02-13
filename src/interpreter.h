#ifndef INTERPRETER_H_INCLUDED
#define INTERPRETER_H_INCLUDED

typedef struct Instruction
{
    int opcode;
    int argument;
} Instruction;

typedef struct Function
{
    int id, nparam, ninstr;
    Instruction *instrs;
} Function;

typedef enum FragmentType {
    F_VERB = 0, F_PREPOSITION = 1, F_ENTITY = 2
} FragmentType;

typedef struct Fragment
{
    FragmentType type;
    int          id;
    const char   *str;
} Fragment;

#endif /* ndef INTERPRETER_H_INCLUDED */
