#include "strings.h"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

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
    if (q != str && isspace(q[-1]))
        --q;

    *q = '\0';
    return str;
}
