#ifndef _LIST_H_
#define _LIST_H_

#include "types.h"

/*
 * Lisp-like lists
 */

struct list {
    void *car;
    void *cdr;
};

int null(List *cons);
void *car(List *cons);
void *cdr(List *cons);
List *cons(void *car, void *cdr);
void *caar(List *cons);
void *cadr(List *cons);
void *cdar(List *cons);
void *cddr(List *cons);
void setcar(List *cons, void *car);
void setcdr(List *cons, void *cdr);
List *append_elt(List *list, void *elt);
List *reverse(List *list);
int length(List *list);
List *insertinorder(int (*cmp)(const void *a, const void *b), List *list, void *elt);

#endif
