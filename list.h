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
List *append_list(List *a, List *b);
List *reverse(List *list);
List *reverse_copy(List *list);
int length(List *list);
/* these functions always call cmp with elt as the first arg */
List *insertinorder(Cmpfunc cmp, List *list, void *elt);
void *findinorder(Cmpfunc cmp, List *list, void *elt);
List *removeinorder(Cmpfunc cmp, List *list, void *elt);

void list_to_array(List *from, u16 *len, void **to);
List *array_to_list(u16 len, void **from);

#endif
