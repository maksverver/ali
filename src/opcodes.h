#ifndef OPCODES_H_INCLUDED
#define OPCODES_H_INCLUDED

/*
    Function call with N arguments:
        N = 0: debug break
        arg[0] >= 0:
            Call local function with N - 1 arguments
        arg[0] < 0:
            System call

    Load indirect: (argument: offset)
        pop index
        push vars[globals + properties*index + offset]

    Store indirect: (argument: offset; stack: index, value)
        pop value
        pop index
        set vars[globals + properties*index + offset] = value
*/

#define OP_LLI   1  /* Push integer literal */
#define OP_POP   2  /* Pop stack elements */
#define OP_LDL   3  /* Push a stack element (local variable) */
#define OP_STL   4  /* Store value at a stack element (local variable) */
#define OP_LDG   5  /* Load global variable */
#define OP_STG   6  /* Store global variable */
#define OP_LDI   7  /* Load global variable indirect */
#define OP_STI   8  /* Store global variable indirect */
#define OP_JMP   9  /* Unconditional jump */
#define OP_JNP  10  /* Conditional jump (if popped value is <= 0) */
#define OP_OP1  11  /* Evaluate using unary operator */
#define OP_OP2  12  /* Evaluate using binary operator */
#define OP_OP3  13  /* Evaluate using ternary operator */
#define OP_CAL  14  /* Function call */
#define OP_RET  15  /* Function return */

/* Operators */
#define OP1_NOT  1  /* Negation (not)*/
#define OP2_AND  2  /* Conjunction (and)*/
#define OP2_OR   3  /* Disjunction (or) */
#define OP2_EQ   4  /* Equality (=) */
#define OP2_NEQ  5  /* Inquality (<>) */

/*
    Mnemonic  Pop Push  Argument
    LLI        0   1    Literal value
    POP        n   0    Number of stack elements to discard (n)
    LDL        0   1    Index of element to load (relative to stack bottom)
    STL        1   0    Index of element to write to (relative to stack bottom)
    LDG        1   1    Variable index
    STG        2   0    Variable index
    LDI        1   1    Index offset 
    STI        2   0    Index offset
    JMP        0   0    Offset in instructions relative to PC
    JNP        1   0    Offset in instructions relative to PC
    OP1        1   1    Unary operator ID
    OP2        2   1    Binary operator ID
    OP3        3   1    Ternary operator ID
    CAL        n   m    256*m + n: number of argument (n) and return values (m)
    RET        n   0    Number of arguments to return (n)
*/

#endif /* ndef OPCODES_H_INCLUDED */
