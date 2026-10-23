// Lab 6: Worst-Fit allocator. Rename this file to your student ID before make.
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <string.h>

#define ARENA_SIZE 1024
#define MAX_BLOCKS 64
#define MAX_IDS 100

typedef struct {
    size_t offset;      // Byte offset from arena.
    size_t size;        // Size in bytes.
    int id;             // 0 means FREE; a positive value means USED by that ID.
    unsigned long last_used; // Timestamp of this block's most recent allocation.
} Block;

static unsigned char *arena; //Pointer to the mapped arena
static Block blocks[MAX_BLOCKS];

// Number of valid entries at the beginning of blocks[]. It changes only when
// insert_block() adds an entry or remove_block() removes one.
static int block_count;

// Logical clock for allocations. On a successful allocation, increment it
// first; that new value is the allocated block's timestamp (last_used).
static unsigned long use_clock;

/*
 * Helper function — no need to change this function.
 *
 * Input:
 *   index - position where the new block should be inserted.
 *   block - block metadata to store at that position.
 *
 * Output:
 *   Returns 1 if the block was inserted.
 *   Returns 0 if the index is invalid or blocks[] is already full.
 */
int insert_block(int index, Block block) {
    if (index < 0 || index > block_count || block_count == MAX_BLOCKS) {
        return 0;
    }

    for (int i = block_count; i > index; i--) {
        blocks[i] = blocks[i - 1];
    }

    blocks[index] = block;
    block_count++;
    return 1;
}

/*
 * Helper function — no need to change this function.
 *
 * Input:
 *   index - position of the block to remove.
 *
 * Output:
 *   No return value.
 *   If index is valid, the block is removed and later entries are shifted left.
 *   If index is invalid, nothing is changed.
 */
void remove_block(int index) {
    if (index < 0 || index >= block_count) {
        return;
    }

    for (int i = index; i < block_count - 1; i++) {
        blocks[i] = blocks[i + 1];
    }

    block_count--;
}

/*
 * Helper function — no need to change this function.
 *
 * Input:
 *   None. The function uses the current block list.
 *
 * Output:
 *   Returns the smallest positive ID in 1..MAX_IDS that is not currently
 *   used by any allocated block.
 *   Returns -1 if every ID is already in use.
 */
static int smallest_unused_id(void) {
    for (int id = 1; id <= MAX_IDS; id++) {
        int is_used = 0;

        for (int i = 0; i < block_count; i++) {
            if (blocks[i].id == id) {
                is_used = 1;
                break;
            }
        }

        if (!is_used) {
            return id;
        }
    }

    return -1;
}

/*
 * Find the block to use for a worst-fit allocation.
 *
 * Input:
 *   size - number of bytes requested by the caller.
 *
 * Output:
 *   Returns the index of the best free block that can hold size bytes.
 *   Choose the largest fitting free block. If multiple blocks have the same
 *   size, choose the one with the smallest last_used value. If those are
 *   also equal, choose the block with the smaller offset.
 *   Returns -1 if no free block can hold the requested size.
 *
 * This function only chooses a block; it must not change allocator state.
 */
static int find_worst_fit(size_t size) {
    // TODO
    return -1;
}

/*
 * Allocate a block of memory.
 *
 * Input:
 *   size - number of bytes requested by the caller.
 *
 * Output:
 *   Returns the ID assigned to the new allocation.
 *   Returns -1 if the request cannot be satisfied.
 *
 * On success, update the block list, choose the smallest unused ID, and
 * assign the next value of use_clock as the allocation's last_used timestamp.
 */
static int alloc_block(size_t size) {
    // TODO: reject zero and oversized requests.
    // TODO: choose a block with find_worst_fit().
    // TODO: split a larger chosen block into USED left + FREE right.
    // TODO: choose the smallest unused ID in 1..MAX_IDS and return it.
    // TODO: increment use_clock and record it as the chosen block's last_used.
    return -1;
}

/*
 * Free an existing allocation.
 *
 * Input:
 *   id - ID of the allocation to release.
 *
 * Output:
 *   Returns 1 if the ID refers to an active allocation and it is freed.
 *   Returns 0 if the ID is invalid or is not currently allocated.
 *
 * When a block is freed, coalesce it with adjacent free blocks. A merged
 * block keeps the larger last_used value of the blocks being combined.
 */
static int free_block(int id) {
    // TODO: find the used block whose id matches.
    // TODO: mark it free, then coalesce adjacent free blocks.
    //       A merged free block keeps the larger last_used value.
    return 0;
}

/*
 * Helper function — no need to change this function.
 *
 * Input:
 *   None.
 *
 * Output:
 *   No return value.
 *   Prints every current block in offset order, including its offset, size,
 *   state, and last_used timestamp.
 *
 * This shows allocator metadata only; it does not print the bytes in arena.
 */
static void show_block(void) {
    printf("Offset  Size  State  Last used\n");

    for (int i = 0; i < block_count; i++) {
        printf("%6zu  %4zu  %-4s   %lu\n", blocks[i].offset, blocks[i].size,
               blocks[i].id == 0 ? "FREE" : "USED", blocks[i].last_used);
    }
}

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

    // TODO: use one private, anonymous mmap() for ARENA_SIZE readable/writable bytes.
    // Variable arena points to the mapped area.


    
    if (arena == MAP_FAILED) {
        perror("mmap");
        fclose(input);
        return 1;
    }

    blocks[0] = (Block){0, ARENA_SIZE, 0, 0};
    block_count = 1;
    use_clock = 0;

    char command[16];
    while (fscanf(input, "%15s", command) == 1) {
        if (strcmp(command, "show") == 0) {
            show_block();
        } else if (strcmp(command, "alloc") == 0) {
            size_t size;

            if (fscanf(input, "%zu", &size) != 1) {
                break;
            }

            int id = alloc_block(size);
            if (id < 0) {
                printf("alloc %zu -> FAILED\n", size);
            } else {
                for (int i = 0; i < block_count; i++) {
                    if (blocks[i].id == id) {
                        printf("alloc %zu -> id %d, offset %zu, size %zu\n",
                               size, id, blocks[i].offset, blocks[i].size);
                        break;
                    }
                }
            }
        } else if (strcmp(command, "free") == 0) {
            int id;

            if (fscanf(input, "%d", &id) != 1) {
                break;
            }

            printf("free %d -> %s\n", id,
                   free_block(id) ? "OK" : "INVALID ID");
        } else {
            fprintf(stderr, "Expected alloc <bytes>, free <id>, or show\n");
            fclose(input);
            // TODO: unmap the entire arena before returning.
            return 1;
        }
    }

    fclose(input);
    // TODO: unmap the entire arena before returning.
    return 0;
}
