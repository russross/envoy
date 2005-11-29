#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "vector.h"

/*
 * Generic vectors
 */

Vector *vector_create(u32 size) {
    Vector *v = GC_NEW(Vector);

    assert(v != NULL);
    v->array = GC_MALLOC(sizeof(void *) * size);
    assert(v->array != NULL);
    v->size = size;
    v->next = 0;

    return v;
}

void *vector_get(Vector *v, const u32 key) {
    if (key >= v->next)
        return NULL;

    return v->array[key];
}

int vector_test(Vector *v, const u32 key) {
    return key < v->next && v->array[key] != NULL;
}

static void vector_double(Vector *v) {
    void **old = v->array;
    v->array = GC_MALLOC(sizeof(void *) * v->size * 2);
    assert(v->array != NULL);
    memcpy(v->array, old, sizeof(void *) * v->size);
    v->size *= 2;
    GC_free(old);
}

u32 vector_alloc(Vector *v, void *value) {
    u32 i;

    for (i = 0; i < v->next; i++) {
        if (v->array[i] == NULL) {
            v->array[i] = value;
            return i;
        }
    }

    if (v->next == v->size)
        vector_double(v);

    v->array[v->next] = value;

    return v->next++;
}

void vector_set(Vector *v, u32 key, void *value) {
    while (key >= v->size)
        vector_double(v);

    while (v->next <= key)
        v->array[v->next++] = NULL;

    v->array[key] = value;
}

void *vector_get_remove(Vector *v, const u32 key) {
    void *res;

    if (key >= v->next || v->array[key] == NULL)
        return NULL;

    res = v->array[key];
    v->array[key] = NULL;
    while (v->next > 0 && v->array[v->next - 1] == NULL)
        v->next--;
    return res;
}

void vector_remove(Vector *v, const u32 key) {
    vector_get_remove(v, key);
}

void vector_apply(Vector *v, void (*fun)(u32, void *)) {
    u32 i;

    assert(v != NULL && fun != NULL);

    for (i = 0; i < v->next; i++)
        if (v->array[i] != NULL)
            fun(i, v->array[i]);
}
