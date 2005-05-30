#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <assert.h>
#include <gc.h>
#include "9p.h"
#include "util.h"

/*
 * Lisp-like lists
 */

inline int null(struct cons *cons) {
    return cons == NULL;
}

inline void *car(struct cons *cons) {
    assert(!null(cons));

    return cons->car;
}

inline void *cdr(struct cons *cons) {
    assert(!null(cons));

    return cons->cdr;
}

inline struct cons *cons(void *car, void *cdr) {
    struct cons *res = GC_NEW(struct cons);
    
    assert(res != NULL);

    res->car = car;
    res->cdr = cdr;
    
    return res;
}

inline void *caar(struct cons *cons) {
    return car(car(cons));
}

inline void *cadr(struct cons *cons) {
    return car(cdr(cons));
}

inline void *cdar(struct cons *cons) {
    return cdr(car(cons));
}

inline void *cddr(struct cons *cons) {
    return cdr(cdr(cons));
}

inline void setcar(struct cons *cons, void *car) {
    assert(!null(cons));

    cons->car = car;
}

inline void setcdr(struct cons *cons, void *cdr) {
    assert(!null(cons));

    cons->cdr = cdr;
}

/*
 * Generic hash tables
 */

struct hashtable *hash_create(
        int bucketCount,
        u32 (*keyhash)(const void *),
        int (*keycmp)(const void *, const void *))
{
    struct hashtable *table;

    assert(keyhash != NULL);
    assert(keycmp != NULL);
    assert(bucketCount > 0);
    
    table = GC_NEW(struct hashtable);
    assert(table != NULL);

    table->size = 0;
    table->bucketCount = bucketCount;
    table->keyhash = keyhash;
    table->keycmp = keycmp;

    table->buckets = GC_MALLOC(sizeof(struct cons *) * bucketCount);
    assert(table->buckets != NULL);

    return table;
}

inline void *hash_get(struct hashtable *table, const void *key) {
    u32 hash;
    struct cons *elt;

    assert(table != NULL);
    assert(key != NULL);

    hash = table->keyhash(key) % table->bucketCount;
    elt = table->buckets[hash];

    while (!null(elt) && table->keycmp(key, caar(elt)))
        elt = cdr(elt);

    return null(elt) ? NULL : cdar(elt);
}

static void hash_size_double(struct hashtable *table) {
    int i;
    struct cons *elt;
    int newBucketCount = table->bucketCount * 2;
    struct cons **newBuckets =
        GC_MALLOC(sizeof(struct cons *) * newBucketCount);

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
}

inline void hash_set(struct hashtable *table, void *key, void *value) {
    u32 hash;
    struct cons *elt;

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
        if (table->size > table->bucketCount * 2 / 3)
            hash_size_double(table);
    }
}

inline void hash_remove(struct hashtable *table, const void *key) {
    u32 hash;
    struct cons *elt, *prev;

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

void hash_apply(struct hashtable *table, void (*fun)(void *, void *)) {
    int i;
    struct cons *elt;

    assert(table != NULL);

    for (i = 0; i < table->bucketCount; i++) {
        elt = table->buckets[i];
        while (!null(elt)) {
            fun(caar(elt), cdar(elt));
            elt = cdr(elt);
        }
    }
}

inline int endswith(char *s, char *sub) {
    int slen, sublen;
    if (s == NULL || sub == NULL)
        return 0;

    slen = strlen(s);
    sublen = strlen(sub);

    if (slen < sublen)
        return 0;
    
    return !strcmp(s + (slen - sublen), sub);
}

inline char *stringcopy(char *s) {
    char *res;
    assert(s != NULL);
    res = GC_MALLOC_ATOMIC(strlen(s) + 1);
    assert(res != NULL);
    strcpy(res, s);
    return res;
}

inline char *substring(char *s, int start, int len) {
    char *res;
    assert(s != NULL);
    res = GC_MALLOC_ATOMIC(len + 1);
    assert(res != NULL);
    strncpy(res, s + start, len);
    res[len] = 0;
    return res;
}

inline int emptystring(char *s) {
    return s == NULL || s[0] == 0;
}

inline char *dirname(char *path) {
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

inline char *filename(char *path) {
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

inline char *concatname(char *path, char *name) {
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

/* we know base to be well-formed, with a leading slash, no trailing slash */
char *resolvePath(char *base, char *ext, struct stat *info) {
    int first = 1;
    int linkdepth = 16;

    while (*ext) {
        int i, j, k;
        char *oldbase;

        for (i = 0; ext[i] == '/'; i++);
        for (j = i; ext[j] != 0 && ext[j] != '/'; j++);
        for (k = j; ext[k] == '/'; k++);

        /* empty string */
        if (i == j)
            break;

        /* starts with / */
        if (i != 0)
            base = "/";

        oldbase = base;

        if (j - i == 1 && ext[i] == '.') {
            /* nothing to do */
        } else if (j - i == 2 && ext[i] == '.' && ext[i+1] == '.') {
            /* back a directory */
            base = dirname(base);
        } else {
            /* copy over one directory part */
            base = concatname(base, substring(ext, i, j - i));
        }

        ext += k;

        if (lstat(base, info) < 0)
            return NULL;

        if (S_ISLNK(info->st_mode)) {
            char *lnk = GC_MALLOC_ATOMIC(info->st_size + strlen(ext) + 2);
            assert(lnk != NULL);

            if (linkdepth <= 0 || readlink(base, lnk, info->st_size) < 0)
                return NULL;

            lnk[info->st_size] = 0;
            strcat(lnk, "/");
            strcat(lnk, ext);
            ext = lnk;
            linkdepth--;
            base = oldbase;
        } else if (!S_ISDIR(info->st_mode)) {
            return NULL;
        }

        first = 0;
    }

    if (first) {
        if (lstat(base, info) < 0)
            return NULL;

        if (!S_ISDIR(info->st_mode))
            return NULL;
    }

    return base;
}
