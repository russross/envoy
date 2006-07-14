#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "hashtable.h"
#include "heap.h"
#include "lru.h"

static int lru_cmp(const struct lru_elt *a, const struct lru_elt *b) {
    if (a->count == b->count)
        return 0;
    if (a->count < b->count && b->count - a->count < HEAP_COUNTER_THRESHOLD)
        return -1;
    return 1;
}

Lru *lru_new(int size, Hashfunc keyhash, Cmpfunc keycmp,
        int (*resurrect)(void *), void (*cleanup)(void *))
{
    Lru *lru = GC_NEW(Lru);
    assert(lru != NULL);

    lru->table = hash_create(((size + 1) * 3) / 2, keyhash, keycmp);
    lru->heap = heap_new(2 * size + 1, (Cmpfunc) lru_cmp);
    lru->resurrect = resurrect;
    lru->cleanup = cleanup;
    lru->size = size;
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

static void lru_compress(Lru *lru) {
    while (lru->heap->count > hash_count(lru->table)) {
        struct lru_elt *elt = heap_remove(lru->heap);

        if (elt->value == NULL)
            continue;
        else if (elt->count != elt->refresh)
            elt->count = elt->refresh;
        else
            elt->count = elt->refresh = lru->counter++;

        heap_add(lru->heap, elt);
    }
}

void lru_add(Lru *lru, void *key, void *value) {
    struct lru_elt *elt;

    assert(lru != NULL);
    assert(key != NULL);
    assert(value != NULL);

    /* clean up and replace the old version if this key already exists */
    if ((elt = hash_get(lru->table, key)) != NULL) {
        if (lru->cleanup != NULL)
            lru->cleanup(elt->value);
        elt->value = value;
        elt->refresh = lru->counter++;
        return;
    }

    /* make sure the heap doesn't grow out of control */
    if (lru->heap->count >= lru->size * 2)
        lru_compress(lru);

    /* if we're full, clear a space */
    while (hash_count(lru->table) >= lru->size) {
        elt = heap_remove(lru->heap);

        /* has this item been touched since we saw it last? */
        if (elt->value == NULL) {
            /* this item was already removed */
        } else if (elt->count != elt->refresh) {
            /* re-insert it with its updated rank */
            elt->count = elt->refresh;
            heap_add(lru->heap, elt);
        } else if (lru->resurrect != NULL && lru->resurrect(elt->value)) {
            /* refresh this item and keep it */
            elt->count = elt->refresh = lru->counter++;
            heap_add(lru->heap, elt);
        } else {
            /* remove it from the hashtable and destroy it */
            hash_remove(lru->table, elt->key);
            if (lru->cleanup != NULL)
                lru->cleanup(elt->value);
        }
    }

    elt = GC_NEW(struct lru_elt);
    assert(elt != NULL);

    elt->count = elt->refresh = lru->counter++;
    elt->key = key;
    elt->value = value;

    heap_add(lru->heap, elt);
    hash_set(lru->table, key, elt);
}

void lru_clear(Lru *lru) {
    struct lru_elt *elt;

    assert(lru != NULL);

    while ((elt = heap_remove(lru->heap)) != NULL) {
        if (elt->value != NULL) {
            hash_remove(lru->table, elt->key);
            if (lru->cleanup != NULL)
                lru->cleanup(elt->value);
        }
    }

    assert(hash_count(lru->table) == 0);
}

void lru_remove(Lru *lru, void *key) {
    struct lru_elt *elt;

    assert(lru != NULL);

    /* the item remains in the heap, but is removed from the hashtable */
    if ((elt = hash_get(lru->table, key)) != NULL) {
        if (lru->cleanup != NULL)
            lru->cleanup(elt->value);
        hash_remove(lru->table, elt->key);
        elt->value = NULL;
    }
}
