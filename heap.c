#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "heap.h"

#define USED_BIT 1u
#define TAG_MAGIC 0xB10CB10Cu
#define GUARD_BYTE 0xFD

static uint32_t get32(const Heap *h, uint32_t off)
{
    uint32_t v;
    memcpy(&v, h->mem + off, 4);
    return v;
}

static void put32(Heap *h, uint32_t off, uint32_t v)
{
    memcpy(h->mem + off, &v, 4);
}

static uint32_t block_size(const Heap *h, uint32_t off)
{
    return get32(h, off) & ~7u;
}

static bool is_used(const Heap *h, uint32_t off)
{
    return (get32(h, off) & USED_BIT) != 0;
}

static uint32_t requested(const Heap *h, uint32_t off)
{
    return get32(h, off + 4);
}

static void set_block(Heap *h, uint32_t off, uint32_t size, bool used, uint32_t req)
{
    uint32_t word = size | (used ? USED_BIT : 0);
    put32(h, off, word);
    put32(h, off + 4, used ? req : 0);
    put32(h, off + size - HEAP_TAG, word);
    put32(h, off + size - HEAP_TAG + 4, TAG_MAGIC);
}

static uint32_t get_prev(const Heap *h, uint32_t off) { return get32(h, off + 8); }
static uint32_t get_next(const Heap *h, uint32_t off) { return get32(h, off + 12); }
static void set_prev(Heap *h, uint32_t off, uint32_t v) { put32(h, off + 8, v); }
static void set_next(Heap *h, uint32_t off, uint32_t v) { put32(h, off + 12, v); }

uint32_t heap_block_need(uint32_t n)
{
    uint64_t need = (uint64_t)n + 2 * HEAP_TAG + HEAP_GUARD;
    need = (need + HEAP_ALIGN - 1) & ~(uint64_t)(HEAP_ALIGN - 1);
    if (need < HEAP_MIN_BLOCK)
        need = HEAP_MIN_BLOCK;
    return need > 0xFFFFFFF0u ? 0xFFFFFFF0u : (uint32_t)need;
}

const char *strategy_name(Strategy s)
{
    static const char *names[] = { "first fit", "next fit", "best fit", "worst fit" };
    return s < NUM_STRATEGIES ? names[s] : "?";
}


/* ---------- free list, kept in address order ---------- */

static void list_insert(Heap *h, uint32_t off)
{
    uint32_t prev = HEAP_NIL, cur = h->free_head;

    while (cur != HEAP_NIL && cur < off) {
        prev = cur;
        cur = get_next(h, cur);
    }
    set_prev(h, off, prev);
    set_next(h, off, cur);
    if (prev == HEAP_NIL)
        h->free_head = off;
    else
        set_next(h, prev, off);
    if (cur != HEAP_NIL)
        set_prev(h, cur, off);
}

static void list_remove(Heap *h, uint32_t off)
{
    uint32_t prev = get_prev(h, off), next = get_next(h, off);

    if (prev == HEAP_NIL)
        h->free_head = next;
    else
        set_next(h, prev, next);
    if (next != HEAP_NIL)
        set_prev(h, next, prev);
    if (h->rover == off)
        h->rover = next != HEAP_NIL ? next : h->free_head;
}

/* new_off takes old_off's place in the list */
static void list_replace(Heap *h, uint32_t old_off, uint32_t new_off)
{
    uint32_t prev = get_prev(h, old_off), next = get_next(h, old_off);

    set_prev(h, new_off, prev);
    set_next(h, new_off, next);
    if (prev == HEAP_NIL)
        h->free_head = new_off;
    else
        set_next(h, prev, new_off);
    if (next != HEAP_NIL)
        set_prev(h, next, new_off);
    if (h->rover == old_off)
        h->rover = new_off;
}


bool heap_init(Heap *h, uint32_t size)
{
    size &= ~(uint32_t)(HEAP_ALIGN - 1);
    memset(h, 0, sizeof *h);
    if (size < 2 * HEAP_MIN_BLOCK)
        return false;
    h->mem = malloc(size);
    if (!h->mem)
        return false;
    memset(h->mem, 0, size);
    h->size = size;
    set_block(h, 0, size, false, 0);
    h->free_head = HEAP_NIL;
    list_insert(h, 0);
    h->rover = 0;
    return true;
}

