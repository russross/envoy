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
    int test = 1;

    assert(table != NULL);
    assert(key != NULL);

    hash = table->keyhash(key) % table->bucketCount;
    elt = table->buckets[hash];

    while (!null(elt) && (test = table->keycmp(key, caar(elt))) < 0)
        elt = cdr(elt);

    return test ? NULL : cdar(elt);
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
    List *prev = NULL;
    int test = 1;

    assert(table != NULL);
    assert(key != NULL);
    assert(value != NULL);

    hash = table->keyhash(key) % table->bucketCount;

    /* check if this key already exists */
    elt = table->buckets[hash];
    while (!null(elt) && (test = table->keycmp(key, caar(elt))) < 0) {
        prev = elt;
        elt = cdr(elt);
    }

    if (!test) {
        setcar(elt, cons(key, value));
        return;
    } else if (null(prev)) {
        table->buckets[hash] = cons(cons(key, value), table->buckets[hash]);
    } else {
        setcdr(prev, cons(cons(key, value), elt));
    }

    table->size++;
    if (table->size > (table->bucketCount * 2) / 3)
        hash_size_double(table);
}

void hash_remove(Hashtable *table, const void *key) {
    u32 hash;
    List *elt;
    List *prev = NULL;
    int test = 1;

    assert(table != NULL);
    assert(key != NULL);

    hash = table->keyhash(key) % table->bucketCount;

    elt = table->buckets[hash];
    while (!null(elt) && (test = table->keycmp(key, caar(elt))) < 0) {
        prev = elt;
        elt = cdr(elt);
    }

    if (!test) {
        return;
    } else if (null(prev)) {
        table->buckets[hash] = cdr(elt);
    } else {
        setcdr(prev, cdr(elt));
    }

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
