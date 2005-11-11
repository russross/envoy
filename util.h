#ifndef _UTIL_H_
#define _UTIL_H_

#include <sys/stat.h>
#include <string.h>
#include <gc/gc.h>
#include "9p.h"

/*
 * Generic hash tables
 */

struct hashtable {
    u32 size;
    u32 bucketCount;
    struct cons **buckets;

    u32 (*keyhash)(const void *);
    int (*keycmp)(const void *, const void *);
};

struct hashtable *hash_create(
        int bucketCount,
        u32 (*hash)(const void *),
        int (*cmp)(const void *, const void *));
inline void *hash_get(struct hashtable *table, const void *key);
inline void hash_set(struct hashtable *table, void *key, void *value);
inline void hash_remove(struct hashtable *table, const void *key);
void hash_apply(struct hashtable *table, void (*fun)(void *, void *));

/*
 * Generic vectors
 */

struct vector {
    u32 size;
    u32 next;
    void **array;
};

struct vector *vector_create(u32 size);
void *vector_get(struct vector *v, const u32 key);
int vector_test(struct vector *v, const u32 key);
u32 vector_alloc(struct vector *v, void *value);
void vector_set(struct vector *v, u32 key, void *value);
void vector_remove(struct vector *v, u32 key);
void *vector_get_remove(struct vector *v, u32 key);
void vector_apply(struct vector *v, void (*fun)(u32, void *));

/*
 * General utility functions
 */

static inline int min(int x, int y) {
    return x < y ? x : y;
}

static inline int max(int x, int y) {
    return x > y ? x : y;
}

static inline char *stringcopy(char *s) {
    char *res;
    assert(s != NULL);
    res = GC_MALLOC_ATOMIC(strlen(s) + 1);
    assert(res != NULL);
    strcpy(res, s);
    return res;
}

static inline char *substring(char *s, int start, int len) {
    char *res;
    assert(s != NULL);
    res = GC_MALLOC_ATOMIC(len + 1);
    assert(res != NULL);
    strncpy(res, s + start, len);
    res[len] = 0;
    return res;
}

static inline char *concatstrings(char *a, char *b) {
    char *res;
    assert(a != NULL && b != NULL);
    res = GC_MALLOC_ATOMIC(strlen(a) + strlen(b) + 1);
    assert(res != NULL);
    strcpy(res, a);
    strcat(res, b);
    return res;
}

static inline int emptystring(char *s) {
    return s == NULL || s[0] == 0;
}

char *dirname(char *path);
char *filename(char *path);
char *concatname(char *path, char *name);
char *resolvePath(char *base, char *ext, struct stat *info);

struct sockaddr_in *make_address(char *host, int port);
struct cons *splitpath(char *path);

#endif
