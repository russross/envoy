#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <netdb.h>
#include <assert.h>
#include <gc/gc.h>
#include "9p.h"
#include "list.h"
#include "util.h"

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

/*
 * Generic vectors
 */

struct vector *vector_create(u32 size) {
    struct vector *v = GC_NEW(struct vector);

    assert(v != NULL);
    v->array = GC_MALLOC(sizeof(void *) * size);
    assert(v->array != NULL);
    v->size = size;
    v->next = 0;

    return v;
}

void *vector_get(struct vector *v, const u32 key) {
    if (key >= v->next)
        return NULL;

    return v->array[key];
}

int vector_test(struct vector *v, const u32 key) {
    return key < v->next && v->array[key] != NULL;
}

static void vector_double(struct vector *v) {
    void **old = v->array;
    v->array = GC_MALLOC(sizeof(void *) * v->size * 2);
    assert(v->array != NULL);
    memcpy(v->array, old, sizeof(void *) * v->size);
    v->size *= 2;
}

u32 vector_alloc(struct vector *v, void *value) {
    u32 i;

    for (i = 0; i < v->next; i++) {
        if (v->array[i] == NULL) {
            v->array[i] = value;
            return i;
        }
    }

    if (v->next == v->size)
        vector_double(v);

    v->array[v->next] = value;

    return v->next++;
}

void vector_set(struct vector *v, u32 key, void *value) {
    while (key >= v->size)
        vector_double(v);

    while (v->next <= key)
        v->array[v->next++] = NULL;

    v->array[key] = value;
}

void vector_remove(struct vector *v, u32 key) {
    assert(key < v->next && v->array[key] != NULL);
    v->array[key] = NULL;
    while (v->next > 0 && v->array[v->next - 1] == NULL)
        v->next--;
}

void vector_apply(struct vector *v, void (*fun)(u32, void *)) {
    u32 i;
    
    assert(v != NULL && fun != NULL);

    for (i = 0; i < v->next; i++)
        if (v->array[i] != NULL)
            fun(i, v->array[i]);
}

/*****************************************************************************/

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

char *dirname(char *path) {
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

char *filename(char *path) {
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

char *concatname(char *path, char *name) {
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

struct sockaddr_in *make_address(char *host, int port) {
    struct sockaddr_in *addr = GC_NEW_ATOMIC(struct sockaddr_in);
    struct hostent *ent = gethostbyname(host);

    assert(addr != NULL);
    assert(ent != NULL);
    assert(ent->h_addrtype == AF_INET && ent->h_length == 4);
    assert(ent->h_addr_list[0] != NULL && ent->h_addr_list[1] == NULL);

    addr->sin_family = AF_INET;
    addr->sin_port = port;
    addr->sin_addr = *((struct in_addr *) ent->h_addr_list[0]);

    return addr;
}

struct cons *splitpath(char *path) {
    struct cons *res = NULL;

    assert(path != NULL);
    
    while (*path) {
        int i, j, k;

        for (i = 0; path[i] == '/'; i++);
        for (j = i; path[j] != 0 && path[j] != '/'; j++);
        for (k = j; path[k] == '/'; k++);

        if (j - i > 0)
            res = cons(substring(path, i, j - i), res);
        else
            break;

        path = &path[k];
    }

    return reverse(res);
}
