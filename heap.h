#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Block layout inside the arena (all offsets are multiples of 8):
 *
 *   header  [size|used] [requested bytes]        8 bytes
 *   payload ...                                  (free blocks keep prev/next here)
 *   guard   0xFD bytes after the requested size  (used blocks)
 *   footer  [size|used] [TAG_MAGIC]              8 bytes
 */
#define HEAP_ALIGN 8
#define HEAP_TAG 8
#define HEAP_GUARD 8
#define HEAP_MIN_BLOCK 24
#define HEAP_NIL 0xFFFFFFFFu

typedef enum { FIRST_FIT, NEXT_FIT, BEST_FIT, WORST_FIT, NUM_STRATEGIES } Strategy;

typedef enum {
    HEAP_OK,
    HEAP_BAD_POINTER,
    HEAP_DOUBLE_FREE,
    HEAP_OVERFLOW_DETECTED
} HeapStatus;

typedef struct {
    unsigned char *mem;
    uint32_t size;
    uint32_t free_head;
    uint32_t rover;
    unsigned long allocs, failed, searched;
} Heap;

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint32_t requested;
    bool used;
} BlockInfo;

typedef struct {
    uint32_t used_blocks, free_blocks;
    uint32_t used_bytes, free_bytes, requested_bytes, largest_free;
    double external_frag;
} HeapStats;

typedef void (*MoveFn)(uint32_t old_addr, uint32_t new_addr, void *ctx);

bool heap_init(Heap *h, uint32_t size);
void heap_destroy(Heap *h);
const char *strategy_name(Strategy s);

uint32_t heap_block_need(uint32_t n);
uint32_t heap_alloc(Heap *h, uint32_t n, Strategy s);
uint32_t heap_calloc(Heap *h, uint32_t count, uint32_t n, Strategy s);
uint32_t heap_realloc(Heap *h, uint32_t addr, uint32_t n, Strategy s);
HeapStatus heap_free(Heap *h, uint32_t addr);

bool heap_block_at(const Heap *h, uint32_t offset, BlockInfo *info);
bool heap_block_of(const Heap *h, uint32_t addr, BlockInfo *info);
bool heap_guard_ok(const Heap *h, uint32_t offset);
unsigned char *heap_ptr(Heap *h, uint32_t addr);
void heap_stats(const Heap *h, HeapStats *st);
bool heap_check(const Heap *h, char *msg, size_t msgsize);
void heap_compact(Heap *h, MoveFn fn, void *ctx);

#endif
