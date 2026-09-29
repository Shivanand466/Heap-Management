#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"
#include "heap.h"
#include "buddy.h"

typedef struct {
    int alloc;      /* 1 = allocate, 0 = free */
    unsigned id;
    uint32_t size;
} Op;

typedef struct {
    const char *name;
    unsigned long attempts, failed;
    double search_per_alloc;
    double avg_frag, avg_holes, internal_waste;
} Result;

static uint32_t rng;

static uint32_t next_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* mostly small objects, some medium, a few large ones */
static uint32_t random_size(void)
{
    uint32_t r = next_rand() % 100;
    if (r < 70)
        return 8 + next_rand() % 121;
    if (r < 95)
        return 128 + next_rand() % 897;
    return 1024 + next_rand() % 3073;
}

/* keeps the live data around 60% of the heap, so failures come from fragmentation */
static unsigned make_trace(Op *ops, unsigned n, uint32_t heap_size)
{
    unsigned *live = malloc(n * sizeof *live);
    uint32_t *sizes = malloc(n * sizeof *sizes);
    unsigned nlive = 0, next_id = 0, i;
    uint64_t live_bytes = 0, target = (uint64_t)heap_size * 6 / 10;

    if (!live || !sizes)
        exit(1);
    for (i = 0; i < n; i++) {
        unsigned chance = live_bytes < target ? 70 : 30;
        if (nlive == 0 || next_rand() % 100 < chance) {
            ops[i].alloc = 1;
            ops[i].id = next_id;
            ops[i].size = sizes[next_id] = random_size();
            live_bytes += ops[i].size;
            live[nlive++] = next_id++;
        } else {
            unsigned k = next_rand() % nlive;
            ops[i].alloc = 0;
            ops[i].id = live[k];
            ops[i].size = 0;
            live_bytes -= sizes[live[k]];
            live[k] = live[--nlive];
        }
    }
    free(live);
    free(sizes);
    return next_id;
}

static void run_freelist(Result *res, Strategy s, const Op *ops, unsigned n, unsigned ids, uint32_t size)
{
    Heap h;
    uint32_t *addr = calloc(ids, sizeof *addr);
    double frag = 0, holes = 0, waste = 0;
    unsigned i, samples = 0;
    HeapStats st;

    if (!addr || !heap_init(&h, size))
        exit(1);
    memset(res, 0, sizeof *res);
    res->name = strategy_name(s);

    for (i = 0; i < n; i++) {
        if (ops[i].alloc) {
            res->attempts++;
            addr[ops[i].id] = heap_alloc(&h, ops[i].size, s);
            if (!addr[ops[i].id]) {
                res->failed++;
            } else {
                BlockInfo b;
                heap_block_of(&h, addr[ops[i].id], &b);
                waste += (double)(b.size - b.requested) / b.size;
            }
        } else if (addr[ops[i].id]) {
            heap_free(&h, addr[ops[i].id]);
            addr[ops[i].id] = 0;
        }
        heap_stats(&h, &st);
        frag += st.external_frag;
        holes += st.free_blocks;
        samples++;
    }
    res->search_per_alloc = (double)h.searched / (double)res->attempts;
    res->avg_frag = frag / samples;
    res->avg_holes = holes / samples;
    res->internal_waste = res->attempts > res->failed ? waste / (double)(res->attempts - res->failed) : 0;
    heap_destroy(&h);
    free(addr);
}

static void buddy_stats(const Buddy *b, uint32_t *free_bytes, uint32_t *largest, uint32_t *holes)
{
    BuddyBlock blk;
    uint32_t off = 0;

    *free_bytes = *largest = *holes = 0;
    while (buddy_block_at(b, off, &blk)) {
        uint32_t sz = 1u << blk.order;
        if (!blk.used) {
            *free_bytes += sz;
            (*holes)++;
            if (sz > *largest)
                *largest = sz;
        }
        off += sz;
    }
}

