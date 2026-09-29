/*
 * Heap Management v2 - simulated heap with boundary tags, fit strategies,
 * buddy system, garbage collection and compaction
 * build: gcc -O2 main.c heap.c buddy.c vars.c sim.c -o heap
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "heap.h"
#include "buddy.h"
#include "vars.h"
#include "sim.h"

#define LINE_MAX_LEN 512
#define MAX_ARGS 6
#define DEFAULT_HEAP 65536u
#define BUDDY_DEMO_SIZE 1024u
#define BUDDY_VARS 32

static Runtime rt;

static bool read_line(const char *prompt, char *buf, size_t size)
{
    size_t len;

    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)size, stdin))
        return false;
    len = strlen(buf);
    if (len && buf[len - 1] == '\n') {
        buf[--len] = '\0';
    } else if (len == size - 1) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }
    if (len && buf[len - 1] == '\r')
        buf[--len] = '\0';
    return true;
}

/* splits into words; the rest of the line after the last word we need stays in *rest */
static int split(char *line, char **argv, int max, char **rest)
{
    int argc = 0;
    char *p = line;

    *rest = NULL;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (argc == max) {
            *rest = p;
            break;
        }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p) {
            *p++ = '\0';
            if (argc == 2 && !strcmp(argv[0], "write")) {
                while (*p == ' ' || *p == '\t')
                    p++;
                *rest = p;
                break;
            }
        }
    }
    return argc;
}

static bool parse_u32(const char *s, uint32_t max, uint32_t *out)
{
    char *end;
    unsigned long v;

    if (!s || *s == '-')
        return false;
    v = strtoul(s, &end, 10);
    if (end == s || *end || v == 0 || v > max)
        return false;
    *out = (uint32_t)v;
    return true;
}

static bool parse_strategy(const char *s, Strategy *out)
{
    static const char *names[] = { "first", "next", "best", "worst" };
    int i;

    for (i = 0; i < NUM_STRATEGIES; i++) {
        if (!strcmp(s, names[i])) {
            *out = (Strategy)i;
            return true;
        }
    }
    return false;
}

static void help(void)
{
    printf("\n  Memory\n");
    printf("    alloc <name> <bytes> [first|next|best|worst]\n");
    printf("    calloc <name> <count> <bytes> [strategy]   zero-filled\n");
    printf("    realloc <name> <bytes>                     grows in place when it can\n");
    printf("    free <name>\n");
    printf("    strategy <first|next|best|worst>           default fit strategy\n");
    printf("  Data\n");
    printf("    write <name> <text>    read <name>\n");
    printf("    overflow <name> <n>    write n bytes past the end (to test detection)\n");
    printf("  Garbage collection\n");
    printf("    link <from> <to>       from now holds a pointer to to\n");
    printf("    unlink <from> <to>\n");
    printf("    unroot <name>          variable goes out of scope (still allocated)\n");
    printf("    root <name>\n");
    printf("    gc                     mark and sweep\n");
    printf("    compact                slide blocks together, fix pointers\n");
    printf("  Inspect\n");
    printf("    show     vars     check\n");
    printf("  Other\n");
    printf("    compare [ops] [seed]   run every strategy on the same random workload\n");
    printf("    buddy                  buddy system demo\n");
    printf("    reset [bytes]          new empty heap (default %u)\n", DEFAULT_HEAP);
    printf("    help     exit\n\n");
}


typedef struct {
    char name[VAR_NAME];
    uint32_t addr;
} BuddyVar;

static void buddy_show(const Buddy *b, const BuddyVar *vars)
{
    BuddyBlock blk;
    uint32_t off = 0;
    int k, i;

    printf("\n  %-11s %5s %6s  %-5s %s\n", "Range", "Order", "Size", "State", "Variable");
    while (buddy_block_at(b, off, &blk)) {
        uint32_t sz = 1u << blk.order;
        const char *who = "";
        for (i = 0; i < BUDDY_VARS; i++)
            if (vars[i].name[0] && vars[i].addr == off + BUDDY_HDR)
                who = vars[i].name;
        printf("  %4u-%-6u %5d %6u  %-5s %s", (unsigned)off, (unsigned)(off + sz - 1), blk.order,
               (unsigned)sz, blk.used ? "USED" : "free", who);
        if (blk.used)
            printf(" (%u bytes asked, %u wasted)", (unsigned)blk.requested,
                   (unsigned)(sz - blk.requested - BUDDY_HDR));
        printf("\n");
        off += sz;
    }
    printf("  Free lists:");
    for (k = BUDDY_MIN_ORDER; k <= b->max_order; k++) {
        uint32_t cur = b->free_head[k];
        int count = 0;
        while (cur != 0xFFFFFFFFu) {
            count++;
            memcpy(&cur, b->mem + cur + 12, 4);
        }
        if (count)
            printf("  %uB x%d", 1u << k, count);
    }
    printf("\n\n");
}

