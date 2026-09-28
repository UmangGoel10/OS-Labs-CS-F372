/* TA reference solution: a small buddy allocator inside one mmap-backed arena. */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define ARENA_SIZE 1024
#define MIN_BLOCK 64
#define SLOT_COUNT (ARENA_SIZE / MIN_BLOCK)
#define MAX_IDS 100

typedef struct {
    size_t size;                 /* Zero means no block starts at this slot. */
    int free;                    /* Meaningful only when size is nonzero. */
} Block;

static unsigned char *arena;
/* Slot i describes a block starting at byte offset i * MIN_BLOCK.
 * Metadata lives outside the arena, so all arena bytes can hold user data. */
static Block blocks[SLOT_COUNT];

/* Round a valid request up to a supported power-of-two block size. */
static size_t round_size(size_t requested) {
    if (requested == 0 || requested > ARENA_SIZE)
        return 0;
    size_t size = MIN_BLOCK;
    while (size < requested)
        size *= 2;
    return size;
}

/* Prefer the smallest suitable free block; break ties by lowest offset. */
static int find_free_block(size_t needed) {
    int best = -1;
    for (int i = 0; i < SLOT_COUNT; i++)
        if (blocks[i].free && blocks[i].size >= needed &&
            (best == -1 || blocks[i].size < blocks[best].size))
            best = i;
    return best;
}

/* Accept only the start of a currently allocated block in our arena. */
static int allocated_index(void *ptr) {
    uintptr_t address = (uintptr_t)ptr, base = (uintptr_t)arena;
    if (ptr == NULL || address < base || address - base >= ARENA_SIZE)
        return -1;
    size_t offset = address - base;
    if (offset % MIN_BLOCK != 0)
        return -1;
    int index = (int)(offset / MIN_BLOCK);
    return blocks[index].size != 0 && !blocks[index].free ? index : -1;
}

/* Allocate from the arena; return NULL when the request cannot be satisfied. */
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
        /* Both halves are free; keep splitting the left half if necessary. */
        blocks[index] = (Block){half, 1};
        blocks[right] = (Block){half, 1};
    }   
    blocks[index].free = 0;
    return arena + index * MIN_BLOCK;
}

// Free one allocation and repeatedly combine it with a free, same-size buddy.
void my_free(void *ptr) {
    int index = allocated_index(ptr);
    if (index == -1)
        return;

    blocks[index].free = 1;
    while (blocks[index].size < ARENA_SIZE) {
        size_t size = blocks[index].size;

        // Number equal-size positions across the arena starting from zero.
        // Positions 0 and 1 form a pair, then 2 and 3, and so on.
        int slots_per_block = (int)(size / MIN_BLOCK);
        int block_number = index / slots_per_block;
        int buddy;

        if (block_number % 2 == 0) {
            // This is the left half of the pair; the buddy is to the right.
            buddy = index + slots_per_block;
        } else {
            // This is the right half of the pair; the buddy is to the left.
            buddy = index - slots_per_block;
        }

        // A buddy that is used or split into smaller blocks cannot merge.
        if (blocks[buddy].free == 0 || blocks[buddy].size != size)
            break;

        int left, right;
        if (index < buddy) {
            left = index;
            right = buddy;
        } else {
            left = buddy;
            right = index;
        }

        // The combined free block starts where the left half started.
        blocks[left].size = size * 2;
        blocks[left].free = 1;

        // The right half no longer has a separate block-start entry.
        blocks[right].size = 0;
        blocks[right].free = 0;

        // Recalculate the buddy for the larger block on the next iteration.
        index = left;
    }
}

/* Show block boundaries, including holes, without machine-dependent addresses. */
static void show_blocks(void) {
    printf("Offset  Size  State\n");
    for (int i = 0; i < SLOT_COUNT; i++)
        if (blocks[i].size != 0)
            printf("%6d  %4zu  %s\n", i * MIN_BLOCK, blocks[i].size,
                   blocks[i].free ? "FREE" : "USED");
}

/* Supplied driver: map memory, run a short instruction file, then unmap it. */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <commands.txt>\n", argv[0]);
        return 1;
    }
    FILE *input = fopen(argv[1], "r");
    if (input == NULL) {
        perror("fopen");
        return 1;
    }
    /* Anonymous means no backing file. Only ARENA_SIZE bytes are ours to use. */
    arena = mmap(NULL, ARENA_SIZE, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED) {
        perror("mmap");
        fclose(input);
        return 1;
    }
    blocks[0] = (Block){ARENA_SIZE, 1};

    void *allocations[MAX_IDS + 1] = {0};
    size_t next_id = 1, value;
    char command[16];
    int result = 0;
    /* Input is a TA-supplied script: alloc <bytes>, free <id>, or show. */
    while (fscanf(input, "%15s", command) == 1) {
        if (strcmp(command, "show") == 0) {
            show_blocks();
            continue;
        }
        if ((strcmp(command, "alloc") != 0 && strcmp(command, "free") != 0) ||
            fscanf(input, "%zu", &value) != 1) {
            fprintf(stderr, "Expected alloc <bytes>, free <id>, or show\n");
            result = 1;
            break;
        }
        if (strcmp(command, "alloc") == 0) {
            if (next_id > MAX_IDS) {
                fprintf(stderr, "Too many allocation IDs in this script\n");
                result = 1;
                break;
            }
            void *ptr = my_malloc(value);
            if (ptr == NULL) {
                printf("alloc %zu -> FAILED\n", value);
                continue;
            }
            allocations[next_id] = ptr;
            printf("alloc %zu -> id %zu, offset %zu, size %zu\n", value,
                   next_id++, (size_t)((unsigned char *)ptr - arena),
                   round_size(value));
        } else if (value == 0 || value >= next_id || allocations[value] == NULL) {
            printf("free %zu -> INVALID ID\n", value);
        } else {
            my_free(allocations[value]);
            allocations[value] = NULL;
            printf("free %zu -> OK\n", value);
        }
    }
    fclose(input);
    /* Individual frees reuse arena space; only shutdown returns it to the OS. */
    if (munmap(arena, ARENA_SIZE) == -1) {
        perror("munmap");
        result = 1;
    }
    return result;
}