#ifndef _LIST_H_
#define _LIST_H_

#include <assert.h>
#include <gc.h>

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

struct cons *append_elt(struct cons *list, void *elt);

#endif
