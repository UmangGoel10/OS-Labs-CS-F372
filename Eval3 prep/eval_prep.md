# OS Eval 3 Prep — Demand Paging (Lab 7)

Scope: **only Lab 7** (`Lab 7/`): the policy half of a demand-paging system — victim selection,
eviction, loading — on top of a provided simulator (4 frames, 12 pages, swap, a backing file).
Nothing here is a new concept; Eval 3 will be Lab 7's machinery with a **twist**, the same way
Eval 2 was Lab05 + Lab 2 forced together.

> **Heads-up:** `Lab 7/paging.c` is still the empty skeleton in your repo. Do **P0** first —
> if you can't write the baseline cold, none of the twists will go well.

## How to use this folder

```text
Eval3 prep/
  eval_prep.md              <- this file: concepts, brainstorm, scenarios, drills
  practice/
    run.sh                  <- ./run.sh p1_fifo | ./run.sh p1_fifo sol | ./run.sh p1_fifo 3
    framework/              <- Lab 7's simulator + 2 small extensions (reference bit, STATS cmd)
    p0_lab7_baseline/       <- reference solution for the ORIGINAL Lab 7 (14/14 public tests)
    p1_fifo/ … p5_readahead/   paging.c (skeleton, TODOs)  solution.c  tests/  expected/
    p6_trace_sim/           <- from-scratch simulator: trace_sim.c (skeleton) + solution.c
```

Workflow per scenario: read the twist in its `paging.c` header → **predict** one test's output on
paper → implement → `./run.sh <scenario>` → only then peek at `solution.c`.
Every `solution.c` is verified (all tests pass); the P6 solution was also cross-checked against an
independent model on 300 random traces.

---

## 0. The one idea that drives everything

A page's **authoritative copy lives in exactly one place**, and every function in this lab is just
"where is it now, and where must it go?":

| Page kind | Where the real contents are | Evict it → | Load it ← |
|---|---|---|---|
| anonymous, never evicted | **nowhere** (new page) | — | zero-fill |
| anonymous, evicted before | **swap slot** (`swap_slot != -1`) | **always** write to swap, clean or dirty | read swap, then **free the slot** |
| file, clean | the **file** (identical) | discard, **no I/O** | read file |
| file, dirty | **RAM is newer** than the file | **write back** to file first | read file |

Minor fault = no disk I/O needed (zero-fill). Major fault = swap-in or file read.

## 1. Headers & build facts

`paging_sim.h` already includes `<stdbool.h> <stddef.h> <stdint.h> <stdio.h> <stdlib.h>`.
**`<string.h>` is NOT included in your `paging.c`** (only the framework uses it) — add it yourself if
a twist needs `memcpy`/`strcmp`. Build is `gcc -Wall -Wextra -std=c99 -g`: unused parameters warn
(silence with `(void)x;`), `for (int i = 0; …)` and `//` comments are fine in C99.

## 2. The API contract (memorise the order rules)

| Call | Does | Rule that bites |
|---|---|---|
| `sim_get_page_info(p)` | snapshot copy of metadata | it's a **copy** — re-fetch after any mutating call |
| `sim_alloc_swap_slot()` | **lowest** free slot; exits the program if swap is full | slot numbers in expected outputs depend on this |
| `sim_free_swap_slot(s)` | slot reusable; `-1` ignored | *you* free it after a swap-in — nobody else does |
| `sim_write_to_swap(frame,s)` / `sim_read_from_swap(frame,s)` | data moves, prints `[DISK I/O]` | |
| `sim_write_to_file(frame,fid,off)` / `sim_read_from_file(...)` | data moves, prints `[FILE I/O]` | write only for **dirty file** pages |
| `sim_zero_fill_frame(f)` | zeroes frame | first-touch anon only |
| `sim_set_swap_slot(p,s)` | records the slot on the page | README order: call it before `mark_page_evicted` |
| `sim_mark_page_evicted(p)` | resident=0, frame=-1, **dirty=0**; **keeps** `swap_slot` | read `dirty`/`frame_id` **before** this |
| `sim_mark_page_loaded(p,f)` | resident=1, **dirty=0, `swap_slot=-1`**, `last_access_time`=now | read `swap_slot` **before** this |