static void buddy_demo(void)
{
    Buddy b;
    BuddyVar vars[BUDDY_VARS];
    char line[LINE_MAX_LEN], *argv[MAX_ARGS], *rest;
    int i;

    if (!buddy_init(&b, BUDDY_DEMO_SIZE)) {
        printf("Could not create buddy heap.\n");
        return;
    }
    memset(vars, 0, sizeof vars);
    printf("\nBuddy system on a %u byte heap. Blocks are powers of two from %u bytes.\n",
           (unsigned)b.size, 1u << BUDDY_MIN_ORDER);
    printf("Commands: alloc <name> <bytes>, free <name>, show, check, back\n");
    buddy_show(&b, vars);

    while (read_line("buddy> ", line, sizeof line)) {
        int argc = split(line, argv, MAX_ARGS, &rest);
        uint32_t n;

        if (argc == 0)
            continue;
        if (!strcmp(argv[0], "back") || !strcmp(argv[0], "exit"))
            break;
        if (!strcmp(argv[0], "show")) {
            buddy_show(&b, vars);
        } else if (!strcmp(argv[0], "check")) {
            char msg[128];
            bool ok = buddy_check(&b, msg, sizeof msg);
            printf("%s: %s\n", ok ? "OK" : "PROBLEM", msg);
        } else if (!strcmp(argv[0], "alloc") && argc == 3) {
            int slot = -1;
            bool dup = false;
            for (i = 0; i < BUDDY_VARS; i++) {
                if (vars[i].name[0] && !strcmp(vars[i].name, argv[1]))
                    dup = true;
                if (!vars[i].name[0] && slot < 0)
                    slot = i;
            }
            if (dup)
                printf("'%s' already exists.\n", argv[1]);
            else if (strlen(argv[1]) >= VAR_NAME)
                printf("Name too long.\n");
            else if (!parse_u32(argv[2], BUDDY_DEMO_SIZE, &n))
                printf("Size must be 1 to %u.\n", BUDDY_DEMO_SIZE);
            else if (slot < 0)
                printf("Too many variables.\n");
            else if (!(vars[slot].addr = buddy_alloc(&b, n)))
                printf("No block large enough.\n");
            else {
                strcpy(vars[slot].name, argv[1]);
                buddy_show(&b, vars);
            }
        } else if (!strcmp(argv[0], "free") && argc == 2) {
            for (i = 0; i < BUDDY_VARS; i++)
                if (vars[i].name[0] && !strcmp(vars[i].name, argv[1]))
                    break;
            if (i == BUDDY_VARS) {
                printf("No variable named '%s'.\n", argv[1]);
            } else {
                buddy_free(&b, vars[i].addr);
                vars[i].name[0] = '\0';
                buddy_show(&b, vars);
            }
        } else {
            printf("Commands: alloc <name> <bytes>, free <name>, show, check, back\n");
        }
    }
    buddy_destroy(&b);
}


static void quit(void)
{
    printf("\n");
    rt_leak_report(&rt);
    rt_destroy(&rt);
    printf("Goodbye.\n");
    exit(0);
}

