#ifndef _HEAP_H_
#define _HEAP_H_

#include "types.h"
#include "9p.h"

struct heap {
    void **array;
    int minSize;
    int size;
    int count;
    Cmpfunc compare;
};

Heap *heap_new(int minSize, Cmpfunc compare);
void heap_add(Heap *heap, void *elt);
void *heap_remove(Heap *heap);
int heap_isempty(Heap *heap);

#endif