Canonical shapes:

```c
evict (anon):  slot = info.swap_slot; if (slot == -1) slot = alloc();
               write_to_swap(info.frame_id, slot); set_swap_slot(p, slot); mark_evicted(p);
evict (file):  if (info.dirty) write_to_file(info.frame_id, info.file_id, info.file_offset);
               mark_evicted(p);                       /* clean: nothing to write */
load  (anon):  info = get(p);  slot != -1 ? (read_from_swap + free_slot) : zero_fill;  mark_loaded
load  (file):  read_from_file(frame, info.file_id, info.file_offset);  mark_loaded
```

### Framework quirks that WILL bite (all verified against the code)

1. **Your code never sees a hit.** It runs only on faults (and when the driver calls it directly).
   Anything that must react to *every access* (LFU, hit counters, working-set) cannot be built from
   these hooks alone — you only get `last_access_time`/`dirty` (and `referenced` in the extended copy).
2. `LOAD`/`EVICT`/`SELECT` driver commands call **your functions directly**, with arbitrary frames
   and no fault logic around them. Don't assume `handle_page_fault` was on the stack.
3. `LOAD` doesn't advance the clock, so directly-loaded pages show `last=0`. Ties are common ⇒ the
   **smaller `page_id` tie-break** actually matters (Lab 7 test 03).
4. On `READ`/`WRITE`: clock++ → fault → your `load_page` → `mark_loaded` stamps `last=clock` → the
   access stamps `last=clock` again. A `WRITE` sets `dirty` **after** the page is loaded.
5. `select_victim_page()` returns a **page id**. The frame comes from
   `sim_get_page_info(victim).frame_id` — fetch it **before** `evict_page` wipes it.
6. `handle_page_fault` takes the **lowest-numbered free frame** first; only a full memory calls
   `select_victim_page()`.
7. `FAULT <p>` on an already-resident page is invalid input (it would map the page twice).
8. Dirty flag ≠ "safe to discard" for anonymous pages in this lab (README says so explicitly).

---

## 3. Brainstorm — what could Eval 3 ask?

Pattern from Eval 1/2: *you get a partly-written program, fill the TODOs, a twist forces you to
re-derive something instead of recalling it.* Likelihood is my estimate, not inside information.

### 3a. What your code can and can't observe (decides what's even possible)

| Information | Available in Lab 7? | Source |
|---|---|---|
| resident / frame / dirty / type / swap slot / file id+offset | yes | `sim_get_page_info` |
| recency (LRU) | yes | `last_access_time` |
| load order (FIFO) | **only if you record it** | in `load_page` (static array) |
| reference bit | **no** in the original; **yes** in this folder's `framework/` | `PageInfo.referenced` + `sim_clear_referenced` |
| access count (LFU/MFU) | **no** — hits are invisible | would need a framework hook; *ask* |
| future references (OPT) | **no** | only possible in a trace simulator (P6) |
| frame contents | **no** (private) | zero-page checks would need a new API |

### 3b. Candidate twists

| # | Category | Example prompt | What you change | Likelihood |
|---|---|---|---|---|
| A | **Different victim policy** | FIFO · pure LRU · MRU · Clock · NRU/enhanced clock `(ref,dirty)` · cost-based ("cheapest I/O first") · random with fixed LCG seed · "never evict a page used in the last K ticks" | `select_victim_page` (+ state in `load_page`) | ★★★ |
| B | **Different priority rules** | swap bucket order; "file pages first"; dirty anon before clean file; weighted cost | `category()` / comparison | ★★★ |
| C | **Different eviction I/O** | swap cache (skip rewrite if copy current); discard *never-written* clean anon (zero page); reuse vs. re-allocate slots; limit swap to K slots → evict file pages when full | `evict_page` (+ `load_page` slot handling) | ★★★ |
| D | **Different load behaviour** | read-ahead / prefetch; sequential-access detection; always zero-fill on first touch | `load_page`, handler | ★★ |
| E | **Accounting / reporting** | minor vs major faults, swap-in/out counts, write-backs, per-page fault counts, `STATS` | counters in `load/evict`, a print function | ★★★ |
| F | **Write the handler** | implement `handle_page_fault` yourself: free-frame search, victim, evict, load, return frame; handle "no victim" | `handle_page_fault` | ★★ |
| G | **Pinned / locked pages** | `pinned[]` skipped by victim selection; all pinned ⇒ return `-1`/fail | selection + state | ★★ |
| H | **Other layers** | virtual→physical address translation; your own page table (`struct {valid,dirty,ref,frame}`); TLB (fully-assoc., LRU) + invalidation; two-level table | new code around the lab | ★★ |
| I | **Whole simulator from scratch** | read a reference/trace file, run FIFO/LRU/Clock/OPT, report faults/hits/write-backs | everything (P6) | ★★ |
| J | **Environment changes** | different `NUM_FRAMES`/`NUM_PAGES`, more files, shared file pages (refcount), per-process tables with global vs local replacement, fork + copy-on-write (ties to Eval 2) | structure of the model | ★ |
| K | **Paper/short answer** | Belady's anomaly, EAT, page-table size, TLB reach, working set, thrashing | none (see §6) | ★★ |

