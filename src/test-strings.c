#include "strings.h"
#include <stdio.h>
#include <string.h>

const char *strings[] = {
    "foo",
    "bar",
    "baz",
    "quux",
    "w00t",
    "foo",
    "baz",
    "blaat",
    "woep",
    NULL
};

const char *commands[] = {
    "TEST",
    "FooBar",
    "\tDit is een test  ",
    "Bla\r123456-abc   xyzzy",
    "    a   B   c   ^&*   d  e  F ",
};

int main()
{
    const char **p, *q;

    for (p = strings; *p; ++p)
    {
        q = internalize(*p);
        printf("[%s] [%s] %d\n", *p, q, (int)q);
    }

    for (p = commands; *p; ++p)
    {
        printf("[%s]\n", normalize(strdup(*p)));
    }
    return 0;
}
