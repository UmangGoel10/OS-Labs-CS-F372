# OS Eval Prep — Process Management + Buddy Allocator

Scope: everything from `Eval1 prep` (`fork`, `execvp`, `wait`/`waitpid`, exit
status, signals) combined with Lab05's buddy allocator (`mmap`, splitting,
coalescing power-of-two blocks). Nothing here is a new syscall — it's the
same primitives from both labs, forced to interact through real OS
constraints.

## 0. The one fact that drives everything below

`fork()` gives a child a **copy-on-write private copy** of the parent's
memory — including any `mmap(..., MAP_PRIVATE, ...)` arena and any global
array like `buddy.c`'s `static Block blocks[SLOT_COUNT]`. Nothing a child
does to either is visible to its parent or siblings unless you deliberately
change that (`MAP_SHARED`, or communicating explicitly). Everything in
section 2 is really this one fact applied to a different scenario.

## 1. Building blocks

### Private arena across a fork
Map once with `MAP_PRIVATE`, initialize `blocks[0]`, **then** fork. Every
child gets its own independent copy of both the arena bytes and the
metadata table. See `practice/fork_arena_demo.c` — it's complete and
runnable, read it before writing anything.

### Shared arena across a fork
`MAP_SHARED` makes the **arena bytes** shared — writes in one process become
visible to others. It does **not** make a plain global array like
`blocks[SLOT_COUNT]` shared; that's ordinary process memory, private and
copy-on-write regardless of what flag the arena's `mmap` used. A truly
shared allocator needs its metadata *also* placed in shared memory (a second
`MAP_SHARED` mapping, or a struct with both regions together).

### Reporting results across a fork
A child can't hand a pointer back to its parent — the address means nothing
in a different address space. Two mechanisms from the labs so far:
exit status (`_exit(code)` in the child, `WIFEXITED`/`WEXITSTATUS` in the
parent — one byte 0–255 of information), or a pipe (arbitrary data, read in
the parent after the write end is closed in both the child *and* the
parent's own copy of it).

### Memory as a scarce resource in a scheduler
A job-scheduling loop (`poll_running` + "try to start whatever's eligible")
already exists in `Eval1 prep/practice/job_scheduler.c`, gated by a
dependency graph. Here, the gate is memory: a job may only fork+exec once
`my_malloc()` can actually reserve its requested size, and its memory is
freed back to the pool the instant its process exits (not sooner).

### Internal fragmentation, under real timing
Two free blocks can sit right next to each other in address space and still
be unable to merge, because merging requires being *buddies* (same size,
correct pair position), not just adjacent and free. Lab05's own
`examples/fragmentation.txt` demonstrates this with plain alloc/free
commands; `practice/mem_fragmentation.txt` demonstrates the identical effect
with real running jobs, where "adjacent free" arrives at different times
because two different child processes finished at different times.

## 2. Practice scenarios

Read `practice/fork_arena_demo.c` first — it's the complete, working
version of the private-arena pattern everything else builds on. Then work
`practice/mem_scheduler.c`'s three TODOs in order against these inputs:

### Scenario A — No contention
`practice/mem_basic.txt`: three small jobs, the arena never runs out of
room. Confirms your `try_start_job`/`poll_running`/`run_scheduler` loop
works at all before anything interesting happens.

### Scenario B — Private vs. shared, on paper
Before running `fork_arena_demo.c`, predict its output: three children each
request 700 bytes from a 1024-byte arena. Do all three succeed? Would your
answer change if the `mmap` used `MAP_SHARED` instead — and if it did, would
merely changing that flag be *enough* to make the second request correctly
fail? (It wouldn't — see section 1's shared-arena note. This is the most
commonly missed distinction in this whole topic.)

### Scenario C — Contention forces serialization
`practice/mem_contention.txt`: two 512-byte jobs exactly fill a 1024-byte
arena; a third 512-byte job must wait for one of them to finish. Trace by
hand which jobs run simultaneously and in what order DONE lines should
print, then run it and compare.

### Scenario D — Fragmentation blocks a job that "should" fit
`practice/mem_fragmentation.txt`: four 256-byte jobs exactly fill the arena
(offsets 0/256/512/768, same layout as Lab05's `fragmentation.txt`). Two
finish quickly, freeing 512 bytes total — but as two separate, non-buddy
256-byte blocks. A fifth job needing 512 bytes must stay PENDING even
though enough total memory is technically free, until the right neighbor
frees and buddies-merge into one contiguous 512-byte block. Find the exact
moment that happens and be ready to explain it in terms of buddy position,
not just "not enough memory."

### Scenario E — Undoing a reservation on fork failure
In `try_start_job`, deliberately imagine `fork()` returns `-1` *after*
`my_malloc()` already succeeded. What does your code do? If it does
nothing, that block is permanently lost — leaked from the pool forever,
since no process will ever free it. Write down the one extra call that
fixes it before you check your implementation against this case.

### Scenario F — Subprocess wiring (optional, exec-focused)
Write a short program that forks a child which `execvp`s the compiled
`../Lab05/buddy` binary on a generated command file, with the child's
stdout captured through a pipe in the parent (count how many `alloc ... ->
FAILED` lines appear). Trains: pipe fd bookkeeping (which end each side
closes and when — get this backwards and `read()` blocks forever waiting
for an EOF that can never come), plus the same "child never returns from a
successful exec" discipline as Eval1's Scenario F.

---

## 3. Quick self-check questions

- A parent `mmap()`s an arena `MAP_PRIVATE`, initializes `blocks[0]`, then
  forks three children. Does child A's `my_malloc()` call change what child
  B sees in *its* `blocks[]`? Why not?
- Changing `MAP_PRIVATE` to `MAP_SHARED` for the arena bytes: does that
  alone make two children's allocations conflict-free with each other? What
  second piece of state also needs to be shared for that to be true?
- Why can't a forked child just return a `void *` from `my_malloc()` to its
  parent the way a normal function call would?
- In a fork-based scheduler, what's the one line you'd add right after a
  failed `fork()` to make sure a `my_malloc()`'d block doesn't leak forever?
- Two free blocks sit at adjacent offsets in the arena. Under what condition
  can they NOT merge into one larger block, even though they're both free
  and physically next to each other?
- If your scheduler frees a job's block *before* checking its exit status
  instead of after, what could that let a later job do that shouldn't be
  possible yet?
- Why must a subprocess's pipe write-end be closed in the *parent* as well
  as inherited/closed correctly in the child, for `read()` in the parent to
  ever see EOF?
