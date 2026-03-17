#include "pmm.h"
#include "../limine.h"

/* ── External Limine requests (defined in kernel.c) ───────────────────────── */
extern struct limine_memmap_request  memmap_request;
extern struct limine_hhdm_request    hhdm_request;

/* ── Simple segregated free-list allocator ────────────────────────────────── */

typedef struct block_header {
    size_t               size;  /* payload size (excl. header) */
    struct block_header *next;  /* next free block             */
    uint8_t              free;
} block_header_t;

#define ALIGN(x, a)  (((x) + (a) - 1) & ~((a) - 1))
#define HDR_SIZE      ALIGN(sizeof(block_header_t), 16)
#define MIN_ALLOC     16

static uint8_t *heap_start = NULL;
static uint8_t *heap_end   = NULL;
static uint8_t *bump       = NULL;           /* bump pointer for fresh pages  */
static block_header_t *free_list = NULL;

static uint64_t total_mem = 0;
static uint64_t used_mem  = 0;

/* Grab a fresh slab from the largest usable memory region */
static void heap_grow(size_t need) {
    (void)need; /* unused – we pre-map the whole region */
}

void pmm_init(uint64_t hhdm_offset) {
    struct limine_memmap_response *mm = memmap_request.response;
    if (!mm) return;

    /* Find the largest usable region */
    uint64_t best_base = 0, best_len = 0;
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->length > best_len) {
            best_base = e->base;
            best_len  = e->length;
        }
        if (e->type == LIMINE_MEMMAP_USABLE)
            total_mem += e->length;
    }

    heap_start = (uint8_t *)(best_base + hhdm_offset);
    heap_end   = heap_start + best_len;
    bump       = heap_start;
}

void *kmalloc(size_t size) {
    if (!size) return NULL;
    size = ALIGN(size, 16);

    /* Search free list first */
    block_header_t *prev = NULL, *cur = free_list;
    while (cur) {
        if (cur->size >= size) {
            /* split if enough room */
            if (cur->size >= size + HDR_SIZE + MIN_ALLOC) {
                block_header_t *split =
                    (block_header_t *)((uint8_t *)cur + HDR_SIZE + size);
                split->size = cur->size - size - HDR_SIZE;
                split->free = 1;
                split->next = cur->next;
                cur->size   = size;
                cur->next   = split;
            }
            cur->free = 0;
            if (prev) prev->next = cur->next;
            else       free_list  = cur->next;
            used_mem += cur->size + HDR_SIZE;
            return (uint8_t *)cur + HDR_SIZE;
        }
        prev = cur;
        cur  = cur->next;
    }

    /* Bump allocate a fresh block */
    size_t total = HDR_SIZE + size;
    if (bump + total > heap_end) return NULL;   /* OOM */

    block_header_t *hdr = (block_header_t *)bump;
    hdr->size = size;
    hdr->free = 0;
    hdr->next = NULL;
    bump     += total;
    used_mem += total;
    return (uint8_t *)hdr + HDR_SIZE;
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_header_t *hdr = (block_header_t *)((uint8_t *)ptr - HDR_SIZE);
    hdr->free = 1;
    used_mem -= hdr->size + HDR_SIZE;
    /* Prepend to free list */
    hdr->next = free_list;
    free_list  = hdr;
}

uint64_t pmm_total_mem(void) { return total_mem; }
uint64_t pmm_used_mem(void)  { return used_mem;  }

/* ── Heap companion functions (used via heap.h) ───────────────────────────── */

void heap_init(void) { /* pmm_init() already set up the heap */ }

void *kcalloc(size_t count, size_t sz) {
    size_t total = count * sz;
    void *p = kmalloc(total);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (size_t i = 0; i < total; i++) b[i] = 0;
    }
    return p;
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return 0; }
    block_header_t *hdr = (block_header_t *)((uint8_t *)ptr - HDR_SIZE);
    if (hdr->size >= ALIGN(new_size, 16)) return ptr;
    void *np = kmalloc(new_size);
    if (!np) return 0;
    size_t copy = hdr->size < new_size ? hdr->size : new_size;
    uint8_t *s = (uint8_t *)ptr, *d = (uint8_t *)np;
    for (size_t i = 0; i < copy; i++) d[i] = s[i];
    kfree(ptr);
    return np;
}

void heap_stats(size_t *total_out, size_t *used_out, size_t *free_out) {
    size_t pool  = (size_t)(heap_end - heap_start);
    size_t in_use = (size_t)used_mem;
    if (total_out) *total_out = pool;
    if (used_out)  *used_out  = in_use;
    if (free_out)  *free_out  = in_use <= pool ? pool - in_use : 0;
}
