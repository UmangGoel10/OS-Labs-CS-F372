# Lab 6: Student Strikes Back

**Preet Methi** scored a **whopping 0** in the previous lab. Naturally, he decided that the only reasonable solution was to make sure the next lab was impossible.

Unfortunately for everyone, he somehow found out that **TA Crash Man** was preparing the next lab. Even worse, Preet managed to get access to Crash Man's SSD, where the lab would be stored.

There was just one problem: **the lab wasn't on the SSD yet**.

So Preet came up with a brilliant plan. If he couldn't sabotage the lab itself, he would make the SSD completely unusable by making its memory allocation as inefficient as possible — by turning it into a **worst-fit allocator**.

As you already know, Preet doesn't know OS. So he needs **you** to do it for him.

## Skeleton Code

The file `student.c` is the starting point for your implementation. Rename it to your student ID before running `make`.

The skeleton already provides the `Block` structure, allocator state, helper functions, `show_block()`, and `main()`. You only need to complete the functions marked with `TODO`.

### The arena and block list

The arena contains `ARENA_SIZE` bytes (1024 bytes in this lab). Block metadata is stored separately in `blocks[]`.

Each `Block` contains:

- `offset` — byte offset from the start of the arena.
- `size` — size of the block in bytes.
- `id` — `0` for a free block, or its allocation ID.
- `last_used` — timestamp used for worst-fit tie-breaking.

The valid entries are `blocks[0]` through `blocks[block_count - 1]`, kept in increasing offset order.

Other important variables are:

- `arena` — pointer to the memory arena.
- `block_count` — number of valid blocks.
- `use_clock` — logical clock for successful allocations. Increment it before assigning a timestamp.
- `MAX_BLOCKS` — maximum number of block descriptors.
- `MAX_IDS` — maximum allocation ID.

A freed block keeps its `last_used` value. A new free remainder created by splitting has `last_used = 0`. When free blocks are merged, the merged block keeps the larger `last_used` value.

`main()` sets up the arena, opens the input file, and loops through the commands, calling `alloc`, `free`, or `show` as appropriate. It then cleans up the arena and closes the file.

### Input and Output

The program takes an input file containing one command per line:

- `alloc <size>` — allocate a block.
- `free <id>` — free a block.
- `show` — display the current block list.

The program prints the result of each allocation or free operation. `show` prints each block's offset, size, state, and `last_used` value. **The required `printf` statements are already provided in the skeleton; you do not need to implement the output formatting.**

### How the allocator works

For `alloc`, `alloc_block()` finds a suitable free block using `find_worst_fit()`. Worst-fit selection is:

1. Choose the **largest block**.
2. If sizes are equal, choose the **smallest `last_used`**.
3. If those are equal, choose the **smallest `offset`**.

If the selected block is larger than requested, it is split:

```text
USED requested bytes | FREE remaining bytes
```

> `alloc_block()` returns `-1` if there is no free block large enough to satisfy the requested size.

## TODOs

You need to implement the following functions in `student.c`:

- `find_worst_fit()` — find the free block to use for an allocation.
- `alloc_block()` — allocate memory, including splitting a block when necessary.
- `free_block()` — free an allocation and coalesce adjacent free blocks.

You also need to complete the `mmap()` and `munmap()` TODOs in `main()`.

## Example run

The following input demonstrates splitting, freeing, timestamp tie-breaking,
ID reuse, and coalescing:

```text
alloc 256
alloc 512
alloc 256
free 1
free 3
alloc 128
free 1
free 2
show
```

In each state below, `i` is the block-array index, `F` means `id == 0`,
`U(id)` means the block is used by that ID, and `t` is `last_used`.

### Initial state

Initially, `use_clock = 0`, `block_count = 1`, and the only block describes the
entire arena.

```text
i   offset   size   state   t
0       0    1024   F       0
```

`show` would print:

```text
Offset  Size  State  Last used
     0  1024  FREE   0
```

### 1. `alloc 256`

