# Heap Management

A command-line simulator for heap allocation, memory inspection, garbage collection, compaction, and buddy allocation.

## Build

Requires GCC or another C11-compatible compiler.

```sh
gcc -O2 -std=c11 -Wall -Wextra -pedantic main.c heap.c buddy.c vars.c sim.c -o heap
```

## Run

```sh
./heap
```

On Windows, run `heap.exe` from PowerShell or Command Prompt. Type `help` in the program to see available commands.

## Features

- Simulated allocation with first-fit, next-fit, best-fit, and worst-fit strategies.
- `alloc`, `calloc`, `realloc`, and `free` operations, plus block and variable inspection.
- Guard checks for detecting writes past an allocation's requested size.
- Pointer-link/root operations with mark-and-sweep garbage collection.
- Heap compaction with pointer updates, workload comparison, and a buddy-system demo.