%{
#include "grammar.tab.h"

int lineno;
%}

%pointer

%%

[A-Z][A-Z0-9]*([ ]+[A-Z][A-Z0-9]*)* return FRAGMENT;

if                              return IF;
then                            return THEN;
else                            return ELSE;
set                             return SET;

verb                            return VERB;
entity                          return ENTITY;
preposition                     return PREPOSITION;
function                        return FUNCTION;
procedure                       return PROCEDURE;
command                         return COMMAND;

=                               return EQUAL;
[<][>]                          return INEQUAL;
and                             return AND;
or                              return OR;
not                             return NOT;
true|yes                        return TRUE;
false|no                        return FALSE;
nil                             return NIL;

\(                              return LPAREN;
\)                              return RPAREN;
\{                              return LCURBR;
\}                              return RCURBR;
\[                              return LSQRBR;
\]                              return RSQRBR;

\.                              return PERIOD;
\,                              return COMMA;
\;                              return SEMICOLON;

\"(\\\"|[^\"\n])*\"             return STRING;
0|[1-9][0-9]*|-[1-9][0-9]*      return INTEGER;

[a-zA-Z_][a-zA-Z0-9_]*          return IDENTIFIER;
[.][a-zA-Z_][a-zA-Z0-9_]*       return ATTRIBUTE;
[:][a-zA-Z_][a-zA-Z0-9_]*       return SYMBOL;
[@][a-zA-Z_][a-zA-Z0-9_]*       return GLOBALVAR;
[$][a-zA-Z_][a-zA-Z0-9_]*       return LOCALVAR;

![^\n]*(\n|$)                   lineno++; return OUTPUT;
#[^\n]*(\n|$)                   lineno++;   /* ignore single-line comment */
[ \t\r]+                                    /* ignore whitespace */
\n                              lineno++;   /* ignore whitespace */

.                               return ERROR;

%%
