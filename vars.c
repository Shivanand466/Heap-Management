#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "vars.h"

#define MAP_WIDTH 64
#define MAP_ROWS 200

bool rt_init(Runtime *rt, uint32_t heap_size)
{
    memset(rt->vars, 0, sizeof rt->vars);
    rt->strategy = FIRST_FIT;
    return heap_init(&rt->heap, heap_size);
}

void rt_destroy(Runtime *rt)
{
    heap_destroy(&rt->heap);
    memset(rt->vars, 0, sizeof rt->vars);
}

static int find(const Runtime *rt, const char *name)
{
    int i;
    for (i = 0; i < MAX_VARS; i++)
        if (rt->vars[i].in_use && strcmp(rt->vars[i].name, name) == 0)
            return i;
    return -1;
}

static int need(const Runtime *rt, const char *name)
{
    int i = find(rt, name);
    if (i < 0)
        printf("No variable named '%s'.\n", name);
    return i;
}

static bool valid_name(const char *name)
{
    size_t i, n = strlen(name);

    if (n == 0 || n >= VAR_NAME || !(isalpha((unsigned char)name[0]) || name[0] == '_'))
        return false;
    for (i = 1; i < n; i++)
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_'))
            return false;
    return true;
}

static void report_free_status(HeapStatus st, const char *name)
{
    if (st == HEAP_OVERFLOW_DETECTED)
        printf("WARNING: guard bytes after '%s' were overwritten - buffer overflow detected!\n", name);
    else if (st == HEAP_DOUBLE_FREE)
        printf("ERROR: double free detected for '%s'.\n", name);
    else if (st == HEAP_BAD_POINTER)
        printf("ERROR: '%s' does not point to a valid block.\n", name);
}

/* frees the block and forgets the variable; pointers to it are dropped */
static void release_var(Runtime *rt, int idx, bool verbose)
{
    Var *v = &rt->vars[idx];
    int i, k;

    report_free_status(heap_free(&rt->heap, v->addr), v->name);
    for (i = 0; i < MAX_VARS; i++) {
        Var *o = &rt->vars[i];
        if (!o->in_use || i == idx)
            continue;
        for (k = 0; k < o->nlinks; k++) {
            if (o->links[k] == idx) {
                if (verbose)
                    printf("Note: '%s' pointed to '%s' - that pointer is now dangling, removed.\n",
                           o->name, v->name);
                o->links[k] = o->links[--o->nlinks];
                k--;
            }
        }
    }
    v->in_use = false;
}

void rt_alloc(Runtime *rt, const char *name, uint32_t count, uint32_t size, Strategy s, bool zeroed)
{
    uint32_t addr, total;
    int i, slot = -1;

    if (!valid_name(name)) {
        printf("Invalid name. Use letters, digits and _ (max %d chars).\n", VAR_NAME - 1);
        return;
    }
    if (find(rt, name) >= 0) {
        printf("'%s' already exists. Free it first or use realloc.\n", name);
        return;
    }
    for (i = 0; i < MAX_VARS && slot < 0; i++)
        if (!rt->vars[i].in_use)
            slot = i;
    if (slot < 0) {
        printf("Too many variables (max %d).\n", MAX_VARS);
        return;
    }

    total = zeroed ? count * size : size;
    if (zeroed)
        addr = heap_calloc(&rt->heap, count, size, s);
    else
        addr = heap_alloc(&rt->heap, size, s);
    if (!addr) {
        HeapStats st;
        heap_stats(&rt->heap, &st);
        printf("Allocation failed: no free block is big enough "
               "(free %u bytes in %u block%s, largest %u).\n",
               (unsigned)st.free_bytes, (unsigned)st.free_blocks, st.free_blocks == 1 ? "" : "s",
               (unsigned)st.largest_free);
        if (st.free_blocks > 1 && st.free_bytes >= heap_block_need(total))
            printf("There is enough free memory in total - try 'compact'.\n");
        return;
    }

    memset(&rt->vars[slot], 0, sizeof(Var));
    rt->vars[slot].in_use = true;
    strcpy(rt->vars[slot].name, name);
    rt->vars[slot].addr = addr;
    rt->vars[slot].size = total;
    rt->vars[slot].root = true;
    {
        BlockInfo b;
        heap_block_of(&rt->heap, addr, &b);
        printf("%s: %u bytes at address %u (block of %u bytes, %s)\n", name, (unsigned)total,
               (unsigned)addr, (unsigned)b.size, strategy_name(s));
    }
}

