/* heap.c - simple kernel heap */
#include "heap.h"

#define HEAP_SIZE (64 * 1024 * 1024)      /* 64 MB, lives in .bss (QEMU runs with -m 256M) */

typedef struct block {
    uint32_t size;                        /* payload size in bytes */
    uint32_t free;                        /* 1 = free, 0 = in use */
    struct block *next;
    uint32_t pad;                         /* keeps the header at 16 bytes */
} block_t;

static uint8_t heap_area[HEAP_SIZE] __attribute__((aligned(16)));
static block_t *head;

void heap_init(void)
{
    head = (block_t *)heap_area;
    head->size = HEAP_SIZE - sizeof(block_t);
    head->free = 1;
    head->next = 0;
}

void *kmalloc(size_t size)
{
    if (size == 0) {
        size = 16;
    }
    size = (size + 15) & ~(size_t)15;     /* keep 16-byte alignment */

    for (block_t *b = head; b; b = b->next) {
        if (!b->free || b->size < size) {
            continue;
        }
        /* Split the block if the remainder is big enough to be useful */
        if (b->size >= size + sizeof(block_t) + 16) {
            block_t *rest = (block_t *)((uint8_t *)(b + 1) + size);
            rest->size = b->size - size - sizeof(block_t);
            rest->free = 1;
            rest->next = b->next;
            b->size = (uint32_t)size;
            b->next = rest;
        }
        b->free = 0;
        return b + 1;
    }
    return 0;                             /* out of memory */
}

void kfree(void *ptr)
{
    if (!ptr) {
        return;
    }
    block_t *b = (block_t *)ptr - 1;
    b->free = 1;

    /* Merge neighbouring free blocks */
    for (block_t *c = head; c && c->next; ) {
        if (c->free && c->next->free) {
            c->size += (uint32_t)sizeof(block_t) + c->next->size;
            c->next = c->next->next;
        } else {
            c = c->next;
        }
    }
}

uint32_t heap_total(void)
{
    return HEAP_SIZE;
}

uint32_t heap_used(void)
{
    uint32_t used = 0;
    for (block_t *b = head; b; b = b->next) {
        if (!b->free) {
            used += b->size;
        }
    }
    return used;
}