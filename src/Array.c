#include "Array.h"
#include "debug.h"
#include <string.h>

void AR_create(Array *ar, size_t el_size)
{
    ar->data     = NULL;
    ar->size     = 0;
    ar->capacity = 0;
    ar->el_size  = el_size;
}

void AR_destroy(Array *ar)
{
    AR_clear(ar);
}

Array *AR_alloc(size_t el_size)
{
    Array *res = malloc(sizeof(Array));
    AR_create(res, el_size);
    return res;
}

void AR_free(Array *ar)
{
    AR_destroy(ar);
    free(ar);
}

void AR_clear(Array *ar)
{
    free(ar->data);
    ar->data = NULL;
    ar->size = ar->capacity = 0;
}

void AR_resize(Array *ar, size_t size)
{
    size_t cap = ar->capacity;

    /* If capacity is larger than twice the size of the array,
       force reallocation of data to prevent wasted space. */
    if (cap > 8 && size < cap/2)
        cap = 0;

    /* If requested size is larger than capacity... */
    if (size > cap)
    {
        /* Find next power of 2 that is no smaller than size,
           with a minimum of 8 to avoid very small allocations. */
        if (size <= 8)
        {
            cap = 8;
        }
        else
        {
            cap = 2*size;
            while (cap&(cap - 1)) cap &= cap - 1;
        }

        /* Reallocate */
        ar->capacity = cap;
        ar->data     = realloc(ar->data, cap*ar->el_size);
    }

    ar->size = size;
}

void AR_reserve(Array *ar, size_t size)
{
    /* Ensure desired capacity is not below current size. */
    if (size < ar->size)
        size = ar->size;

    ar->capacity = size;
    ar->data     = realloc(ar->data, size*ar->el_size);
}

void *AR_append(Array *ar, const void *data)
{
    size_t pos = ar->size;
    AR_resize(ar, pos + 1);
    return memcpy(AR_at(ar, pos), data, ar->el_size);
}

void AR_pop(Array *ar, void *data)
{
    size_t pos = ar->size - 1;
    if (data != NULL)
        memcpy(data, AR_at(ar, pos), ar->el_size);
    AR_resize(ar, pos);
}