void rt_realloc(Runtime *rt, const char *name, uint32_t size)
{
    int i = need(rt, name);
    uint32_t addr;

    if (i < 0)
        return;
    addr = heap_realloc(&rt->heap, rt->vars[i].addr, size, rt->strategy);
    if (!addr) {
        printf("realloc failed - '%s' keeps its old block.\n", name);
        return;
    }
    printf("%s: %u -> %u bytes, %s (address %u)\n", name, (unsigned)rt->vars[i].size,
           (unsigned)size, addr == rt->vars[i].addr ? "resized in place" : "moved", (unsigned)addr);
    rt->vars[i].addr = addr;
    rt->vars[i].size = size;
}

void rt_free(Runtime *rt, const char *name)
{
    int i = find(rt, name);
    if (i < 0) {
        printf("'%s' is not allocated (already freed?).\n", name);
        return;
    }
    release_var(rt, i, true);
    printf("Freed '%s'.\n", name);
}

void rt_write(Runtime *rt, const char *name, const char *text)
{
    int i = need(rt, name);
    size_t n;
    unsigned char *p;

    if (i < 0)
        return;
    n = strlen(text) + 1;
    p = heap_ptr(&rt->heap, rt->vars[i].addr);
    if (n > rt->vars[i].size) {
        n = rt->vars[i].size;
        printf("Text truncated to %u bytes to fit '%s'.\n", (unsigned)n, name);
    }
    memcpy(p, text, n);
    if (n == rt->vars[i].size)
        p[n - 1] = '\0';
    printf("Wrote %u bytes into '%s'.\n", (unsigned)n, name);
}

void rt_read(Runtime *rt, const char *name)
{
    int i = need(rt, name);
    const unsigned char *p;
    uint32_t k;

    if (i < 0)
        return;
    p = heap_ptr(&rt->heap, rt->vars[i].addr);
    printf("%s = \"", name);
    for (k = 0; k < rt->vars[i].size && p[k]; k++)
        putchar(isprint(p[k]) ? p[k] : '.');
    printf("\"\n");
}

void rt_overflow(Runtime *rt, const char *name, uint32_t n)
{
    int i = need(rt, name);
    BlockInfo b;
    uint32_t room;

    if (i < 0)
        return;
    heap_block_of(&rt->heap, rt->vars[i].addr, &b);
    room = b.size - 2 * HEAP_TAG - b.requested;
    if (n > room) {
        printf("Limited to %u bytes so the block tags stay readable.\n", (unsigned)room);
        n = room;
    }
    memset(heap_ptr(&rt->heap, rt->vars[i].addr) + b.requested, 'X', n);
    printf("Wrote %u bytes past the end of '%s'. Try 'check' or 'free %s'.\n",
           (unsigned)n, name, name);
}

void rt_link(Runtime *rt, const char *from, const char *to, bool add)
{
    int a = need(rt, from), b, k;
    Var *v;

    if (a < 0 || (b = need(rt, to)) < 0)
        return;
    v = &rt->vars[a];
    for (k = 0; k < v->nlinks; k++)
        if (v->links[k] == b)
            break;
    if (add) {
        if (k < v->nlinks)
            printf("'%s' already points to '%s'.\n", from, to);
        else if (v->nlinks == MAX_LINKS)
            printf("'%s' already has %d pointers.\n", from, MAX_LINKS);
        else {
            v->links[v->nlinks++] = b;
            printf("'%s' now points to '%s'.\n", from, to);
        }
    } else {
        if (k == v->nlinks)
            printf("'%s' does not point to '%s'.\n", from, to);
        else {
            v->links[k] = v->links[--v->nlinks];
            printf("Removed pointer %s -> %s.\n", from, to);
        }
    }
}

