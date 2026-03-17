/* ============================================================
 *  FunnyOS  –  Kernel Heap  (simple explicit free-list)
 *
 *  Layout of a block:
 *    [ block_hdr_t | <user data, padded to MIN_ALIGN> ]
 *
 *  Free blocks are coalesced on kfree(). The pool lives in .bss
 *  so it adds ~HEAP_SIZE to the BSS section (no file size cost).
 * ============================================================ */

#include "heap.h"
#include <stdint.h>
#include <stddef.h>

#define HEAP_SIZE  (768 * 1024)    /* 768 KiB static pool in .bss          */
#define MIN_ALIGN  16              /* all alloc sizes rounded to this       */
#define MAGIC_FREE 0xF3EEB10Cu    /* canary for free blocks                */
#define MAGIC_USED 0xA110CA7Eu    /* canary for live blocks                */

typedef struct block_hdr {
    uint32_t         magic;
    size_t           size;         /* usable bytes after header             */
    struct block_hdr *next;        /* next block in pool (used and free)    */
    struct block_hdr *prev;
    uint8_t          free;
} block_hdr_t;

static uint8_t heap_pool[HEAP_SIZE] __attribute__((aligned(16)));
static block_hdr_t *heap_root = 0;
static int heap_ready = 0;

/* ── Internal helpers ─────────────────────────────────────────────────────── */

static inline size_t align_up(size_t n) {
    return (n + MIN_ALIGN - 1) & ~(size_t)(MIN_ALIGN - 1);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void heap_init(void) {
    heap_root = (block_hdr_t *)heap_pool;
    heap_root->magic = MAGIC_FREE;
    heap_root->size  = HEAP_SIZE - sizeof(block_hdr_t);
    heap_root->next  = 0;
    heap_root->prev  = 0;
    heap_root->free  = 1;
    heap_ready = 1;
}

void *kmalloc(size_t size) {
    if (!heap_ready || size == 0) return 0;
    size = align_up(size);

    block_hdr_t *b = heap_root;
    while (b) {
        if (b->free && b->size >= size) {
            /* Split if the leftover is large enough to be useful */
            if (b->size > size + sizeof(block_hdr_t) + MIN_ALIGN) {
                block_hdr_t *nb = (block_hdr_t *)((uint8_t *)b + sizeof(block_hdr_t) + size);
                nb->magic = MAGIC_FREE;
                nb->size  = b->size - size - sizeof(block_hdr_t);
                nb->next  = b->next;
                nb->prev  = b;
                nb->free  = 1;
                if (b->next) b->next->prev = nb;
                b->next  = nb;
                b->size  = size;
            }
            b->free  = 0;
            b->magic = MAGIC_USED;
            return (void *)((uint8_t *)b + sizeof(block_hdr_t));
        }
        b = b->next;
    }
    return 0;   /* OOM */
}

void *kcalloc(size_t count, size_t sz) {
    size_t total = count * sz;
    void *p = kmalloc(total);
    if (p) {
        uint8_t *bp = (uint8_t *)p;
        for (size_t i = 0; i < total; i++) bp[i] = 0;
    }
    return p;
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return 0; }

    block_hdr_t *b = (block_hdr_t *)((uint8_t *)ptr - sizeof(block_hdr_t));
    if (b->size >= align_up(new_size)) return ptr;   /* already big enough */

    void *np = kmalloc(new_size);
    if (!np) return 0;

    size_t copy_sz = b->size < new_size ? b->size : new_size;
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)np;
    for (size_t i = 0; i < copy_sz; i++) dst[i] = src[i];
    kfree(ptr);
    return np;
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_hdr_t *b = (block_hdr_t *)((uint8_t *)ptr - sizeof(block_hdr_t));
    if (b->magic != MAGIC_USED) return;   /* double-free or bad ptr guard */
    b->free  = 1;
    b->magic = MAGIC_FREE;

    /* Coalesce with next free block */
    while (b->next && b->next->free) {
        b->size += sizeof(block_hdr_t) + b->next->size;
        b->next  = b->next->next;
        if (b->next) b->next->prev = b;
    }

    /* Coalesce with previous free block */
    if (b->prev && b->prev->free) {
        block_hdr_t *pb = b->prev;
        pb->size += sizeof(block_hdr_t) + b->size;
        pb->next  = b->next;
        if (b->next) b->next->prev = pb;
    }
}

void heap_stats(size_t *total_out, size_t *used_out, size_t *free_out) {
    size_t used = 0, free = 0;
    block_hdr_t *b = heap_root;
    while (b) {
        if (b->free) free += b->size;
        else         used += b->size;
        b = b->next;
    }
    if (total_out) *total_out = HEAP_SIZE;
    if (used_out)  *used_out  = used;
    if (free_out)  *free_out  = free;
}
