#ifndef _DLIST_H_
#define _DLIST_H_

/* Doubly-linked lists */

#include "types.h"

struct dlist_cell {
    struct dlist_cell *prev;
    struct dlist_cell *next;
    void *elt;
};

struct dlist {
    struct dlist_cell *first;
};

Dlist *dlist_new(void);
struct dlist_cell *dlist_push(Dlist *list, void *elt);
void *dlist_pop(Dlist *list);
struct dlist_cell *dlist_unshift(Dlist *list, void *elt);
void *dlist_shift(Dlist *list);
void *dlist_remove(struct dlist_cell *cell);

#endif