void rt_set_root(Runtime *rt, const char *name, bool root)
{
    int i = need(rt, name);
    if (i < 0)
        return;
    rt->vars[i].root = root;
    printf("'%s' is %s.\n", name, root ? "now a root (directly reachable)"
                                      : "no longer a root (reachable only through pointers)");
}

static void mark(Runtime *rt, int i)
{
    int k;
    if (rt->vars[i].marked)
        return;
    rt->vars[i].marked = true;
    for (k = 0; k < rt->vars[i].nlinks; k++)
        mark(rt, rt->vars[i].links[k]);
}

static int mark_all(Runtime *rt)
{
    int i, reachable = 0;

    for (i = 0; i < MAX_VARS; i++)
        rt->vars[i].marked = false;
    for (i = 0; i < MAX_VARS; i++)
        if (rt->vars[i].in_use && rt->vars[i].root)
            mark(rt, i);
    for (i = 0; i < MAX_VARS; i++)
        if (rt->vars[i].in_use && rt->vars[i].marked)
            reachable++;
    return reachable;
}

void rt_gc(Runtime *rt)
{
    int i, freed = 0, reachable = mark_all(rt);
    uint32_t bytes = 0;

    printf("Mark: %d block%s reachable from the roots.\n", reachable, reachable == 1 ? "" : "s");
    for (i = 0; i < MAX_VARS; i++) {
        Var *v = &rt->vars[i];
        if (v->in_use && !v->marked) {
            BlockInfo b;
            heap_block_of(&rt->heap, v->addr, &b);
            printf("Sweep: freeing '%s' (%u bytes, unreachable)\n", v->name, (unsigned)v->size);
            bytes += b.size;
            freed++;
            release_var(rt, i, false);
        }
    }
    if (freed)
        printf("Garbage collection freed %d block%s, %u bytes.\n", freed, freed == 1 ? "" : "s",
               (unsigned)bytes);
    else
        printf("Nothing to collect.\n");
}

static void moved(uint32_t old_addr, uint32_t new_addr, void *ctx)
{
    Runtime *rt = ctx;
    int i;
    for (i = 0; i < MAX_VARS; i++) {
        if (rt->vars[i].in_use && rt->vars[i].addr == old_addr) {
            printf("  %-12s %7u -> %u\n", rt->vars[i].name, (unsigned)old_addr, (unsigned)new_addr);
            rt->vars[i].addr = new_addr;
            return;
        }
    }
}

void rt_compact(Runtime *rt)
{
    HeapStats before, after;

    heap_stats(&rt->heap, &before);
    printf("Compacting (moving blocks down and updating every pointer):\n");
    heap_compact(&rt->heap, moved, rt);
    heap_stats(&rt->heap, &after);
    printf("Free space: %u block%s -> %u block%s (largest %u -> %u bytes)\n",
           (unsigned)before.free_blocks, before.free_blocks == 1 ? "" : "s",
           (unsigned)after.free_blocks, after.free_blocks == 1 ? "" : "s",
           (unsigned)before.largest_free, (unsigned)after.largest_free);
}

static const char *owner(const Runtime *rt, uint32_t addr)
{
    int i;
    for (i = 0; i < MAX_VARS; i++)
        if (rt->vars[i].in_use && rt->vars[i].addr == addr)
            return rt->vars[i].name;
    return "?";
}