static void run_buddy(Result *res, const Op *ops, unsigned n, unsigned ids, uint32_t size)
{
    Buddy b;
    uint32_t *addr = calloc(ids, sizeof *addr);
    double frag = 0, holes = 0, waste = 0;
    unsigned i, samples = 0;

    if (!addr || !buddy_init(&b, size))
        exit(1);
    memset(res, 0, sizeof *res);
    res->name = "buddy system";

    for (i = 0; i < n; i++) {
        uint32_t fb, lg, hc;
        if (ops[i].alloc) {
            res->attempts++;
            addr[ops[i].id] = buddy_alloc(&b, ops[i].size);
            if (!addr[ops[i].id]) {
                res->failed++;
            } else {
                BuddyBlock blk;
                uint32_t bs;
                buddy_block_at(&b, addr[ops[i].id] - BUDDY_HDR, &blk);
                bs = 1u << blk.order;
                waste += (double)(bs - blk.requested) / bs;
            }
        } else if (addr[ops[i].id]) {
            buddy_free(&b, addr[ops[i].id]);
            addr[ops[i].id] = 0;
        }
        buddy_stats(&b, &fb, &lg, &hc);
        frag += fb ? 1.0 - (double)lg / fb : 0.0;
        holes += hc;
        samples++;
    }
    res->search_per_alloc = (double)b.searched / (double)res->attempts;
    res->avg_frag = frag / samples;
    res->avg_holes = holes / samples;
    res->internal_waste = res->attempts > res->failed ? waste / (double)(res->attempts - res->failed) : 0;
    buddy_destroy(&b);
    free(addr);
}

void run_comparison(unsigned ops, unsigned seed, uint32_t heap_size)
{
    Op *trace = malloc(ops * sizeof *trace);
    Result res[NUM_STRATEGIES + 1];
    unsigned ids, i, best_fail = 0, best_frag = 0, best_search = 0, best_waste = 0;
    int s;

    if (!trace)
        exit(1);
    rng = seed ? seed : 1;
    ids = make_trace(trace, ops, heap_size);

    for (s = 0; s < NUM_STRATEGIES; s++)
        run_freelist(&res[s], (Strategy)s, trace, ops, ids, heap_size);
    run_buddy(&res[NUM_STRATEGIES], trace, ops, ids, heap_size);

    printf("\nSame workload of %u operations (%u allocations, seed %u) on a %u byte heap,\n"
           "live data kept near 60%% of the heap\n\n", ops, ids, seed, (unsigned)heap_size);
    printf("  %-13s %8s %7s %11s %10s %9s %10s\n", "Strategy", "Success", "Failed",
           "Search/req", "Ext.frag", "Holes", "Int.waste");
    printf("  ------------- -------- ------- ----------- ---------- --------- ----------\n");
    for (i = 0; i <= NUM_STRATEGIES; i++) {
        Result *r = &res[i];
        printf("  %-13s %7.1f%% %7lu %11.1f %9.1f%% %9.1f %9.1f%%\n", r->name,
               100.0 * (double)(r->attempts - r->failed) / (double)r->attempts, r->failed,
               r->search_per_alloc, r->avg_frag * 100, r->avg_holes, r->internal_waste * 100);
        if (r->failed < res[best_fail].failed)
            best_fail = i;
        if (r->avg_frag < res[best_frag].avg_frag)
            best_frag = i;
        if (r->search_per_alloc < res[best_search].search_per_alloc)
            best_search = i;
        if (r->internal_waste < res[best_waste].internal_waste)
            best_waste = i;
    }
    printf("\n  Fewest failures      : %s\n", res[best_fail].name);
    printf("  Least fragmentation  : %s\n", res[best_frag].name);
    printf("  Shortest search      : %s\n", res[best_search].name);
    printf("  Least wasted space   : %s\n", res[best_waste].name);
    printf("\n  Search/req = free blocks (or buddy lists) looked at per request.\n");
    printf("  Ext.frag   = 1 - largest free block / total free, averaged over the run.\n");
    printf("  Int.waste  = share of each block not used by the data (tags, guard, rounding).\n\n");
    free(trace);
}
