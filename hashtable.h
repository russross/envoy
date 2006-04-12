#ifndef _HASHTABLE_H_
#define _HASHTABLE_H_

#include "types.h"
#include "9p.h"
#include "list.h"

/*
 * Generic hash tables
 */

struct hashtable {
    u32 size;
    u32 bucketCount;
    List **buckets;

    u32 (*keyhash)(const void *);
    int (*keycmp)(const void *, const void *);
};

Hashtable *hash_create(
        int bucketCount,
        u32 (*hash)(const void *),
        int (*cmp)(const void *, const void *));
void *hash_get(Hashtable *table, const void *key);
void hash_set(Hashtable *table, void *key, void *value);
void hash_remove(Hashtable *table, const void *key);
void hash_apply(Hashtable *table, void (*fun)(void *, void *));

#endif
