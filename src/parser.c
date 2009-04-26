#include "parser.h"
#include <string.h>

int symref_cmp(const SymbolRef *a, const SymbolRef *b)
{
    if (a->type != b->type)
        return (int)a->type - (int)b->type;
    else
        return a->index - b->index;
}

SymbolRefList *symrefs_create(size_t nref)
{
    size_t total_size = sizeof(SymbolRefList) + nref*sizeof(SymbolRef);
    SymbolRefList *refs = malloc(total_size);

    if (refs != NULL)
    {
        refs->nref = nref;
        refs->refs = (SymbolRef*)((char*)refs + sizeof(SymbolRefList));

        size_t n;
        for (n = 0; n < nref; ++n)
        {
            refs->refs[n].type = SYM_NONE;
            refs->refs[n].index = 0;
        }
    }

    return refs;
}

void symrefs_destroy(SymbolRefList *symrefs)
{
    free(symrefs);
}

int symrefs_cmp(const SymbolRefList *a, const SymbolRefList *b)
{
    int d = 0;

    size_t n;
    for (n = 0; n < a->nref && n < b->nref; ++n)
        if ((d = symref_cmp(&a->refs[n], &b->refs[n])) != 0)
            return d;

    if (a->nref < b->nref) d = -1;
    if (a->nref > b->nref) d = +1;

    return d;
}

GrammarRuleSet *ruleset_create(size_t nrule)
{
    size_t total_size = sizeof(GrammarRuleSet) + nrule*sizeof(SymbolRefList*);
    GrammarRuleSet *ruleset = malloc(total_size);

    if (ruleset != NULL)
    {
        ruleset->nrule  = nrule;
        ruleset->rules  = (SymbolRefList**)
                          ((char*)ruleset + sizeof(GrammarRuleSet));

        size_t n;
        for (n = 0; n < nrule; ++n)
            ruleset->rules[n] = NULL;
    }

    return ruleset;
}

void ruleset_destroy(GrammarRuleSet *ruleset)
{
    size_t n;
    for (n = 0; n < ruleset->nrule; ++n)
    {
        symrefs_destroy(ruleset->rules[n]);
        ruleset->rules[n] = NULL;
    }
    free(ruleset);
}

int ruleset_cmp(const GrammarRuleSet *a, const GrammarRuleSet *b)
{
    int d = 0;

    size_t n;
    for (n = 0; n < a->nrule && n < b->nrule; ++n)
        if ((d = symrefs_cmp(a->rules[n], b->rules[n])) != 0)
            return d;

    if (a->nrule < b->nrule) d = -1;
    if (a->nrule > b->nrule) d = +1;

    return d;
}

static int symrefs_cmp_indirect(const void *a, const void *b)
{
    return symrefs_cmp(*(SymbolRefList**)a, *(SymbolRefList**)b);
}

void ruleset_sort(GrammarRuleSet *ruleset)
{
    qsort(ruleset->rules, ruleset->nrule, sizeof(SymbolRefList*),
          &symrefs_cmp_indirect);
}

static bool match_rule(const GrammarRuleSet *grammar, const SymbolRefList *rule,
                       const int *i, const int *j, size_t pos);

static bool match_symbol(const GrammarRuleSet *grammar, const SymbolRef *symref,
                         const int *i, const int *j)
{
    if (symref->type == SYM_TERMINAL)
    {
        return j - i == 1 && *i == symref->index;
    }

    if (symref->type == SYM_NONTERMINAL)
    {
        const GrammarRuleSet *rules = grammar + symref->index;
        size_t n;
        for (n = 0; n < rules->nrule; ++n)
            if(match_rule(grammar, rules->rules[n], i, j, 0))
                return true;
        return false;
    }

    return false;
}

static bool match_rule(const GrammarRuleSet *grammar, const SymbolRefList *rule,
                       const int *i, const int *j, size_t pos)
{
    if (pos == rule->nref)
        return i == j;

    const int *k;
    for (k = i; k <= j; ++k)
    {
        if (match_symbol(grammar, &rule->refs[pos], i, k) &&
            match_rule(grammar, rule, k, j, pos + 1))
            return true;
    }

    return false;
}

bool parse_dumb(const GrammarRuleSet *grammar,
                const int *tokens, int ntoken, const SymbolRef *symref)
{
    return match_symbol(grammar, symref, tokens, tokens + ntoken);
}
