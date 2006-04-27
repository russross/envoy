#ifndef _LRU_H_
#define _LRU_H_

#include "types.h"
#include "9p.h"
#include "hashtable.h"
#include "heap.h"

#define HEAP_COUNTER_THRESHOLD 0x80000000L

/* A Least-Recently-Used (LRU) cache.
 * Items are indexed using a hashtable, and they are also put into a
 * binary heap.  Items are tracked by two values: the time of insert
 * and time of most recent access.  When an item is pulled off the
 * heap, the two values are compared.  If equal, the item is cleaned
 * up and discarded, otherwise the insert time is updated to the
 * most recent access time and the item is put back in the heap.
 *
 * Age is tracked by a u32 counter.  If the difference between two
 * values is over 2^31 then it is assumed that the counter wrapped
 * around, and the lower value is considered more recent.  This
 * scheme could fail if the items are accessed 2^31 times between
 * inventory cycles.
 */
struct lru {
    int count;
    int size;
    u32 counter;
    Hashtable *table;
    Heap *heap;
    int (*resurrect)(void *);
    void (*cleanup)(void *);
};

struct lru_elt {
    u32 count;
    u32 refresh;
    void *key;
    void *value;
};

Lru *lru_new(
        int size,
        u32 (*keyhash)(const void *),
        int (*keycmp)(const void *, const void *),
        int (*resurrect)(void *),
        void (*cleanup)(void *));
void *lru_get(Lru *lru, void *key);
void lru_add(Lru *lru, void *key, void *value);
void lru_clear(Lru *lru);
void lru_remove(Lru *lru, void *key);

#endif
