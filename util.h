#ifndef _UTIL_H_
#define _UTIL_H_

#include <sys/stat.h>
#include <string.h>
#include <gc.h>
#include "9p.h"

/*
 * Lisp-like lists
 */

struct cons {
    void *car;
    void *cdr;
};

static inline int null(struct cons *cons) {
    return cons == NULL;
}

static inline void *car(struct cons *cons) {
    assert(!null(cons));

    return cons->car;
}

static inline void *cdr(struct cons *cons) {
    assert(!null(cons));

    return cons->cdr;
}

static inline struct cons *cons(void *car, void *cdr) {
    struct cons *res = GC_NEW(struct cons);
    
    assert(res != NULL);

    res->car = car;
    res->cdr = cdr;
    
    return res;
}

static inline void *caar(struct cons *cons) {
    return car(car(cons));
}

static inline void *cadr(struct cons *cons) {
    return car(cdr(cons));
}

static inline void *cdar(struct cons *cons) {
    return cdr(car(cons));
}

static inline void *cddr(struct cons *cons) {
    return cdr(cdr(cons));
}

static inline void setcar(struct cons *cons, void *car) {
    assert(!null(cons));

    cons->car = car;
}

static inline void setcdr(struct cons *cons, void *cdr) {
    assert(!null(cons));

    cons->cdr = cdr;
}

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

static inline int emptystring(char *s) {
    return s == NULL || s[0] == 0;
}

static inline char *dirname(char *path) {
    char *slash, *base;

    if (!strcmp(path, "/"))
        return path;

    slash = strrchr(path, '/');
    if (!slash)
        return path;
    assert(slash[1] != 0);
    base = GC_MALLOC_ATOMIC(slash - path + 1);
    assert(base != NULL);
    strncpy(base, path, slash - path);
    base[slash - path] = 0;

    return base;
}

static inline char *filename(char *path) {
    char *slash, *name;

    /* root directory is a special case */
    if (!strcmp(path, "/"))
        return path;

    slash = strrchr(path, '/');
    if (!slash)
        return path;
    ++slash;
    assert(*slash != 0);

    name = GC_MALLOC_ATOMIC(strlen(slash) + 1);
    strcpy(name, slash);

    return name;
}

static inline char *concatname(char *path, char *name) {
    int pathlen = strlen(path);
    int namelen = strlen(name);
    char *res = GC_MALLOC_ATOMIC(pathlen + namelen + 2);

    assert(res != NULL);

    strcpy(res, path);
    assert(pathlen == 1 || res[pathlen-1] != '/');
    if (path[pathlen-1] != '/')
        strcat(res, "/");
    strcat(res, name);

    return res;
}

char *resolvePath(char *base, char *ext, struct stat *info);

#endif