void rt_show(const Runtime *rt)
{
    const Heap *h = &rt->heap;
    BlockInfo b;
    HeapStats st;
    uint32_t off = 0, cell = h->size / MAP_WIDTH;
    int rows = 0, c;
    char map[MAP_WIDTH + 1];

    printf("\n  %-9s %8s  %-6s %-14s %s\n", "Offset", "Block", "State", "Variable", "Bytes used");
    printf("  --------- --------  ------ -------------- ----------\n");
    while (heap_block_at(h, off, &b)) {
        if (rows++ < MAP_ROWS) {
            if (b.used)
                printf("  %-9u %8u  USED   %-14s %u\n", (unsigned)b.offset, (unsigned)b.size,
                       owner(rt, b.offset + HEAP_TAG), (unsigned)b.requested);
            else
                printf("  %-9u %8u  free\n", (unsigned)b.offset, (unsigned)b.size);
        }
        off += b.size;
    }
    if (rows > MAP_ROWS)
        printf("  ... %d more blocks\n", rows - MAP_ROWS);

    /* one character per 1/64th of the heap: # used, . free, : mixed */
    for (c = 0; c < MAP_WIDTH; c++) {
        uint32_t lo = (uint32_t)c * cell, hi = lo + cell, used = 0;
        off = 0;
        while (heap_block_at(h, off, &b) && off < hi) {
            uint32_t s = b.offset > lo ? b.offset : lo;
            uint32_t e = b.offset + b.size < hi ? b.offset + b.size : hi;
            if (b.used && e > s)
                used += e - s;
            off += b.size;
        }
        map[c] = used == 0 ? '.' : used >= cell ? '#' : ':';
    }
    map[MAP_WIDTH] = '\0';

    heap_stats(h, &st);
    printf("\n  [%s]\n", map);
    printf("  heap %u B | used %u block%s, %u B (%u requested) | free %u block%s, %u B\n",
           (unsigned)h->size, (unsigned)st.used_blocks, st.used_blocks == 1 ? "" : "s",
           (unsigned)st.used_bytes, (unsigned)st.requested_bytes, (unsigned)st.free_blocks,
           st.free_blocks == 1 ? "" : "s", (unsigned)st.free_bytes);
    printf("  largest free block %u B | external fragmentation %.1f%% | default strategy: %s\n\n",
           (unsigned)st.largest_free, st.external_frag * 100, strategy_name(rt->strategy));
}

void rt_list_vars(const Runtime *rt)
{
    int i, k, count = 0;

    for (i = 0; i < MAX_VARS; i++) {
        const Var *v = &rt->vars[i];
        if (!v->in_use)
            continue;
        if (count++ == 0) {
            printf("\n  %-14s %8s %8s  %-5s %s\n", "Variable", "Address", "Bytes", "Root", "Points to");
            printf("  -------------- -------- --------  ----- ---------\n");
        }
        printf("  %-14s %8u %8u  %-5s ", v->name, (unsigned)v->addr, (unsigned)v->size,
               v->root ? "yes" : "no");
        for (k = 0; k < v->nlinks; k++)
            printf("%s%s", k ? ", " : "", rt->vars[v->links[k]].name);
        printf("%s\n", v->nlinks ? "" : "-");
    }
    if (!count)
        printf("No variables allocated.\n");
    else
        printf("\n");
}

void rt_check(const Runtime *rt)
{
    char msg[160];
    bool ok = heap_check(&rt->heap, msg, sizeof msg);
    printf("%s: %s\n", ok ? "OK" : "PROBLEM", msg);
}

void rt_leak_report(Runtime *rt)
{
    int i, leaked = 0, live = 0;

    mark_all(rt);
    for (i = 0; i < MAX_VARS; i++) {
        if (!rt->vars[i].in_use)
            continue;
        if (rt->vars[i].marked) {
            live++;
        } else {
            if (!leaked++)
                printf("Leak report - unreachable blocks never freed:\n");
            printf("  %-14s %u bytes at %u\n", rt->vars[i].name, (unsigned)rt->vars[i].size,
                   (unsigned)rt->vars[i].addr);
        }
    }
    if (!leaked)
        printf("No leaks.");
    if (live)
        printf("%s%d block%s still reachable, released at exit.", leaked ? "" : " ", live,
               live == 1 ? "" : "s");
    printf("\n");
}
