#ifndef _HEAP_H_
#define _HEAP_H_

#include "types.h"

struct heap {
    void **array;
    int minSize;
    int size;
    int count;
    int (*compare)(void *, void *);
};

Heap *heap_new(int minSize, int (*compare)(void *, void *));
void heap_add(Heap *heap, void *elt);
void *heap_remove(Heap *heap);

#endif
