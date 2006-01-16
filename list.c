#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include "types.h"
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

    if (res == NULL)
        return cons(elt, NULL);

    while (!null(cdr(list)))
        list = cdr(list);
    setcdr(list, cons(elt, NULL));

    return res;
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
