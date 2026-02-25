#ifndef HEAP_H
#define HEAP_H

#include "common.h"
#include <stdlib.h>

typedef struct heap *Heap;

Heap HeapInit(u64 capacity, i32 (*cmp)(const void*, const void*));
void HeapDestroy(Heap heap);
void HeapPush(Heap heap, void *element);
void* HeapPop(Heap heap);
i32 HeapIsEmpty(Heap heap);
u64 HeapSize(Heap heap);

#endif // !HEAP_H
