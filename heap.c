/**
 * A binary heap.
 * This structure is useful as a priority queue.  add() and remove() both
 * work in O(log(n)) time, and remove() always returns the smallest object
 * in the container.  If two objects compare as equal, there is no
 * guarantee about the order in which they will be removed.
 * The Heap will grow and shrink as needed (never smaller than the
 * specificied minimum size).  The changes are done by doubling and
 * halving so the amortized cost is constant.
 */

#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "util.h"
#include "heap.h"

/**
 * Create a Heap with the given minimum size that will use the given
 * compare function to determine ordering.
 */
Heap *heap_new(int minSize, Cmpfunc compare) {
    Heap *heap = GC_NEW(Heap);
    assert(heap != NULL);

    heap->array = GC_MALLOC(sizeof(void *) * minSize);
    heap->size = heap->minSize = minSize;
    assert(heap->array != NULL);

    heap->compare = compare;
    heap->count = 0;

    return heap;
}

/**
 * Change the current Heap capacity.
 */
static void heap_resize(Heap *heap, int newSize) {
    void **old = heap->array;
    heap->array = GC_MALLOC(sizeof(void *) * newSize);
    assert(heap->array != NULL);
    memcpy(heap->array, old, sizeof(void *) * (heap->count + 1));
    heap->size = newSize;
}

/**
 * Add an object to the Heap.
 */
void heap_add(Heap *heap, void *elt) {
    int i;

    assert(heap != NULL);

    if (heap->count == heap->size - 1)
        heap_resize(heap, heap->size*2);

    i = ++heap->count;
    heap->array[i] = elt;

    /* let the new element bubble up as necessary */
    while (i > 1 && heap->compare(heap->array[i], heap->array[i/2]) < 0) {
        /* swap element i with element i/2 */
        void *swap = heap->array[i];
        heap->array[i] = heap->array[i/2];
        heap->array[i/=2] = swap;
    }
}

/**
 * Remove and return the smallest element in the Heap.
 */
void *heap_remove(Heap *heap) {
    int i, left;
    void *min;

    assert(heap != NULL);

    if (heap->count == 0)
        return NULL;

    min = heap->array[1];
    heap->array[1] = heap->array[heap->count--];
    if (heap->size > heap->minSize && heap->count < heap->size/4)
        heap_resize(heap, heap->size/2);

    /* reheap from the root going down */
    i = 1;
    for (left = i*2; left <= heap->count; left = i*2) {
        /* is root <= right node or no right node exists? */
        if (left == heap->count ||
                heap->compare(heap->array[i], heap->array[left+1]) <= 0)
        {
            if (heap->compare(heap->array[i], heap->array[left]) <= 0) {
                break;
            } else {
                /* swap root & left */
                void *swap = heap->array[i];
                heap->array[i] = heap->array[left];
                heap->array[i = left] = swap;
            }
        } else {
            /* right < root, is it < left too? */
            if (heap->compare(heap->array[left], heap->array[left+1]) < 0) {
                /* swap root & left */
                void *swap = heap->array[i];
                heap->array[i] = heap->array[left];
                heap->array[i = left] = swap;
            } else {
                /* swap root & right */
                void *swap = heap->array[i];
                heap->array[i] = heap->array[left+1];
                heap->array[i = left+1] = swap;
            }
        }
    }

    return min;
}

int heap_isempty(Heap *heap) {
    return heap->count == 0;
}
