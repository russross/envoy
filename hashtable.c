#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
#include "config.h"

/*
 * Generic hash tables
 */

Hashtable *hash_create(
        int bucketCount,
        u32 (*keyhash)(const void *),
        int (*keycmp)(const void *, const void *))
{
    Hashtable *table;

    assert(keyhash != NULL);
    assert(keycmp != NULL);
    assert(bucketCount > 0);

    table = GC_NEW(Hashtable);
    assert(table != NULL);

    table->size = 0;
    table->bucketCount = bucketCount;
    table->keyhash = keyhash;
    table->keycmp = keycmp;

    table->buckets = GC_MALLOC(sizeof(List *) * bucketCount);
    assert(table->buckets != NULL);

    return table;
}

void *hash_get(Hashtable *table, const void *key) {
    u32 hash;
    List *elt;

    assert(table != NULL);
    assert(key != NULL);

    hash = table->keyhash(key) % table->bucketCount;
    elt = table->buckets[hash];

    while (!null(elt) && table->keycmp(key, caar(elt)))
        elt = cdr(elt);

    return null(elt) ? NULL : cdar(elt);
}

static void hash_size_double(Hashtable *table) {
    int i;
    List *elt;
    int newBucketCount = table->bucketCount * 2;
    List **newBuckets =
        GC_MALLOC(sizeof(List *) * newBucketCount);

    assert(newBuckets != NULL);

    for (i = 0; i < table->bucketCount; i++) {
        elt = table->buckets[i];
        while (!null(elt)) {
            u32 hash = table->keyhash(caar(elt)) % newBucketCount;
            newBuckets[hash] = cons(car(elt), newBuckets[hash]);
            elt = cdr(elt);
        }
    }

    table->bucketCount = newBucketCount;
    table->buckets = newBuckets;
    if (DEBUG_VERBOSE)
        printf("hash_size_double: new size = %d buckets\n", newBucketCount);
}

void hash_set(Hashtable *table, void *key, void *value) {
    u32 hash;
    List *elt;

    assert(table != NULL);
    assert(key != NULL);
    assert(value != NULL);

    hash = table->keyhash(key) % table->bucketCount;

    /* check if this key already exists */
    elt = table->buckets[hash];
    while (!null(elt) && table->keycmp(key, caar(elt)))
        elt = cdr(elt);

    if (!null(elt)) {
        setcar(elt, cons(key, value));
    } else {
        table->buckets[hash] = cons(cons(key, value), table->buckets[hash]);
        table->size++;
        if (table->size > (table->bucketCount * 2) / 3)
            hash_size_double(table);
    }
}

void hash_remove(Hashtable *table, const void *key) {
    u32 hash;
    List *elt, *prev;

    assert(table != NULL);
    assert(key != NULL);

    hash = table->keyhash(key) % table->bucketCount;
    prev = NULL;
    elt = table->buckets[hash];

    while (!null(elt) && table->keycmp(key, caar(elt))) {
        prev = elt;
        elt = cdr(elt);
    }

    /* element didn't exist */
    if (null(elt))
        return;

    /* was it the first element? */
    if (null(prev))
        table->buckets[hash] = cdr(elt);
    else
        setcdr(prev, cdr(elt));

    table->size--;
}

void hash_apply(Hashtable *table, void (*fun)(void *, void *, void *),
        void *env)
{
    int i;
    List *elt;

    assert(table != NULL);

    for (i = 0; i < table->bucketCount; i++) {
        elt = table->buckets[i];
        while (!null(elt)) {
            fun(env, caar(elt), cdar(elt));
            elt = cdr(elt);
        }
    }
}

int hash_count(Hashtable *table) {
    return table->size;
}

List *hash_tolist(Hashtable *table) {
    int i;
    List *res = NULL;
    List *elt;

    assert(table != NULL);

    for (i = 0; i < table->bucketCount; i++)
        for (elt = table->buckets[i]; !null(elt); elt = cdr(elt))
            res = cons(cdar(elt), res);

    return res;
}

void hash_clear(Hashtable *table) {
    int i;

    assert(table != NULL);

    for (i = 0; i < table->bucketCount; i++)
        table->buckets[i] = NULL;

    table->size = 0;
}
