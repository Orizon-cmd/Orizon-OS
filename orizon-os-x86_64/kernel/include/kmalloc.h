/*
 * Orizon OS x86_64 - Simple Kernel Memory Allocator
 */

#ifndef _KMALLOC_H
#define _KMALLOC_H

#include "types.h"

/* Allocation flags (simplified) */
#define GFP_KERNEL 0
#define GFP_ATOMIC 1

typedef struct {
  size_t total;
  size_t used;
  size_t free;
  size_t largest_free;
  size_t blocks;
  size_t free_blocks;
  size_t used_blocks;
} kmalloc_stats_t;

/* Initialize the heap */
void kmalloc_init(void);

/* Allocate memory */
void *kmalloc(size_t size);
void *kzalloc(size_t size); /* Zero-initialized */
void *krealloc(void *ptr, size_t new_size);

/* Free memory */
void kfree(void *ptr);

/* Get heap statistics */
size_t kmalloc_get_total(void);
size_t kmalloc_get_used(void);
size_t kmalloc_get_free(void);
size_t kmalloc_get_largest_free(void);
void kmalloc_get_stats(kmalloc_stats_t *stats);

#endif /* _KMALLOC_H */
