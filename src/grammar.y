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
void end_guard(void);
void end_command(void);
void begin_function(const char *id, int nret);
void add_parameter(const char *id);
void emit(int opcode, int arg);
void end_function(void);
void patch_jmp(int offset);
int resolve_global(const char *str);
int resolve_local(const char *str);
int resolve_symbol(const char *str);
void parse_string(const char *token);
int resolve_string(void);
void write_string(void);
int resolve_fragment(const char *str, int type);
int resolve_property(const char *str);
void begin_verb(void);
void begin_preposition(void);
void begin_entity(void);
void add_fragment(const char *str);
void begin_call(const char *name, int nret);
void end_call(int nret);
void count_arg(void);

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
                  IDENTIFIER { begin_function(yytext, 1); }
                  LPAREN optparameters RPAREN
                  expression { end_function(); };

declprocedure   : PROCEDURE
                  IDENTIFIER { begin_function(yytext, 0); }
                  LPAREN optparameters RPAREN
                  block { end_function(); };

declcommand     : optcmdtok
                  cmdfrags { begin_function(NULL, 1); /* guard */ }
                  guard { begin_function(NULL, 0); /* body */ }
                  block { end_command(); };
optcmdtok       : COMMAND
                | ;
cmdfrags        : cmdfrags COMMA cmdfrag
                | cmdfrag;
cmdfrag         : FRAGMENT { begin_command(yytext); };
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
                | proc_call SEMICOLON
                | string SEMICOLON { write_string(); };

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

proc_call       : IDENTIFIER { begin_call(yytext, 0); }
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
                | string    { emit(OP_LLI, resolve_string()); }
                | INTEGER   { emit(OP_LLI, atoi(yytext)); }
                | SYMBOL    { emit(OP_LLI, -2 - resolve_symbol(yytext)); }
                | LPAREN expression RPAREN
                | func_call
                | entref
                | varref;
string          : string strtok
                | strtok;
strtok          : STRING { parse_string(yytext); };
varref          : entref ATTRIBUTE { emit(OP_LDI, resolve_property(yytext)); }
                | GLOBALVAR { emit(OP_LDG, resolve_global(yytext)); }
                | LOCALVAR { emit(OP_LDL, resolve_local(yytext)); };
entref          : FRAGMENT { emit(OP_LLI, resolve_fragment(yytext, F_ENTITY)); }
                | LSQRBR expression RSQRBR;
func_call       : IDENTIFIER { begin_call(yytext, 1); }
                  LPAREN arguments RPAREN { end_call(1); };
arguments       : argument morearguments
                | ;
morearguments   : COMMA argument morearguments
                | ;
argument        : expression { count_arg(); };
