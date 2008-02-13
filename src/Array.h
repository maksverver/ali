#ifndef ARRAY_H_INCLUDED
#define ARRAY_H_INCLUDED

#include <stdlib.h>

typedef struct Array
{
    void *data;
    size_t size, capacity, el_size;
} Array;

#define AR_INIT(el_size) { NULL, 0, 0, el_size }

void AR_create(Array *ar, size_t el_size);
void AR_destroy(Array *ar);

Array *AR_alloc(size_t el_size);
void AR_free(Array *ar);

#define AR_data(ar)         ((ar)->data)
#define AR_size(ar)         ((ar)->size)
#define AR_at(ar, index)    ((void*)((char*)(ar)->data + (index)*(ar)->el_size))
#define AR_push(ar,data)    AR_append(ar, data)
#define AR_empty(ar)        ((ar)->size == 0)
#define AR_first(ar)        AR_at((ar), 0)
#define AR_last(ar)         AR_at((ar), (ar)->size - 1)

void AR_clear(Array *ar);
void AR_resize(Array *ar, size_t size);
void AR_reserve(Array *ar, size_t size);
void *AR_append(Array *ar, const void *data);
void AR_pop(Array *ar, void *data);

#endif /* ndef ARRAY_H_INCLUDED */
