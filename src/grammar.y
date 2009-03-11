%{

#include <stdlib.h>
#include "interpreter.h"
#include "opcodes.h"
extern char *yytext;

/* Used to generate assignments */
static int dest;

/* Tokenizer functions */
int yylex();
void yyerror(const char *str);

/* Functions defined in alic.c */
void begin_command(const char *str);
void end_guard();
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
void begin_call();
void end_call(int nret);
void count_arg();

%}

%token FRAGMENT
%token IF THEN ELSE SET
%token VERB ENTITY PREPOSITION FUNCTION PROCEDURE COMMAND
%token EQUAL INEQUAL AND OR NOT TRUE FALSE NIL
%token LPAREN RPAREN LCURBR RCURBR LSQRBR RSQRBR
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
                | declprocedure
                | declcommand;

declverb        : VERB { begin_verb(); } synonyms PERIOD;

declentity      : ENTITY { begin_entity(); } synonyms PERIOD;

declpreposition : PREPOSITION { begin_preposition(); } synonyms PERIOD;

declfunction    : FUNCTION
                  IDENTIFIER { begin_function(yytext); }
                  LPAREN optparameters RPAREN
                  expression { end_function(1); };

declprocedure   : PROCEDURE
                  IDENTIFIER { begin_function(yytext); }
                  LPAREN optparameters RPAREN
                  block { end_function(0); };

declcommand     : COMMAND
                  FRAGMENT { begin_command(yytext); }
                  guard
                  block { end_command(); };
guard           : expression { end_guard(); }
                | ;

optparameters   : parameters
                | ;
parameters      : parameters COMMA parameter
                | parameter;
parameter       : LOCALVAR { add_parameter(yytext); };

synonyms        : synonyms COMMA synonym
                | synonym;
synonym         : FRAGMENT { add_fragment(yytext); };

block           : LCURBR statements RCURBR;
statements      : statements statement
                | ;
statement       : ifst
                | setst SEMICOLON
                | proc_call SEMICOLON;

ifst            : IF LPAREN expression RPAREN { emit(OP_JNP, -1); }
                  THEN block elseclause { patch_jmp(0); };
elseclause      : ELSE { emit(OP_JMP, -1); patch_jmp(-1); } block
                | ;

setst           : SET GLOBALVAR { dest = resolve_global(yytext); }
                  expression { emit(OP_STG, dest); }
                | SET LOCALVAR { dest = resolve_local(yytext); }
                  expression { emit(OP_STL, dest); }
                | SET entref /* pushes entity index */
                  ATTRIBUTE { dest = resolve_property(yytext); }
                  expression { emit(OP_STI, dest); };

proc_call       : IDENTIFIER { begin_call(yytext); }
                  LPAREN arguments RPAREN { end_call(0); };

expression      : ifexpr;
ifexpr          : orexpr
                | IF expression { emit(OP_JNP, -1); }
                  THEN expression { emit(OP_JMP, -1); patch_jmp(-1); }
                  ELSE expression { patch_jmp(0); };
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
baseexpr        : NIL       { emit(OP_LLI, -1); }
                | TRUE      { emit(OP_LLI, 1); }
                | FALSE     { emit(OP_LLI, 0); }
                | STRING    { emit(OP_LLI, parse_string(yytext)); }
                | INTEGER   { emit(OP_LLI, atoi(yytext)); }
                | SYMBOL    { emit(OP_LLI, -2 - resolve_symbol(yytext)); }
                | LPAREN expression RPAREN
                | func_call
                | entref
                | varref;
varref          : entref ATTRIBUTE { emit(OP_LDI, resolve_property(yytext)); }
                | GLOBALVAR { emit(OP_LDG, resolve_global(yytext)); }
                | LOCALVAR { emit(OP_LDL, resolve_local(yytext)); };
entref          : FRAGMENT { emit(OP_LLI, resolve_fragment(yytext, F_ENTITY)); }
                | LSQRBR expression RSQRBR;
func_call       : IDENTIFIER { begin_call(yytext); }
                  LPAREN arguments RPAREN { end_call(1); };
arguments       : argument morearguments
                | ;
morearguments   : COMMA argument morearguments
                | ;
argument        : expression { count_arg(); };
