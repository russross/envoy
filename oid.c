#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
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

static void close_dir(struct objectdir *od) {
    int i;

    for (i = (1 << BITS_PER_DIR_OBJECTS) - 1; i >= 0; i--) {
        if (od->oids[i] != NULL && od->oids[i]->fd >= 0) {
            close(od->oids[i]->fd);
            od->oids[i]->fd = -1;
        }
    }
}

static void close_oid(struct oid *oid) {
    if (oid->fd >= 0) {
        close(oid->fd);
        oid->fd = -1;
    }
}

Lru *oid_init_dir_lru(void) {
    return lru_new(
            OBJECTDIR_CACHE_SIZE,
            (u32 (*)(const void *)) u64_hash,
            (int (*)(const void *, const void *)) u64_cmp,
            (void (*)(void *)) close_dir);
}

Lru *oid_init_fd_lru(void) {
    return lru_new(
            FD_CACHE_SIZE,
            (u32 (*)(const void *)) u64_hash,
            (int (*)(const void *, const void *)) u64_cmp,
            (void (*)(void *)) close_oid);
}

List *oid_to_path(u64 oid) {
    List *path = NULL;
    int bitsleft = OID_BITS;
    char buff[16];

    while (bitsleft > 0) {
        u64 part;
        int chunkpart =
            ((bitsleft - BITS_PER_DIR_OBJECTS) / BITS_PER_DIR_DIRS) *
            BITS_PER_DIR_DIRS;
        int bits;

        /* three cases:
         * 1) odd bits at the beginning
         * 2) regular sized chunks in the middle
         * 3) odd bits at the end */
        if (bitsleft > chunkpart + BITS_PER_DIR_OBJECTS)
            bits = bitsleft - (chunkpart + BITS_PER_DIR_OBJECTS);
        else if (bitsleft == BITS_PER_DIR_OBJECTS)
            bits = BITS_PER_DIR_OBJECTS;
        else
            bits = BITS_PER_DIR_DIRS;

        bitsleft -= bits;
        part = oid >> bitsleft;
        oid &= (1LL << bitsleft) - 1LL;
        sprintf(buff, "%0*llx", (bits + 3) / 4, part);
        path = cons(stringcopy(buff), path);
    }

    return reverse(path);
}

u64 path_to_oid(List *path) {
    u64 oid = 0LL;
    int bitsleft = OID_BITS;

    while (bitsleft > 0) {
        int part;
        char *elt = car(path);
        int chunkpart =
            ((bitsleft - BITS_PER_DIR_OBJECTS) / BITS_PER_DIR_DIRS) *
            BITS_PER_DIR_DIRS;
        int bits;

        assert(sscanf(elt, "%x", &part) == 1);
        path = cdr(path);

        /* three cases:
         * 1) odd bits at the beginning
         * 2) regular sized chunks in the middle
         * 3) odd bits at the end */
        if (bitsleft > chunkpart + BITS_PER_DIR_OBJECTS)
            bits = bitsleft - (chunkpart + BITS_PER_DIR_OBJECTS);
        else if (bitsleft == BITS_PER_DIR_OBJECTS)
            bits = BITS_PER_DIR_OBJECTS;
        else
            bits = BITS_PER_DIR_DIRS;

        bitsleft -= bits;

        oid |= ((u64) part) << bitsleft;
    }

    return oid;
}

u64 oid_find_next_available(void) {
    char *dir = objectroot;
    List *minpath_full = oid_to_path(0LL);
    List *minpath_lst = minpath_full;
    List *maxpath_full = oid_to_path(0xFFFFFFFFFFFFFFFFLL);
    List *maxpath_lst = maxpath_full;
    List *lastpath = NULL;

    while (!null(maxpath_lst) && !null(cdr(maxpath_lst))) {
        /* scan the current directory */
        char *maxpath = car(maxpath_lst);
        char *high = NULL;
        DIR *dd = opendir(dir);
        assert(dd != NULL);

        for (;;) {
            int n;
            char c;
            struct dirent *ent = readdir(dd);

            if (ent == NULL) {
                closedir(dd);
                dd = NULL;
                break;
            }

            /* test this filename:
             * 1) is it the right length?
             * 2) is it a valid hex number?
             * 3) is it within the acceptable range?
             * 4) does it beat our previous high value? */
            if (strlen(ent->d_name) != strlen(maxpath) ||
                    sscanf(ent->d_name, "%x%c", &n, &c) != 1 ||
                    strcmp(ent->d_name, maxpath) > 0 ||
                    (high != NULL && strcmp(ent->d_name, high) <= 0))
            {
                continue;
            }

            high = stringcopy(ent->d_name);
        }

        if (high == NULL) {
            while (!null(minpath_lst)) {
                lastpath = cons(car(minpath_lst), lastpath);
                minpath_lst = cdr(minpath_lst);
            }
            return path_to_oid(reverse(lastpath));
        } else {
            maxpath_lst = cdr(maxpath_lst);
            minpath_lst = cdr(minpath_lst);
            lastpath = cons(high, lastpath);
            dir = concatname(dir, high);
        }
    }

    lastpath = cons(car(minpath_lst), lastpath);
    return path_to_oid(reverse(lastpath)) + (1 << BITS_PER_DIR_OBJECTS);
}

int oid_reserve_block(u64 *oid, int *count) {
    char *dir = objectroot;
    struct stat info;
    List *path;

    *oid = state->oid_next_available;
    *count = 1 << BITS_PER_DIR_OBJECTS;
    state->oid_next_available += (u64) *count;

    /* just in case 64 bits isn't enough... */
    if (state->oid_next_available < *oid)
        return -1;

    path = oid_to_path(*oid);

    while (!null(path) && !null(cdr(path))) {
        dir = concatname(dir, car(path));
        path = cdr(path);
        if (lstat(dir, &info) < 0) {
            if (mkdir(dir, 0755) < 0)
                return -1;
        } else if (!S_ISDIR(info.st_mode)) {
            return -1;
        }
    }

    return 0;
}
