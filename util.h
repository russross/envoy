#ifndef _UTIL_H_
#define _UTIL_H_

#include <sys/stat.h>
#include "9p.h"

/*
 * Lisp-like lists
 */

struct cons {
    void *car;
    void *cdr;
};

inline int null(struct cons *cons);
inline void *car(struct cons *cons);
inline void *cdr(struct cons *cons);
inline void *caar(struct cons *cons);
inline void *cadr(struct cons *cons);
inline void *cdar(struct cons *cons);
inline void *cddr(struct cons *cons);
inline struct cons *cons(void *car, void *cdr);
inline void setcar(struct cons *cons, void *car);
inline void setcdr(struct cons *cons, void *cdr);

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

#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((x)>(y)?(x):(y))

inline int endswith(char *s, char *sub);
inline char *stringcopy(char *s);
inline char *substring(char *s, int start, int len);
inline int emptystring(char *s);

inline char *dirname(char *path);
inline char *filename(char *path);
inline char *concatname(char *path, char *name);

char *resolvePath(char *base, char *ext, struct stat *info);

#endif
