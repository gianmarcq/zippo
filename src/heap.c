#include "heap.h"
#include "common.h"
#include <stdlib.h>

struct heap {
    void **data;
    u64 size, capacity;
    i32 (*cmp)(const void*, const void*);
};

#define PARENT(i) ((i-1)/2)
#define LEFT(i)   (2*i+1)
#define RIGHT(i)  (2*i+2)

Heap HeapInit(u64 capacity, i32 (*cmp)(const void*, const void*)) {
    Heap heap = malloc(sizeof(*heap));
    heap->size = 0;
    heap->data = malloc(sizeof(*heap->data) * capacity);
    heap->capacity = capacity;
    heap->cmp = cmp;
    return heap;
}

void HeapDestroy(Heap heap) {
    free(heap->data);
    free(heap);
}

void HeapPush(Heap heap, void *element) {
    /* The array is dynamically reallocated based on user needs */
    if (heap->size == heap->capacity) {
        heap->capacity *= 2;
        heap->data = realloc(heap->data, sizeof(*heap->data) * heap->capacity);
        if (heap->data == NULL) SYS_ERROR("realloc");
    }
    /* When inserting an element the Fix-Up logic allows to
     * mantain the functional property of the Heap */
    u64 i = heap->size;
    while (i > 0 && heap->cmp(element, heap->data[PARENT(i)]) < 0) {
        heap->data[i] = heap->data[PARENT(i)];
        i = PARENT(i);
    }
    heap->data[i] = element;
    heap->size++;
}

void* HeapPop(Heap heap) {
    if (heap->size == 0) return NULL;
    void *min = heap->data[0];
    heap->size--;

    /* submin is the new root after deletion */
    void *submin = heap->data[heap->size];
    u64 i = 0, child;

    /* Implementation of the Fix-Down logic */
    while (LEFT(i) < heap->size) {
        child = LEFT(i);
        if (RIGHT(i) < heap->size && heap->cmp(heap->data[RIGHT(i)], heap->data[child]) < 0) {
            child = RIGHT(i);
        }

        if (heap->cmp(submin, heap->data[child]) <= 0) break;
        heap->data[i] = heap->data[child];
        i = child;
    }

    heap->data[i] = submin;

    return min;
}

i32 HeapIsEmpty(Heap heap) {
    return heap->size == 0;
}

u64 HeapSize(Heap heap) {
    return heap->size;
}