---

## 4. Practice scenarios

Do them in this order. Time targets assume the real eval's ~60 min budget feels tight.

| Id | Folder | Twist | Trains | Target |
|---|---|---|---|---|
| **P0** | `practice/p0_lab7_baseline/` | none — the original Lab 7 | the contract in §2, tie-breaking, `-1` | 20 min |
| **P1** | `p1_fifo/` | victim = earliest **loaded** | own state across calls; "hits are invisible"; reload requeues | 10 min |
| **P2** | `p2_clock/` | **Clock / second chance** with a reference bit | stateful hand, side effects of `SELECT`, termination, free frames | 15 min |
| **P3** | `p3_swap_cache/` | keep swap slot after swap-in; skip write for unmodified pages | the `mark_page_loaded` slot-reset trap, slot reuse vs leak | 15 min |
| **P4** | `p4_fault_stats/` | count minor/major faults, swap & file I/O, add `STATS` | where each event is observable; exact output format | 10 min |
| **P5** | `p5_readahead/` | **you write `handle_page_fault`** + sequential read-ahead | orchestration order, frame bookkeeping, never evict for prefetch | 20 min |
| **P6** | `p6_trace_sim/` | simulator from scratch: translate, 4 policies, dirty/write-back, (stretch) TLB | everything without a safety net | 40 min |

### P0 — Lab 7 from memory
Do the original lab (`Lab 7/paging.c`, `make test`) *without* looking, then compare with
`practice/p0_lab7_baseline/paging_solution.c`. Check you can state, without code: the 4 buckets,
why anon-clean outranks file-clean, which of the three functions touches `dirty`, `swap_slot`, and
`last_access_time`.

### P1 — FIFO
*"Replace the policy by FIFO: evict the page resident the longest. Ignore type/dirty/recency."*
Trap: hits don't reach your code — if FIFO "feels like it needs the access", it doesn't. Test 03
checks that a **reloaded** page goes to the back.

### P2 — Clock
*"The framework now exposes a reference bit. Implement second-chance with a hand that walks the
frames."* Traps: (1) `SELECT` has side effects too; (2) don't spin forever when nothing is
resident; (3) skip free frames; (4) the hand ends **after** the victim's frame.
Variation to try after: **enhanced clock** — classes `(ref,dirty)`: `(0,0) < (0,1) < (1,0) < (1,1)`.

