#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "util.h"
#include "config.h"
#include "state.h"
#include "lru.h"
#include "oid.h"

static u64 objectdir_findstart(u64 oid) {
    return (oid >> BITS_PER_DIR_OBJECTS) << BITS_PER_DIR_OBJECTS;
}

static u32 u64_hash(const u64 *n) {
    return generic_hash(n, sizeof(u64), 13);
}

static int u64_cmp(const u64 *a, const u64 *b) {
    if (a == b)
        return 0;
    else if (a < b)
        return -1;
    else
        return 1;
}

static void close_oids(struct objectdir *od) {
    int i;

    for (i = (1 << BITS_PER_DIR_OBJECTS) - 1; i >= 0; i--)
        if (od->oids[i] != NULL && od->oids[i]->fd >= 0)
            close(od->oids[i]->fd);
}

Lru *oid_init_lru(void) {
    return lru_new(
            OBJECTDIR_CACHE_SIZE,
            (u32 (*)(const void *)) u64_hash,
            (int (*)(const void *, const void *)) u64_cmp,
            (void (*)(void *)) close_oids);
}

List *oid_to_path(u64 oid) {
    List *path = NULL;
    int bitsleft = OID_BITS;
    char buff[16];

    while (bitsleft > 0) {
        u64 part;
        int bits;
        int chunkpart =
            ((bitsleft - BITS_PER_DIR_OBJECTS) / BITS_PER_DIR_DIRS) *
            BITS_PER_DIR_DIRS;
        /* three cases:
         * 1) odd bits at the beginning
         * 2) regular sized chunks in the middle
         * 3) odd bits at the end
         */
        if (bitsleft > chunkpart + BITS_PER_DIR_OBJECTS)
            bits = bitsleft - (chunkpart + BITS_PER_DIR_OBJECTS);
        else if (bitsleft == BITS_PER_DIR_OBJECTS)
            bits = BITS_PER_DIR_OBJECTS;
        else
            bits = BITS_PER_DIR_DIRS;

        bitsleft -= bits;
        part = oid >> bitsleft;
        oid &= (1LL << bitsleft) - 1LL;
        sprintf(buff, "%0*llx", (bits + 7) / 8, part);
        path = cons(stringcopy(buff), path);
    }

    return reverse(path);
}
