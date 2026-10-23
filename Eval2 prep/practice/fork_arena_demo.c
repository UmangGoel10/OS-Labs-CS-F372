/*
 * fork_arena_demo: a private (COW) buddy arena, forked into several
 * children. Read this BEFORE touching mem_scheduler.c -- it's the
 * complete, working version of the pattern that file builds on.
 *
 * The parent mmap()s ONE 1024-byte arena MAP_PRIVATE and initializes it
 * (one free 1024-byte block) BEFORE forking. Each child then does its own
 * alloc/free against what looks like the same arena, but copy-on-write
 * means each child actually has an independent copy: nothing a child does
 * is visible to its siblings or to the parent. A child can't hand a
 * pointer back across the fork either, so it reports success/failure
 * purely through its exit status.
 *
 * Build:
 *     gcc -Wall -Wextra -o fork_arena_demo fork_arena_demo.c
 * Run:
 *     ./fork_arena_demo
 */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define ARENA_SIZE 1024
#define MIN_BLOCK 64
#define SLOT_COUNT (ARENA_SIZE / MIN_BLOCK)
#define NUM_CHILDREN 3

typedef struct {
    size_t size;
    int free;
} Block;

unsigned char *arena;
Block blocks[SLOT_COUNT];

size_t round_size(size_t requested) {
    if (requested == 0 || requested > ARENA_SIZE)
        return 0;
    size_t size = MIN_BLOCK;
    while (size < requested)
        size *= 2;
    return size;
}

int find_free_block(size_t needed) {
    int best = -1;
    for (int i = 0; i < SLOT_COUNT; i++)
        if (blocks[i].free && blocks[i].size >= needed &&
            (best == -1 || blocks[i].size < blocks[best].size))
            best = i;
    return best;
}

int allocated_index(void *ptr) {
    uintptr_t address = (uintptr_t)ptr, base = (uintptr_t)arena;
    if (ptr == NULL || address < base || address - base >= ARENA_SIZE)
        return -1;
    size_t offset = address - base;
    if (offset % MIN_BLOCK != 0)
        return -1;
    int index = (int)(offset / MIN_BLOCK);
    return blocks[index].size != 0 && !blocks[index].free ? index : -1;
}

void *my_malloc(size_t requested) {
    size_t needed = round_size(requested);
    if (needed == 0)
        return NULL;
    int index = find_free_block(needed);
    if (index == -1)
        return NULL;
    while (blocks[index].size > needed) {
        size_t half = blocks[index].size / 2;
        int right = index + (int)(half / MIN_BLOCK);
        blocks[index] = (Block){half, 1};
        blocks[right] = (Block){half, 1};
    }
    blocks[index].free = 0;
    return arena + index * MIN_BLOCK;
}

void my_free(void *ptr) {
    int index = allocated_index(ptr);
    if (index == -1)
        return;
    blocks[index].free = 1;
    while (blocks[index].size < ARENA_SIZE) {
        size_t size = blocks[index].size;
        int slots_per_block = (int)(size / MIN_BLOCK);
        int block_number = index / slots_per_block;
        int buddy = (block_number % 2 == 0) ? index + slots_per_block
                                             : index - slots_per_block;
        if (blocks[buddy].free == 0 || blocks[buddy].size != size)
            break;
        int left = (index < buddy) ? index : buddy;
        int right = (index < buddy) ? buddy : index;
        blocks[left] = (Block){size * 2, 1};
        blocks[right] = (Block){0, 0};
        index = left;
    }
}

/* Each child requests a different size, so you can see (via exit status)
 * that they all succeed independently even though the "same" arena can't
 * actually fit all three requests at once if it were truly shared. */
void child_run(int child_num, size_t request) {
    void *p = my_malloc(request);
    if (p == NULL) {
        fprintf(stderr, "child %d: alloc %zu FAILED\n", child_num, request);
        _exit(1);
    }
    fprintf(stderr, "child %d: alloc %zu -> offset %zu\n", child_num, request,
            (size_t)((unsigned char *)p - arena));
    my_free(p);
    _exit(0);
}

int main(void) {
    arena = mmap(NULL, ARENA_SIZE, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    blocks[0] = (Block){ARENA_SIZE, 1};

    size_t requests[NUM_CHILDREN] = {700, 700, 700}; /* each > half the arena */
    pid_t pids[NUM_CHILDREN];

    for (int i = 0; i < NUM_CHILDREN; i++) {
        pid_t pid = fork();
        if (pid == 0)
            child_run(i, requests[i]);
        pids[i] = pid;
    }

    int ok = 0;
    for (int i = 0; i < NUM_CHILDREN; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            ok++;
    }
    /* All three succeed: each child privately believes it owns the whole
     * 1024-byte arena, even though 3*700 bytes could never fit in one real
     * shared 1024-byte arena. That gap between "looks shared" (same
     * virtual address) and "is shared" (same physical pages, and same
     * metadata) is the whole point of this demo -- see eval_prep.md
     * Scenario B before you touch mem_scheduler.c. */
    printf("%d/%d children succeeded\n", ok, NUM_CHILDREN);

    munmap(arena, ARENA_SIZE);
    return 0;
}
