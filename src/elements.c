#include "elements.h"

void *EA_no_dup(const void *arg)
{
    return (void*)arg;
}

void EA_no_free(void *arg /* unused */)
{
}
