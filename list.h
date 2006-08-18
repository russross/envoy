#ifndef _LIST_H_
#define _LIST_H_

#include "types.h"
#include "9p.h"

/*
 * Lisp-like lists
 */

struct list {
    void *car;
    void *cdr;
};

static inline int null(List *list) {
    return list == NULL;
}

static inline void *car(List *list) {
    assert(!null(list));

    return list->car;
}

static inline void *cdr(List *list) {
    assert(!null(list));

    return list->cdr;
}

static inline List *cons(void *car, void *cdr) {
    List *list = GC_NEW(List);

    assert(list != NULL);

    list->car = car;
    list->cdr = cdr;

    return list;
}

static inline void *caar(List *list) {
    return car(car(list));
}

static inline void *cadr(List *list) {
    return car(cdr(list));
}

static inline void *cdar(List *list) {
    return cdr(car(list));
}

static inline void *cddr(List *list) {
    return cdr(cdr(list));
}

static inline void setcar(List *list, void *car) {
    assert(!null(list));

    list->car = car;
}

static inline void setcdr(List *list, void *cdr) {
    assert(!null(list));

    list->cdr = cdr;
}

List *append_elt(List *list, void *elt);
List *append_list(List *a, List *b);
List *reverse(List *list);
int length(List *list);

/* these functions always call cmp with elt as the first arg */
List *insertinorder(Cmpfunc cmp, List *list, void *elt);
void *findinorder(Cmpfunc cmp, List *list, void *elt);
List *removeinorder(Cmpfunc cmp, List *list, void *elt);
void **list_to_array(List *from, u16 *len);
List *array_to_list(u16 len, void **from);

#endif