The only free block fits the request, so worst-fit selects it. Since it is
larger than 256 bytes, split it into a 256-byte used block and a 768-byte free
remainder. The smallest unused ID is 1. Increment `use_clock` from 0 to 1 and
store 1 as the allocation's timestamp.

```text
alloc 256 -> id 1, offset 0, size 256
```

```text
Offset  Size  State  Last used
     0   256  USED   1
   256   768  FREE   0
```

### 2. `alloc 512`

The only free block is the 768-byte block at offset 256. Split it into a
512-byte used block and a 256-byte free remainder. Assign ID 2 and increment
`use_clock` to 2.

```text
alloc 512 -> id 2, offset 256, size 512
```

```text
Offset  Size  State  Last used
     0   256  USED   1
   256   512  USED   2
   768   256  FREE   0
```

### 3. `alloc 256`

The remaining free block is an exact fit, so no split occurs. Assign ID 3 and
increment `use_clock` to 3.

```text
alloc 256 -> id 3, offset 768, size 256
```

```text
Offset  Size  State  Last used
     0   256  USED   1
   256   512  USED   2
   768   256  USED   3
```

### 4. `free 1`

Block 0 becomes free, but it cannot merge because its right neighbour is used.
Its `last_used` value remains 1 and `use_clock` does not change.

```text
free 1 -> OK
```

```text
Offset  Size  State  Last used
     0   256  FREE   1
   256   512  USED   2
   768   256  USED   3
```

### 5. `free 3`

Block 2 becomes free and retains timestamp 3. It cannot merge with block 1
because ID 2 is still used.

```text
free 3 -> OK
```

```text
Offset  Size  State  Last used
     0   256  FREE   1
   256   512  USED   2
   768   256  FREE   3
```

### 6. `alloc 128`: the tie-breaker

There are two fitting free blocks, both of size 256. Their timestamps are 1
and 3, so the block with timestamp 1 is selected. The lower-offset rule is not
needed because the timestamps differ.

Split the selected block into a 128-byte used block and a 128-byte free
remainder. ID 1 is now available again, so it is reused. Increment `use_clock`
from 3 to 4.

```text
alloc 128 -> id 1, offset 0, size 128
```

```text
Offset  Size  State  Last used
     0   128  USED   4
   128   128  FREE   0
   256   512  USED   2
   768   256  FREE   3
```

### 7. `free 1`: coalescing with the next block

Block 0 is freed and its right neighbour is already free, so the two blocks
are merged. The resulting block has size 256 and keeps the larger timestamp:
`max(4, 0) = 4`.

```text
free 1 -> OK
```

```text
Offset  Size  State  Last used
     0   256  FREE   4
   256   512  USED   2
   768   256  FREE   3
```

### 8. `free 2`: coalescing with both neighbours

The middle 512-byte block is freed. It merges with the free block on the left
and then with the free block on the right. The final block keeps
`max(4, 2, 3) = 4`.

```text
free 2 -> OK
```

```text
Offset  Size  State  Last used
     0  1024  FREE   4
```

The final state is `use_clock = 4`, `block_count = 1`, with one free block
covering the entire 1024-byte arena.

## Build and test

```sh
make                 # compile your one student-ID .c file
make test            # run public tests
make test N=5        # run only public test 5
make test N="1 2 3"  # run public tests 1, 2, and 3
make clean           # remove build files
```

---

## Debugging

You can compile and run directly without the Makefile:

```sh
gcc -Wall -Wextra -g 2024A7PS0001G.c -o worst_fit
./worst_fit public/tests/04_tie_break.txt
```

To make your own test, create a text file containing `alloc`, `free`, and
`show` commands, then pass its path to the program:

```text
alloc 256
alloc 128
free 1
show
```

```sh
./worst_fit my_test.txt
```

For crashes or incorrect state transitions, use:

```sh
gdb --args ./worst_fit my_test.txt
```

Useful commands include `break alloc_block`, `break free_block`, `run`, `next`,
`print block_count`, `print use_clock`, and `print blocks[0]`.
