#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "hashtable.h"
#include "heap.h"
#include "lru.h"

static int lru_cmp(struct lru_elt *a, struct lru_elt *b) {
    if (a->count == b->count)
        return 0;
    if (a->count < b->count && b->count - a->count < HEAP_COUNTER_THRESHOLD)
        return -1;
    return 1;
}

Lru *lru_new(int size, u32 (*keyhash)(const void *),
        int (*keycmp)(const void *, const void *), void (*cleanup)(void *))
{
    Lru *lru = GC_NEW(Lru);
    assert(lru != NULL);

    lru->table = hash_create((size + 1) * 3 / 2, keyhash, keycmp);
    lru->heap = heap_new(size + 1, (int (*)(void *, void *)) lru_cmp);
    lru->cleanup = cleanup;
    lru->size = size;
    lru->count = 0;
    lru->counter = 0;

    return lru;
}

void *lru_get(Lru *lru, void *key) {
    struct lru_elt *elt;

    assert(lru != NULL);

    if ((elt = hash_get(lru->table, key)) == NULL)
        return NULL;

    /* mark it as the most recent item touched */
    elt->refresh = lru->counter++;

    return elt->value;
}

void lru_add(Lru *lru, void *key, void *value) {
    struct lru_elt *elt;

    assert(lru != NULL);

    /* clean up and replace the old version if this key already exists */
    if ((elt = hash_get(lru->table, key)) != NULL) {
        lru->cleanup(elt->value);
        elt->value = value;
        elt->refresh = lru->counter++;
        return;
    }

    /* if we're full, clear a space */
    while (lru->count >= lru->size) {
        elt = heap_remove(lru->heap);

        /* has this item has been touched since we saw it last? */
        if (elt->count != elt->refresh) {
            /* re-insert it with its updated rank */
            elt->count = elt->refresh;
            heap_add(lru->heap, elt);
        } else {
            /* remove it from the hashtable and destroy it */
            hash_remove(lru->table, elt->key);
            lru->cleanup(elt->value);
            lru->count--;
        }
    }

    elt = GC_NEW(struct lru_elt);
    assert(elt != NULL);

    elt->count = elt->refresh = lru->counter++;
    elt->key = key;
    elt->value = value;

    heap_add(lru->heap, elt);
    hash_set(lru->table, key, elt);
    lru->count++;
}

void lru_clear(Lru *lru) {
    struct lru_elt *elt;

    assert(lru != NULL);

    while ((elt = heap_remove(lru->heap)) != NULL) {
        hash_remove(lru->table, elt->key);
        lru->cleanup(elt->value);
        lru->count--;
    }

    assert(lru->count == 0);
}