void heap_destroy(Heap *h)
{
    free(h->mem);
    memset(h, 0, sizeof *h);
}

static uint32_t find_block(Heap *h, uint32_t need, Strategy s)
{
    uint32_t cur, pick = HEAP_NIL;

    switch (s) {
    case FIRST_FIT:
        for (cur = h->free_head; cur != HEAP_NIL; cur = get_next(h, cur)) {
            h->searched++;
            if (block_size(h, cur) >= need)
                return cur;
        }
        break;

    case NEXT_FIT: {
        uint32_t start;
        if (h->free_head == HEAP_NIL)
            break;
        start = cur = h->rover != HEAP_NIL ? h->rover : h->free_head;
        do {
            h->searched++;
            if (block_size(h, cur) >= need)
                return cur;
            cur = get_next(h, cur);
            if (cur == HEAP_NIL)
                cur = h->free_head;
        } while (cur != start);
        break;
    }

    case BEST_FIT:
        for (cur = h->free_head; cur != HEAP_NIL; cur = get_next(h, cur)) {
            uint32_t sz = block_size(h, cur);
            h->searched++;
            if (sz >= need && (pick == HEAP_NIL || sz < block_size(h, pick))) {
                pick = cur;
                if (sz == need)
                    break;
            }
        }
        break;

    case WORST_FIT:
        for (cur = h->free_head; cur != HEAP_NIL; cur = get_next(h, cur)) {
            uint32_t sz = block_size(h, cur);
            h->searched++;
            if (sz >= need && (pick == HEAP_NIL || sz > block_size(h, pick)))
                pick = cur;
        }
        break;

    default:
        break;
    }
    return pick;
}

static void fill_guard(Heap *h, uint32_t off)
{
    uint32_t start = off + HEAP_TAG + requested(h, off);
    uint32_t end = off + block_size(h, off) - HEAP_TAG;
    memset(h->mem + start, GUARD_BYTE, end - start);
}

bool heap_guard_ok(const Heap *h, uint32_t off)
{
    uint32_t i = off + HEAP_TAG + requested(h, off);
    uint32_t end = off + block_size(h, off) - HEAP_TAG;

    for (; i < end; i++)
        if (h->mem[i] != GUARD_BYTE)
            return false;
    return true;
}

uint32_t heap_alloc(Heap *h, uint32_t n, Strategy s)
{
    uint32_t need, off, size;

    if (n == 0 || n > h->size) {
        h->failed++;
        return 0;
    }
    need = heap_block_need(n);
    off = find_block(h, need, s);
    if (off == HEAP_NIL) {
        h->failed++;
        return 0;
    }

    size = block_size(h, off);
    if (size - need >= HEAP_MIN_BLOCK) {
        uint32_t rest = off + need;
        set_block(h, rest, size - need, false, 0);
        list_replace(h, off, rest);
        size = need;
        if (s == NEXT_FIT)
            h->rover = rest;
    } else {
        uint32_t after = get_next(h, off);
        list_remove(h, off);
        if (s == NEXT_FIT)
            h->rover = after != HEAP_NIL ? after : h->free_head;
    }

    set_block(h, off, size, true, n);
    fill_guard(h, off);
    h->allocs++;
    return off + HEAP_TAG;
}

uint32_t heap_calloc(Heap *h, uint32_t count, uint32_t n, Strategy s)
{
    uint64_t total = (uint64_t)count * n;
    uint32_t addr;

    if (total == 0 || total > h->size) {
        h->failed++;
        return 0;
    }
    addr = heap_alloc(h, (uint32_t)total, s);
    if (addr)
        memset(h->mem + addr, 0, (size_t)total);
    return addr;
}

