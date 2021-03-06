#ifndef _HASH_H_
#define _HASH_H_

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

    Hashfunc keyhash;
    Cmpfunc keycmp;
};

Hashtable *hash_create(
        int bucketCount,
        Hashfunc hash,
        Cmpfunc cmp);
void *hash_get(Hashtable *table, const void *key);
void hash_set(Hashtable *table, void *key, void *value);
void hash_remove(Hashtable *table, const void *key);
void hash_apply(Hashtable *table, void (*fun)(void *, void *, void *), void *env);
int hash_count(Hashtable *table);
List *hash_tolist(Hashtable *table);
void hash_clear(Hashtable *table);

#endif
