#include "debug.h"

void *debug_malloc(size_t size, const char *file, int line)
{
    void *res = malloc(size);
    if (res == NULL)
    {
        error("malloc failed at %s:%d", file, line);
        abort();
    }
    return res;
}
