#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "list.h"

int null(List *list) {
    return list == NULL;
}

void *car(List *list) {
    assert(!null(list));

    return list->car;
}

void *cdr(List *list) {
    assert(!null(list));

    return list->cdr;
}

List *cons(void *car, void *cdr) {
    List *list = GC_NEW(List);

    assert(list != NULL);

    list->car = car;
    list->cdr = cdr;

    return list;
}

void *caar(List *list) {
    return car(car(list));
}

void *cadr(List *list) {
    return car(cdr(list));
}

void *cdar(List *list) {
    return cdr(car(list));
}

void *cddr(List *list) {
    return cdr(cdr(list));
}

void setcar(List *list, void *car) {
    assert(!null(list));

    list->car = car;
}

void setcdr(List *list, void *cdr) {
    assert(!null(list));

    list->cdr = cdr;
}

List *append_elt(List *list, void *elt) {
    List *res = list;

    if (null(res))
        return cons(elt, NULL);

    while (!null(cdr(list)))
        list = cdr(list);
    setcdr(list, cons(elt, NULL));

    return res;
}

List *append_list(List *a, List *b) {
    List *list = a;

    if (null(a))
        return b;
    if (null(b))
        return a;

    while (!null(cdr(list)))
        list = cdr(list);
    setcdr(list, b);

    return a;
}

List *reverse(List *list) {
    List *prev = NULL, *next = NULL;

    while (!null(list)) {
        next = cdr(list);
        setcdr(list, prev);
        prev = list;
        list = next;
    }

    return prev;
}

int length(List *list) {
    int len;
    for (len = 0; !null(list); list = cdr(list), len++)
        ;
    return len;
}

List *insertinorder(Cmpfunc cmp, List *list, void *elt) {
    List *prev = NULL;
    List *cur = list;

    while (!null(cur) && cmp(elt, car(cur)) < 0) {
        prev = cur;
        cur = cdr(cur);
    }

    if (null(prev))
        return cons(elt, list);

    setcdr(prev, cons(elt, cur));
    return list;
}

void *findinorder(Cmpfunc cmp, List *list, void *elt) {
    int test = 1;
    while (!null(list) && (test = cmp(elt, car(list))) < 0)
        list = cdr(list);

    return test ? NULL : car(list);
}

List *removeinorder(Cmpfunc cmp, List *list, void *elt) {
    List *prev = NULL;
    List *cur = list;

    while (!null(cur) && cmp(elt, car(cur)) < 0) {
        prev = cur;
        cur = cdr(cur);
    }

    if (null(cur))
        return list;

    if (null(prev))
        return cdr(cur);

    setcdr(prev, cdr(cur));
    return list;
}

void list_to_array(List *from, u16 *len, void ***to) {
    int i;

    *len = length(from);
    if (*len == 0) {
        *to = NULL;
        return;
    }

    *to = GC_MALLOC(sizeof(void *) * *len);
    assert(*to != NULL);

    for (i = 0; i < *len; i++) {
        (*to)[i] = car(from);
        from = cdr(from);
    }

    assert(null(from));
}

List *array_to_list(u16 len, void **from) {
    List *to = NULL;
    int i;

    for (i = len - 1; i >= 0; i--)
        to = cons(from[i], to);

    return to;
}
