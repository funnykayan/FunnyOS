#pragma once
#include <stdint.h>
#include <stddef.h>

void  pmm_init(uint64_t hhdm_offset);
void *kmalloc(size_t size);
void  kfree(void *ptr);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t new_size);
void  heap_init(void);
void  heap_stats(size_t *total_out, size_t *used_out, size_t *free_out);

/* Expose stats */
uint64_t pmm_total_mem(void);
uint64_t pmm_used_mem(void);
