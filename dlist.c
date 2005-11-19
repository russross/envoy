/* Doubly-linked lists */

#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include "dlist.h"

static struct dlist_cell *dlist_cons(void *elt) {
    struct dlist_cell *cell = GC_NEW(struct dlist_cell);
    cell->prev = cell->next = cell;
    cell->elt = elt;
    return cell;
}

struct dlist *dlist_new(void) {
    struct dlist *list = GC_NEW(struct dlist);
    assert(list != NULL);
    list->first = NULL;
    return list;
}

struct dlist_cell *dlist_push(struct dlist *list, void *elt) {
    struct dlist_cell *last;
    
    assert(list != NULL);

    last = dlist_cons(elt);

    if (list->first == NULL) {
        list->first = last;
    } else {
        last->prev = list->first->prev;
        last->prev->next = last;
        last->next = list->first;
        list->first->prev = last;
    }

    return last;
}

void *dlist_pop(struct dlist *list) {
    struct dlist_cell *last;

    assert(list != NULL);

    if (list->first == NULL)
        return NULL;

    last = list->first->prev;

    if (list->first == last) {
        list->first = NULL;
    } else {
        last->next->prev = last->prev;
        last->prev->next = last->next;
    }
    return last->elt;
}

struct dlist_cell *dlist_unshift(struct dlist *list, void *elt) {
    struct dlist_cell *res = dlist_push(list, elt);
    list->first = list->first->prev;
    return res;
}

void *dlist_shift(struct dlist *list) {
    if (list->first != NULL)
        list->first = list->first->next;
    return dlist_pop(list);
}

void *dlist_remove(struct dlist_cell *cell) {
    assert(cell != NULL);
    cell->prev->next = cell->next;
    cell->next->prev = cell->prev;
    cell->next = cell->prev = NULL;
    return cell->elt;
}
