#ifndef BUDDY_H
#define BUDDY_H

#include <stdint.h>
#include <stdbool.h>

#define BUDDY_MIN_ORDER 5   /* 32 byte blocks */
#define BUDDY_MAX_ORDER 30
#define BUDDY_HDR 8

typedef struct {
    unsigned char *mem;
    uint32_t size;
    int max_order;
    uint32_t free_head[BUDDY_MAX_ORDER + 1];
    unsigned long allocs, failed, searched;
} Buddy;

typedef struct {
    uint32_t offset;
    int order;
    bool used;
    uint32_t requested;
} BuddyBlock;

bool buddy_init(Buddy *b, uint32_t size);
void buddy_destroy(Buddy *b);
uint32_t buddy_alloc(Buddy *b, uint32_t n);
bool buddy_free(Buddy *b, uint32_t addr);
bool buddy_block_at(const Buddy *b, uint32_t offset, BuddyBlock *blk);
bool buddy_check(const Buddy *b, char *msg, unsigned long msgsize);

#endif