static bool valid_block(const Heap *h, uint32_t off)
{
    uint32_t size, foot;

    if (off % HEAP_ALIGN || off + HEAP_MIN_BLOCK > h->size)
        return false;
    size = block_size(h, off);
    if (size < HEAP_MIN_BLOCK || size % HEAP_ALIGN || size > h->size - off)
        return false;
    foot = off + size - HEAP_TAG;
    return get32(h, foot) == get32(h, off) && get32(h, foot + 4) == TAG_MAGIC;
}

/* marks off as free, merges with free neighbours and puts the result on the list */
static void release(Heap *h, uint32_t off)
{
    uint32_t size = block_size(h, off);
    uint32_t next = off + size;
    bool prev_free = false;

    /* retag as free first, so stale tags left inside a merged block never look allocated */
    set_block(h, off, size, false, 0);
    if (next < h->size && !is_used(h, next)) {
        list_remove(h, next);
        size += block_size(h, next);
    }
    if (off > 0) {
        uint32_t psize = get32(h, off - HEAP_TAG) & ~7u;
        uint32_t prev = off - psize;
        if (!(get32(h, off - HEAP_TAG) & USED_BIT)) {
            uint32_t pn = get_next(h, prev), pp = get_prev(h, prev);
            size += psize;
            off = prev;
            prev_free = true;
            set_block(h, off, size, false, 0);
            set_prev(h, off, pp);
            set_next(h, off, pn);
        }
    }
    if (!prev_free) {
        set_block(h, off, size, false, 0);
        list_insert(h, off);
    }
    if (h->rover == HEAP_NIL)
        h->rover = h->free_head;
}

HeapStatus heap_free(Heap *h, uint32_t addr)
{
    uint32_t off;
    HeapStatus st = HEAP_OK;

    if (addr < HEAP_TAG)
        return HEAP_BAD_POINTER;
    off = addr - HEAP_TAG;
    if (!valid_block(h, off))
        return HEAP_BAD_POINTER;
    if (!is_used(h, off))
        return HEAP_DOUBLE_FREE;
    if (!heap_guard_ok(h, off))
        st = HEAP_OVERFLOW_DETECTED;
    release(h, off);
    return st;
}

uint32_t heap_realloc(Heap *h, uint32_t addr, uint32_t n, Strategy s)
{
    uint32_t off, size, need, next, fresh, keep;

    if (addr < HEAP_TAG || !valid_block(h, addr - HEAP_TAG) || !is_used(h, addr - HEAP_TAG))
        return 0;
    if (n == 0 || n > h->size)
        return 0;
    off = addr - HEAP_TAG;
    size = block_size(h, off);
    need = heap_block_need(n);

    if (need <= size) {
        keep = requested(h, off);
        if (n > keep)
            memset(h->mem + addr + keep, 0, n - keep);
        if (size - need >= HEAP_MIN_BLOCK) {
            set_block(h, off + need, size - need, true, 0);
            set_block(h, off, need, true, n);
            release(h, off + need);
        } else {
            set_block(h, off, size, true, n);
        }
        fill_guard(h, off);
        return addr;
    }

    next = off + size;
    if (next < h->size && !is_used(h, next) && size + block_size(h, next) >= need) {
        uint32_t total = size + block_size(h, next);
        list_remove(h, next);
        if (total - need >= HEAP_MIN_BLOCK) {
            set_block(h, off + need, total - need, false, 0);
            list_insert(h, off + need);
            total = need;
        }
        if (h->rover == HEAP_NIL)
            h->rover = h->free_head;
        keep = requested(h, off);
        set_block(h, off, total, true, n);
        memset(h->mem + addr + keep, 0, n - keep);
        fill_guard(h, off);
        return addr;
    }

    fresh = heap_alloc(h, n, s);
    if (!fresh)
        return 0;
    keep = requested(h, off);
    memcpy(h->mem + fresh, h->mem + addr, keep < n ? keep : n);
    if (n > keep)
        memset(h->mem + fresh + keep, 0, n - keep);
    heap_free(h, addr);
    return fresh;
}

unsigned char *heap_ptr(Heap *h, uint32_t addr)
{
    return h->mem + addr;
}