### P3 — Swap cache
*"A page that came back from swap and was never modified shouldn't cost another swap write."*
Traps: `sim_mark_page_loaded` **wipes** `swap_slot` (re-attach with `sim_set_swap_slot` *after* it);
a dirty page with a slot must **overwrite the same slot** (a new slot leaks the old one); a clean
page with **no** slot still must be written (nothing is in swap yet).
Variation to try after: also **discard** a clean anon page that has *no* slot. That is only sound
*because P3 keeps slots*: now "clean + no slot" really means "never written, still all zeros". In
the original Lab 7 the same flags describe a page that was swapped in and may hold non-zero data
(that is the README's warning and Lab 7 test 05) — be able to explain the difference.

### P4 — Fault accounting
Exact one-line `STATS` format is in the file. Decide *which function sees each event* before coding
(zero-fill vs swap-in vs file-read is only distinguishable in `load_page`; clean-discard only in
`evict_page`).

### P5 — Write the handler + read-ahead
Order matters: print → frame (free first, else victim; remember the victim's frame **before**
evicting) → load → read-ahead → return the **faulting** page's frame. Read-ahead only into a
**free** frame, only for the same file at `offset + PAGE_SIZE`, only if not resident, and **check
`page_id + 1 < sim_page_count()`** — `sim_get_page_info(12)` reads out of bounds (undefined
behaviour; the tests can't reliably catch it, a grader might).

### P6 — Simulator from scratch
Full spec is at the top of `trace_sim.c`. Do FIFO + LRU first (check against the 15 / 12 faults
in §6a), then CLOCK, then OPT, then the TLB stretch (tests 08–09). Traps: access numbers are
1-based; `vpn = addr / pagesize`; a faulting **write** dirties the page it just loaded; OPT ties
→ lowest frame; **evicting a page must invalidate its TLB entry** (test 09 exists for exactly this).

### Variations with no files (modify a solution yourself)
MRU · enhanced clock · cost-based eviction (`clean file=0, anon=1 swap write, dirty file=1`) ·
pinned pages (a `static bool pinned[NUM_PAGES]`, `-1` if all resident are pinned) · swap limited to
2 slots ⇒ pick a file page instead when swap is full (track slots yourself — the framework would
`exit()`) · "flush all dirty file pages every 3rd fault" (what API is missing? — a way to clear
`dirty`; this is the kind of thing you'd ask the examiner about).

---

## 5. Policy cheat sheet

| Policy | State you must keep | Victim rule | Pitfall |
|---|---|---|---|
| FIFO | load sequence per page | min `load_seq` | hit doesn't change order; reload = new seq |
| LRU | none (`last_access_time`) | min `last_access_time`, tie → min page id | `LOAD` leaves `last=0` ⇒ ties |
| MRU | none | **max** `last_access_time` | tie rule direction |
| Clock | `hand` (frame index) | ref=1 → clear & advance; ref=0 → victim | free frames, empty memory, ≤2 laps |
| Enhanced clock | `hand` | lap 1: first `(0,0)`; lap 2: first `(0,1)` clearing ref; repeat | up to 4 laps |
| NRU | none | lowest class `2·ref + dirty`, then lowest page id | classes 0–3, not four separate scans |
| LFU/MFU | access counts | min/max count | **needs a hit hook** the framework lacks |
| OPT | the whole future | farthest next use; never-again = ∞ | only in a trace simulator |
| Lab 7 | none | bucket 1→4, then LRU, then min id | category order beats recency |

Belady's anomaly applies to FIFO (and, as the table shows, Clock); **stack algorithms (LRU, OPT)
cannot** show it.

---

## 6. Paper drills

### 6a. Reference-string fault counts (answers in the table — cover and compute first)
Strings: **A** = `7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1`,
**B** = `1 2 3 4 1 2 5 1 2 3 4 5`, **C** = `2 3 2 1 5 2 4 5 3 2 5 2`. Clock: ref set on load/hit,
hand starts at frame 0, not moved while filling free frames.

<details><summary>Answers (computed by an independent model; A-3 matches the textbook 15/12/9)</summary>

| String, frames | FIFO | LRU | Clock | OPT |
|---|---|---|---|---|
| A, 3 | 15 | 12 | 14 | 9 |
| A, 4 | 10 | 8 | 9 | 8 |
| B, 3 | 9 | 10 | 9 | 7 |
| B, 4 | **10** | 8 | **10** | 6 |
| C, 3 | 9 | 7 | 8 | 6 |
| C, 4 | 6 | 6 | 6 | 5 |

B with 3→4 frames: FIFO 9→10 and Clock 9→10 (**anomaly**), LRU 10→8, OPT 7→6.
FIFO trace of A (3 frames), frames after each ref (`*` = fault, 15 of them): `[7]* [7 0]* [7 0 1]* [0 1 2]* hit [1 2 3]* [2 3 0]* [3 0 4]* [0 4 2]* [4 2 3]* [2 3 0]* hit hit [3 0 1]* [0 1 2]* hit hit [1 2 7]* [2 7 0]* [7 0 1]*`.
</details>

### 6b. Lab 7 hand-trace (predict, then check against `Lab 7/public/solutions/`)
Take `Lab 7/public/tests/08_fault_and_lru.txt` and `10_dirty_anonymous_over_dirty_file.txt`.
For each line write: output text, which `last=` value each page ends with, which swap slot gets used.
Then do the same for a test of your own with a `WRITE` on a file page followed by two faults.

### 6c. Numbers

1. Memory access 200 ns, fault service 8 ms, fault rate 1/1000. EAT? Max rate for ≤10 % slowdown?
2. `PAGE_SIZE=64`. Virtual address `0x1A7`: page, offset, type (pages 6–11 are file)? If that page is in frame 2, physical address?
3. 32-bit address, 4 KiB pages. Single-level table size (4-byte entries)? For `0x00403A7C` with a 10/10/12 split: outer index, inner index, offset?
4. TLB lookup 10 ns, memory 100 ns, single-level table, 90 % TLB hit rate. EAT?

<details><summary>Answers</summary>

1. EAT = 0.999·200 + 0.001·8 000 000 = **8199.8 ns** (~41× slower). 200 + p·7 999 800 ≤ 220 ⇒ p ≤ **2.5·10⁻⁶** (≈ 1 fault per 400 000 accesses).
2. 0x1A7 = 423 → page **6**, offset **39 (0x27)**, **file-backed** (file offset 0); frame 2 ⇒ 2·64 + 39 = **167**.
3. 2²⁰ entries × 4 B = **4 MiB**; indices **1**, **3**, offset **0xA7C**.
4. 0.9·(10+100) + 0.1·(10+100+100) = **120 ns**.
</details>

---

## 7. Debug checklist (in order)

1. Diff first line that differs — usually an **I/O line missing or extra**, not a wrong value.
2. Did you read `PageInfo` *before* `mark_page_loaded` / `mark_page_evicted`?
3. Slot numbers off by one? Check free-after-swap-in and "lowest free slot" reuse.
4. Victim wrong only in ties? Strict `<` while scanning ascending page ids keeps the smaller id.
5. Infinite loop? Clock with nothing resident, or a hand that never advances.
6. `-1` handling: empty memory, `SELECT` on empty, handler with no victim.
7. Compile with `-Wall -Wextra`: unused parameter, signed/unsigned compare on `size_t offset`.

## 8. Self-check questions

- Why must `load_page` read `swap_slot` before `sim_mark_page_loaded`, and what does `swap_slot` hold afterwards?
- After `evict_page` on an anonymous page, what are `resident / frame_id / dirty / swap_slot`?
- Why is a clean anonymous page still written to swap in Lab 7, and under what *changed* rules would skipping the write be safe?
- With these hooks, why can FIFO be implemented but LFU can't? What exact API would you ask the examiner for?
- In Clock, why does the hand walk **frames** rather than pages? What if it lands on a free frame?
- Why must read-ahead never evict? What breaks if it does (think: the page you just loaded)?
- Which `last_access_time` does a page show after `LOAD` vs after `READ`-triggered fault?
- Minor vs major fault: classify zero-fill, swap-in, file read, clean-file reload.
- Why does a dirty file page need a write-back but a dirty anonymous page "only" needs a swap write?
- Which replacement policies can exhibit Belady's anomaly, and why can't LRU?
- Why must a TLB entry be invalidated when its page is evicted? What output would reveal the bug?
- Original Lab 7 rules, A/B/C anonymous and all in RAM: write the swap-slot assignments after
  `evict A, evict B, load A, evict C`. (Which slot does C get, and why?)

## 9. Exam-day workflow

1. Read the whole prompt; mark what is **provided** vs **TODO**. Test file names are a spec — read them.
2. Hand-trace *one* expected output until you reproduce it exactly (learns `last=`, slot numbers, I/O line order).
3. Implement the smallest function first, run that test, then the next. Don't write all three blind.
4. Keep a 5-minute tail for edge cases (§7) and a clean compile.
5. If the twist needs information the framework doesn't expose (§3a), say so and ask — don't invent it.
