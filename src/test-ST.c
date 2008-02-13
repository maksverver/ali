#include <stdio.h>
#include <string.h>
#include "ScapegoatTree.h"

ScapegoatTree st;

static int callback(void *arg, const void *key, const void *value)
{
    fputs(key, stdout);
    return 0;
}

int main()
{
    char line[1024];
    ST_create(&st, (EA_cmp)strcmp, (EA_dup)strdup, (EA_free)free, NULL, NULL);
    while (fgets(line, sizeof(line), stdin))
        ST_insert(&st, line, NULL);
    printf("%d unique lines:\n", (int)ST_size(&st));
    ST_iterate(&st, callback, NULL);
    ST_destroy(&st);
    return 0;
}