bool heap_block_at(const Heap *h, uint32_t offset, BlockInfo *info)
{
    if (offset >= h->size)
        return false;
    info->offset = offset;
    info->size = block_size(h, offset);
    info->used = is_used(h, offset);
    info->requested = info->used ? requested(h, offset) : 0;
    return info->size != 0;
}

bool heap_block_of(const Heap *h, uint32_t addr, BlockInfo *info)
{
    if (addr < HEAP_TAG || !valid_block(h, addr - HEAP_TAG))
        return false;
    return heap_block_at(h, addr - HEAP_TAG, info);
}

void heap_stats(const Heap *h, HeapStats *st)
{
    BlockInfo b;
    uint32_t off = 0;

    memset(st, 0, sizeof *st);
    while (heap_block_at(h, off, &b)) {
        if (b.used) {
            st->used_blocks++;
            st->used_bytes += b.size;
            st->requested_bytes += b.requested;
        } else {
            st->free_blocks++;
            st->free_bytes += b.size;
            if (b.size > st->largest_free)
                st->largest_free = b.size;
        }
        off += b.size;
    }
    st->external_frag = st->free_bytes ? 1.0 - (double)st->largest_free / st->free_bytes : 0.0;
}

bool heap_check(const Heap *h, char *msg, size_t msgsize)
{
    uint32_t off = 0, free_walk = 0, free_list = 0, cur, prev = HEAP_NIL;
    bool last_free = false, rover_ok = h->rover == HEAP_NIL;

    while (off < h->size) {
        if (!valid_block(h, off)) {
            snprintf(msg, msgsize, "corrupt block tags at offset %u", (unsigned)off);
            return false;
        }
        if (is_used(h, off)) {
            if (requested(h, off) + 2 * HEAP_TAG + HEAP_GUARD > block_size(h, off)) {
                snprintf(msg, msgsize, "bad requested size in block %u", (unsigned)off);
                return false;
            }
            if (!heap_guard_ok(h, off)) {
                snprintf(msg, msgsize, "guard bytes overwritten in block at %u (buffer overflow)",
                         (unsigned)(off + HEAP_TAG));
                return false;
            }
            last_free = false;
        } else {
            if (last_free) {
                snprintf(msg, msgsize, "two free blocks next to each other at %u", (unsigned)off);
                return false;
            }
            last_free = true;
            free_walk++;
        }
        off += block_size(h, off);
    }
    if (off != h->size) {
        snprintf(msg, msgsize, "blocks do not add up to the heap size");
        return false;
    }

    for (cur = h->free_head; cur != HEAP_NIL; cur = get_next(h, cur)) {
        if (!valid_block(h, cur) || is_used(h, cur) || get_prev(h, cur) != prev
            || (prev != HEAP_NIL && cur <= prev) || ++free_list > free_walk) {
            snprintf(msg, msgsize, "free list is broken near offset %u", (unsigned)cur);
            return false;
        }
        if (cur == h->rover)
            rover_ok = true;
        prev = cur;
    }
    if (free_list != free_walk) {
        snprintf(msg, msgsize, "free list has %u blocks but heap has %u",
                 (unsigned)free_list, (unsigned)free_walk);
        return false;
    }
    if (!rover_ok) {
        snprintf(msg, msgsize, "next-fit rover does not point at a free block");
        return false;
    }
    snprintf(msg, msgsize, "heap is consistent (%u free block%s)", (unsigned)free_walk,
             free_walk == 1 ? "" : "s");
    return true;
}

void heap_compact(Heap *h, MoveFn fn, void *ctx)
{
    uint32_t off = 0, dest = 0;

    while (off < h->size) {
        uint32_t size = block_size(h, off);
        if (is_used(h, off)) {
            if (off != dest) {
                memmove(h->mem + dest, h->mem + off, size);
                if (fn)
                    fn(off + HEAP_TAG, dest + HEAP_TAG, ctx);
            }
            dest += size;
        }
        off += size;
    }
    h->free_head = HEAP_NIL;
    if (dest < h->size) {
        set_block(h, dest, h->size - dest, false, 0);
        list_insert(h, dest);
    }
    h->rover = h->free_head;
}
