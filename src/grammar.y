%{

#include <stdlib.h>
#include "interpreter.h"
#include "opcodes.h"
extern char *yytext;

/* Used to generate assignments */
static int dest;

/* Functions defined in alic.c */
void begin_command(const char *str);
void end_command();
void begin_function(const char *id);
void add_parameter(const char *id);
void emit(int opcode, int arg);
void end_function();
void patch_jmp(int offset);
int resolve_global(const char *str);
int resolve_local(const char *str);
int resolve_function(const char *id);
int resolve_symbol(const char *str);
int parse_string(const char *token);
int resolve_fragment(const char *str, int type);
int resolve_property(const char *str);
void begin_verb();
void begin_preposition();
void begin_entity();
void add_fragment(const char *str);
int parse_fragment(const char *token, int type);
void push_args();
int pop_args();
void count_arg();

%}

%token FRAGMENT
%token IF ELSE SET RETURN CALL
%token VERB ENTITY PREPOSITION FUNCTION COMMAND
%token EQUAL INEQUAL AND OR NOT TRUE FALSE NIL
%token LPAREN RPAREN LSQRBR RSQRBR LCURBR RCURBR
%token PERIOD COMMA SEMICOLON
%token STRING INTEGER
%token IDENTIFIER ATTRIBUTE SYMBOL GLOBALVAR LOCALVAR
%token ERROR

%%

start           : decls;

decls           : decls decl
                | ;

decl            : declverb
                | declentity
                | declpreposition
                | declfunction
                | declcommand;

declverb        : VERB { begin_verb(); } synonyms PERIOD;
declentity      : ENTITY { begin_entity(); } synonyms PERIOD;
declpreposition : PREPOSITION { begin_preposition(); } synonyms PERIOD;
declfunction    : FUNCTION
                  IDENTIFIER { begin_function(yytext); }
                  LPAREN optparameters RPAREN
                  block { end_function(); };
declcommand     : COMMAND
                  FRAGMENT { begin_command(yytext); }
                  block { end_command(); };
optparameters   : parameters
                | ;
parameters      : parameters COMMA parameter
                | parameter;
parameter       : LOCALVAR { add_parameter(yytext); };
synonyms        : synonyms COMMA synonym
                | synonym;
synonym         : FRAGMENT { add_fragment(yytext); };
block           : LCURBR statements RCURBR
                | LCURBR statements statement RCURBR;
statements      : statements statement SEMICOLON
                | ;
statement       : expression { emit(OP_POP, 1); }
                | ifst
                | returnst
                | setst;

ifst            : IF expression { emit(OP_JNP, -1); } block elseclause { patch_jmp(0); };

elseclause      : ELSE { emit(OP_JMP, -1); patch_jmp(-1); } block
                | ;

returnst        : RETURN { emit(OP_LLI, 0); emit(OP_RET, 0); }
                | RETURN expression { emit(OP_RET, 0); };

setst           : SET GLOBALVAR { dest = resolve_global(yytext); }
                  expression { emit(OP_STG, dest); }
                | SET LOCALVAR { dest = resolve_local(yytext); }
                  expression { emit(OP_STL, dest); }
                | SET LSQRBR entref RSQRBR /* pushes entity index */
                  ATTRIBUTE { dest = resolve_property(yytext); }
                  expression { emit(OP_STI, dest); };

expression      : orexpr;

orexpr          : andexpr orterms;
orterms         : OR orexpr { emit(OP_OP2, OP2_OR); }
                | ;

andexpr         : eqexpr andterms;
andterms        : AND andexpr { emit(OP_OP2, OP2_AND); }
                | ;

eqexpr          : uexpr eqterms;
eqterms         : EQUAL uexpr { emit(OP_OP2, OP2_EQ); }
                | INEQUAL uexpr { emit(OP_OP2, OP2_NEQ); }
                | ;

uexpr           : NOT baseexpr { emit(OP_OP1, OP1_NOT); }
                | baseexpr;

varref          : LSQRBR entref RSQRBR ATTRIBUTE { emit(OP_LDI, resolve_property(yytext)); }
                | GLOBALVAR { emit(OP_LDG, resolve_global(yytext)); }
                | LOCALVAR { emit(OP_LDL, resolve_local(yytext)); };

baseexpr        : NIL       { emit(OP_LLI, -1); }
                | TRUE      { emit(OP_LLI, 1); }
                | FALSE     { emit(OP_LLI, 0); }
                | STRING    { emit(OP_LLI, parse_string(yytext)); }
                | INTEGER   { emit(OP_LLI, atoi(yytext)); }
                | SYMBOL    { emit(OP_LLI, ~resolve_symbol(yytext)); }
                | LPAREN expression RPAREN
                | IDENTIFIER { emit(OP_LLI, resolve_function(yytext)); push_args(); }
                  LPAREN arguments RPAREN { emit(OP_CAL, 1 + pop_args()); }
                | entref;

entref          : FRAGMENT { emit(OP_LLI, resolve_fragment(yytext, F_ENTITY)); }
                /* | SYMBOL */
                | varref;

arguments       : argument morearguments
                | ;
morearguments   : COMMA argument morearguments
                | ;
argument        : expression { count_arg(); };
