#ifndef VARS_H
#define VARS_H

#include "heap.h"

#define VAR_NAME 24
#define MAX_VARS 128
#define MAX_LINKS 8

typedef struct {
    bool in_use;
    char name[VAR_NAME];
    uint32_t addr;
    uint32_t size;
    bool root;
    bool marked;
    int links[MAX_LINKS];
    int nlinks;
} Var;

typedef struct {
    Heap heap;
    Var vars[MAX_VARS];
    Strategy strategy;
} Runtime;

bool rt_init(Runtime *rt, uint32_t heap_size);
void rt_destroy(Runtime *rt);

void rt_alloc(Runtime *rt, const char *name, uint32_t count, uint32_t size, Strategy s, bool zeroed);
void rt_realloc(Runtime *rt, const char *name, uint32_t size);
void rt_free(Runtime *rt, const char *name);
void rt_write(Runtime *rt, const char *name, const char *text);
void rt_read(Runtime *rt, const char *name);
void rt_overflow(Runtime *rt, const char *name, uint32_t n);

void rt_link(Runtime *rt, const char *from, const char *to, bool add);
void rt_set_root(Runtime *rt, const char *name, bool root);
void rt_gc(Runtime *rt);
void rt_compact(Runtime *rt);

void rt_show(const Runtime *rt);
void rt_list_vars(const Runtime *rt);
void rt_check(const Runtime *rt);
void rt_leak_report(Runtime *rt);

#endif
