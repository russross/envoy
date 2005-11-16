#ifndef _VECTOR_H_
#define _VECTOR_H_

#include "9p.h"
#include "types.h"

/*
 * Generic vectors
 */

struct vector {
    u32 size;
    u32 next;
    void **array;
};

Vector *vector_create(u32 size);
void *vector_get(Vector *v, const u32 key);
int vector_test(Vector *v, const u32 key);
u32 vector_alloc(Vector *v, void *value);
void vector_set(Vector *v, u32 key, void *value);
void vector_remove(Vector *v, const u32 key);
void *vector_get_remove(Vector *v, const u32 key);
void vector_apply(Vector *v, void (*fun)(u32, void *));

#endif
