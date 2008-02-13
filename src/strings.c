#include "strings.h"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "ScapegoatTree.h"

ScapegoatTree st_string_pool = ST_INIT(
    (EA_cmp)strcmp, (EA_dup)strdup, (EA_free)free, EA_no_dup, EA_no_free );

const char *internalize(const char *str)
{
    const void *key   = str;
    const void *value = NULL;
    ST_find_or_insert_entry(&st_string_pool, &key, &value);

    return (char*)key;
}

char *normalize(char *str)
{
    char *p, *q;
    bool space = true;

    for (p = q = str; *p; ++p)
    {
        if (isalnum(*p))
        {
            space = false;
            *q++ = isalpha(*p) ? toupper(*p) : *p;
        }
        else
        if (isspace(*p) && !space)
        {
            space = true;
            *q++ = ' ';
        }
    }

    /* Remove trailing space */
    if (q != str && q[-1] == ' ')
        --q;

    *q = '\0';
    return str;
}