static void run_command(int argc, char **argv, char *rest)
{
    const char *cmd = argv[0];
    uint32_t n, m, max = rt.heap.size;
    Strategy s = rt.strategy;

    if (!strcmp(cmd, "alloc")) {
        if (argc < 3 || argc > 4 || (argc == 4 && !parse_strategy(argv[3], &s)))
            printf("Usage: alloc <name> <bytes> [first|next|best|worst]\n");
        else if (!parse_u32(argv[2], max, &n))
            printf("Size must be between 1 and %u.\n", (unsigned)max);
        else
            rt_alloc(&rt, argv[1], 1, n, s, false);
    } else if (!strcmp(cmd, "calloc")) {
        if (argc < 4 || argc > 5 || (argc == 5 && !parse_strategy(argv[4], &s)))
            printf("Usage: calloc <name> <count> <bytes> [strategy]\n");
        else if (!parse_u32(argv[2], max, &n) || !parse_u32(argv[3], max, &m)
                 || (uint64_t)n * m > max)
            printf("count * bytes must be between 1 and %u.\n", (unsigned)max);
        else
            rt_alloc(&rt, argv[1], n, m, s, true);
    } else if (!strcmp(cmd, "realloc")) {
        if (argc != 3)
            printf("Usage: realloc <name> <bytes>\n");
        else if (!parse_u32(argv[2], max, &n))
            printf("Size must be between 1 and %u.\n", (unsigned)max);
        else
            rt_realloc(&rt, argv[1], n);
    } else if (!strcmp(cmd, "free")) {
        if (argc != 2)
            printf("Usage: free <name>\n");
        else
            rt_free(&rt, argv[1]);
    } else if (!strcmp(cmd, "strategy")) {
        if (argc != 2 || !parse_strategy(argv[1], &s))
            printf("Usage: strategy <first|next|best|worst>\n");
        else {
            rt.strategy = s;
            printf("Default strategy: %s\n", strategy_name(s));
        }
    } else if (!strcmp(cmd, "write")) {
        if (argc != 2 || !rest)
            printf("Usage: write <name> <text>\n");
        else
            rt_write(&rt, argv[1], rest);
    } else if (!strcmp(cmd, "read")) {
        if (argc != 2)
            printf("Usage: read <name>\n");
        else
            rt_read(&rt, argv[1]);
    } else if (!strcmp(cmd, "overflow")) {
        if (argc != 3 || !parse_u32(argv[2], max, &n))
            printf("Usage: overflow <name> <bytes>\n");
        else
            rt_overflow(&rt, argv[1], n);
    } else if (!strcmp(cmd, "link") || !strcmp(cmd, "unlink")) {
        if (argc != 3)
            printf("Usage: %s <from> <to>\n", cmd);
        else
            rt_link(&rt, argv[1], argv[2], cmd[0] == 'l');
    } else if (!strcmp(cmd, "root") || !strcmp(cmd, "unroot")) {
        if (argc != 2)
            printf("Usage: %s <name>\n", cmd);
        else
            rt_set_root(&rt, argv[1], cmd[0] == 'r');
    } else if (!strcmp(cmd, "gc")) {
        rt_gc(&rt);
    } else if (!strcmp(cmd, "compact")) {
        rt_compact(&rt);
    } else if (!strcmp(cmd, "show")) {
        rt_show(&rt);
    } else if (!strcmp(cmd, "vars")) {
        rt_list_vars(&rt);
    } else if (!strcmp(cmd, "check")) {
        rt_check(&rt);
    } else if (!strcmp(cmd, "compare")) {
        uint32_t ops = 5000, seed = (uint32_t)time(NULL);
        if (argc > 3 || (argc >= 2 && !parse_u32(argv[1], 1000000, &ops))
            || (argc == 3 && !parse_u32(argv[2], 0xFFFFFFFFu, &seed)))
            printf("Usage: compare [ops up to 1000000] [seed]\n");
        else
            run_comparison(ops, seed, DEFAULT_HEAP);
    } else if (!strcmp(cmd, "buddy")) {
        buddy_demo();
    } else if (!strcmp(cmd, "reset")) {
        n = DEFAULT_HEAP;
        if (argc > 2 || (argc == 2 && !parse_u32(argv[1], 64u << 20, &n)) || n < 256)
            printf("Usage: reset [bytes, 256 to %u]\n", 64u << 20);
        else {
            Strategy keep = rt.strategy;
            rt_destroy(&rt);
            if (!rt_init(&rt, n)) {
                printf("Out of memory.\n");
                exit(1);
            }
            rt.strategy = keep;
            printf("New heap of %u bytes.\n", (unsigned)rt.heap.size);
        }
    } else if (!strcmp(cmd, "help")) {
        help();
    } else if (!strcmp(cmd, "exit") || !strcmp(cmd, "quit")) {
        quit();
    } else {
        printf("Unknown command '%s'. Type help.\n", cmd);
    }
}

int main(void)
{
    char line[LINE_MAX_LEN], *argv[MAX_ARGS], *rest;

    if (!rt_init(&rt, DEFAULT_HEAP)) {
        printf("Out of memory.\n");
        return 1;
    }
    printf("Heap Management v2 - %u byte heap. Type help for commands.\n", (unsigned)rt.heap.size);
    while (read_line("heap> ", line, sizeof line)) {
        int argc = split(line, argv, MAX_ARGS, &rest);
        if (argc)
            run_command(argc, argv, rest);
    }
    quit();
    return 0;
}
