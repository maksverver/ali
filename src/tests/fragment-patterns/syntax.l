%{
#include "grammar.tab.h"
%}

%%

[A-Z][A-Z0-9]*([ ]+[A-Z][A-Z0-9]*)* return FRAGMENT;

\(                              return LPAREN;
\)                              return RPAREN;
\[                              return LSQRBR;
\]                              return RSQRBR;
\/                              return SLASH;

#[^\n]*(\n|$)                   /* ignore single-line comment */
[ \t\r]+                        /* ignore whitespace */
\n                              /* ignore whitespace */

.                               return ERROR;

%%
