#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buddy.h"

#define NIL 0xFFFFFFFFu
#define MAGIC 0xBD000000u
#define USED 0x100u

/* header: [MAGIC | used | order] [requested]; free blocks keep prev/next after it */

static uint32_t get32(const Buddy *b, uint32_t off)
{
    uint32_t v;
    memcpy(&v, b->mem + off, 4);
    return v;
}

static void put32(Buddy *b, uint32_t off, uint32_t v)
{
    memcpy(b->mem + off, &v, 4);
}

static void set_header(Buddy *b, uint32_t off, int order, bool used, uint32_t req)
{
    put32(b, off, MAGIC | (used ? USED : 0) | (uint32_t)order);
    put32(b, off + 4, req);
}

static bool header_ok(const Buddy *b, uint32_t off)
{
    return (get32(b, off) & 0xFF000000u) == MAGIC;
}

static int order_of(const Buddy *b, uint32_t off)
{
    return (int)(get32(b, off) & 0xFFu);
}

static bool used_at(const Buddy *b, uint32_t off)
{
    return (get32(b, off) & USED) != 0;
}

static void push(Buddy *b, int order, uint32_t off)
{
    uint32_t head = b->free_head[order];

    set_header(b, off, order, false, 0);
    put32(b, off + 8, NIL);
    put32(b, off + 12, head);
    if (head != NIL)
        put32(b, head + 8, off);
    b->free_head[order] = off;
}

static void unlink_block(Buddy *b, int order, uint32_t off)
{
    uint32_t prev = get32(b, off + 8), next = get32(b, off + 12);

    if (prev == NIL)
        b->free_head[order] = next;
    else
        put32(b, prev + 12, next);
    if (next != NIL)
        put32(b, next + 8, prev);
}

bool buddy_init(Buddy *b, uint32_t size)
{
    int order = BUDDY_MIN_ORDER, i;

    memset(b, 0, sizeof *b);
    while (order < BUDDY_MAX_ORDER && (1u << (order + 1)) <= size)
        order++;
    if ((1u << order) > size)
        return false;
    b->size = 1u << order;
    b->max_order = order;
    b->mem = malloc(b->size);
    if (!b->mem)
        return false;
    memset(b->mem, 0, b->size);
    for (i = 0; i <= BUDDY_MAX_ORDER; i++)
        b->free_head[i] = NIL;
    push(b, order, 0);
    return true;
}

void buddy_destroy(Buddy *b)
{
    free(b->mem);
    memset(b, 0, sizeof *b);
}

uint32_t buddy_alloc(Buddy *b, uint32_t n)
{
    uint64_t need = (uint64_t)n + BUDDY_HDR;
    int order = BUDDY_MIN_ORDER, j;
    uint32_t off;

    if (n == 0) {
        b->failed++;
        return 0;
    }
    while (order <= b->max_order && ((uint64_t)1 << order) < need)
        order++;
    if (order > b->max_order) {
        b->failed++;
        return 0;
    }

    for (j = order; j <= b->max_order; j++) {
        b->searched++;
        if (b->free_head[j] != NIL)
            break;
    }
    if (j > b->max_order) {
        b->failed++;
        return 0;
    }

    off = b->free_head[j];
    unlink_block(b, j, off);
    while (j > order) {
        j--;
        push(b, j, off + (1u << j));
    }
    set_header(b, off, order, true, n);
    b->allocs++;
    return off + BUDDY_HDR;
}

bool buddy_free(Buddy *b, uint32_t addr)
{
    uint32_t off;
    int order;

    if (addr < BUDDY_HDR || addr >= b->size)
        return false;
    off = addr - BUDDY_HDR;
    if (!header_ok(b, off) || !used_at(b, off))
        return false;
    order = order_of(b, off);
    if (order < BUDDY_MIN_ORDER || order > b->max_order || off % (1u << order))
        return false;

    /* clear the used flag first so a stale header can't be freed twice */
    set_header(b, off, order, false, 0);
    while (order < b->max_order) {
        uint32_t buddy = off ^ (1u << order);
        if (!header_ok(b, buddy) || used_at(b, buddy) || order_of(b, buddy) != order)
            break;
        unlink_block(b, order, buddy);
        if (buddy < off)
            off = buddy;
        order++;
    }
    push(b, order, off);
    return true;
}

bool buddy_block_at(const Buddy *b, uint32_t offset, BuddyBlock *blk)
{
    if (offset >= b->size || !header_ok(b, offset))
        return false;
    blk->offset = offset;
    blk->order = order_of(b, offset);
    blk->used = used_at(b, offset);
    blk->requested = blk->used ? get32(b, offset + 4) : 0;
    return true;
}

bool buddy_check(const Buddy *b, char *msg, unsigned long msgsize)
{
    uint32_t off = 0, cur;
    unsigned long free_walk = 0, free_list = 0;
    int k;

    while (off < b->size) {
        int order;
        uint32_t buddy;
        if (!header_ok(b, off)) {
            snprintf(msg, msgsize, "bad header at %u", (unsigned)off);
            return false;
        }
        order = order_of(b, off);
        if (order < BUDDY_MIN_ORDER || order > b->max_order || off % (1u << order)) {
            snprintf(msg, msgsize, "bad order at %u", (unsigned)off);
            return false;
        }
        if (!used_at(b, off)) {
            free_walk++;
            buddy = off ^ (1u << order);
            if (order < b->max_order && header_ok(b, buddy) && !used_at(b, buddy)
                && order_of(b, buddy) == order) {
                snprintf(msg, msgsize, "free buddies not merged at %u", (unsigned)off);
                return false;
            }
        }
        off += 1u << order;
    }
    for (k = BUDDY_MIN_ORDER; k <= b->max_order; k++) {
        uint32_t prev = NIL;
        for (cur = b->free_head[k]; cur != NIL; cur = get32(b, cur + 12)) {
            if (!header_ok(b, cur) || used_at(b, cur) || order_of(b, cur) != k
                || get32(b, cur + 8) != prev || ++free_list > free_walk) {
                snprintf(msg, msgsize, "free list %d broken at %u", k, (unsigned)cur);
                return false;
            }
            prev = cur;
        }
    }
    if (free_list != free_walk) {
        snprintf(msg, msgsize, "free lists hold %lu blocks, heap has %lu", free_list, free_walk);
        return false;
    }
    snprintf(msg, msgsize, "buddy heap is consistent (%lu free block%s)", free_walk,
             free_walk == 1 ? "" : "s");
    return true;
}
