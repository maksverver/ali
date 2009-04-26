#ifndef PARSER_H_INCLUDED
#define PARSER_H_INCLUDED

/* This file contains data structures to represent context-free grammars,
   functions to manipulate their parts, and a parser function that determines
   which non-terminals match a given string.

   Note that grammars may be ambiguous and contain empty rules.
*/

#include <stdlib.h>
#include <stdbool.h>

typedef enum SymbolType {
    SYM_NONE, SYM_TERMINAL, SYM_NONTERMINAL
} SymbolType;

typedef struct SymbolRef
{
    SymbolType  type;
    int         index;
} SymbolRef;

typedef struct SymbolRefList
{
    size_t    nref;
    SymbolRef *refs;
} SymbolRefList;

/* A set of production rules for a symbol.
    `sym` -> rules[n] (for 0 <= n < `nrule`) */
typedef struct GrammarRuleSet
{
    SymbolRef       sym;
    size_t          nrule;
    SymbolRefList   **rules;
} GrammarRuleSet;

int symref_cmp(const SymbolRef *a, const SymbolRef *b);

SymbolRefList *symrefs_create(size_t nref);
void symrefs_destroy(SymbolRefList *refs);
int symrefs_cmp(const SymbolRefList *a, const SymbolRefList *b);

GrammarRuleSet *ruleset_create(size_t nrule);
void ruleset_destroy(GrammarRuleSet *ruleset);
int ruleset_cmp(const GrammarRuleSet *a, const GrammarRuleSet *b);
void ruleset_sort(GrammarRuleSet *ruleset);

/* A very simple parser that determines if the start symbol matches the given
   list of tokens (completely). */
bool parse_dumb(const GrammarRuleSet *grammar,
                const int *tokens, int ntoken, const SymbolRef *symref);

#endif /* ndef PARSER_H_INCLUDED */
