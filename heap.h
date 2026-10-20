/* heap.h - simple kernel heap (first-fit allocator with coalescing) */
#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

void     heap_init(void);
void    *kmalloc(size_t size);
void     kfree(void *ptr);
uint32_t heap_total(void);   /* bytes managed by the heap */
uint32_t heap_used(void);    /* bytes currently allocated (without headers) */

#endif /* HEAP_H */