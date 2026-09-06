# OS Eval Prep — Process Management in C

Based on Lab01 (`parmake`: fork/execvp/wait) and Lab 2 (`task_manager`: fork/exec/signals/waitpid).
The eval will likely hand you a partially-written C program and have you fill in TODOs using
these same building blocks. Goal here: know every variation cold, then practice composing them.

---

## 1. Building blocks — what's possible to write

### `fork()`
- `pid_t pid = fork();`
- Three-way branch on return value:
  - `pid < 0` → fork failed, **no child was created**. Handle it (print error, skip, continue loop) — don't assume child exists.
  - `pid == 0` → you are **in the child**. Do child-only work here, then either `exec*` or `_exit()`.
  - `pid > 0` → you are **in the parent**. `pid` is the child's PID — save it if you'll need to `wait`/`waitpid`/`kill` it later.
- Child inherits open file descriptors, memory (copy-on-write), signal handlers — but gets its own PID and its own copy of variables from that point on.
- Forking in a loop to launch N children in parallel: each iteration's `pid` variable must be stored per-child (array indexed by loop counter), not overwritten and lost.

### `exec*()` family
- `execvp(path, argv)` — searches `$PATH`, takes an array of args (`argv[0]` = program name by convention, array **must be NULL-terminated**).
- `execv(path, argv)` — like execvp but needs the full/relative path, no `$PATH` search.
- `execl(path, arg0, arg1, ..., NULL)` — list form instead of array, still NULL-terminated.
- Variations you should recognize: `l` vs `v` (list vs vector args), `p` (search PATH), `e` (takes explicit `envp`).
- **Exec only returns if it fails.** Correct pattern:
  ```c
  execvp(prog, argv);
  perror("execvp");   // only reached on failure
  _exit(127);          // or some nonzero code — never fall through
  ```
- If exec succeeds, the child's code/memory is replaced entirely — anything written after a successful exec never runs (that's the "child never comes back" hint).
- Use `_exit()` not `exit()` in a child after fork when you want to skip flushing/duplicating the parent's stdio buffers (relevant if parent had buffered output).

### `wait()` / `waitpid()`
- `wait(&status)` — blocks until **any** child exits, reaps it, returns its pid.
- `waitpid(pid, &status, options)`:
  - `pid > 0` → wait for that specific child.
  - `pid == -1` → wait for any child (like `wait`).
  - `options = 0` → blocking.
  - `options = WNOHANG` → **non-blocking**: returns 0 immediately if the child hasn't exited yet, returns `pid` if it has, returns -1 on error. This is the mechanism for "check if a task finished without stopping the world" (used in `check_status`/`reap_all`-style polling).
- Looping `wait()` until it returns -1 (errno == ECHILD) reaps *all* children — the common "wait for everyone" pattern (parmake-style: fork N, then loop calling `wait` N times or until no children remain).
- **Matching pid back to task/file**: since children finish in arbitrary order, `wait`/`waitpid` returns the pid of whichever exited — you need a lookup (array/struct search by pid) to know *which* file/task that corresponds to.

### Interpreting exit `status`
- `WIFEXITED(status)` — true if it exited normally (via `return`/`exit`/`_exit`).
- `WEXITSTATUS(status)` — the actual exit code (only meaningful if `WIFEXITED` is true).
- `WIFSIGNALED(status)` — true if killed by a signal instead of exiting normally.
- `WTERMSIG(status)` — which signal killed it (only meaningful if `WIFSIGNALED`).
- `WIFSTOPPED(status)` / `WSTOPSIG(status)` — only relevant if you waited with `WUNTRACED`; tells you the child was stopped (e.g. by `SIGSTOP`), not exited.
- "Did it succeed?" almost always = `WIFEXITED(status) && WEXITSTATUS(status) == 0`.

### Signals (`signal.h`, `<sys/wait.h>`)
- `kill(pid, sig)` — despite the name, just *sends a signal*; doesn't have to kill.
- Common signals and their default effect:
  - `SIGSTOP` — pause the process, **can't be caught/ignored/blocked**. Use for "pause".
  - `SIGCONT` — resume a stopped process. Can be caught with a handler (see `handle_sigcont` in Lab 2) to notice you were resumed.
  - `SIGTERM` — polite "please exit," catchable/ignorable. Default action = terminate.
  - `SIGKILL` — force kill, **can't be caught/ignored/blocked**. Use when you must guarantee death.
  - `SIGINT` — what Ctrl-C sends.
- Installing a handler: `struct sigaction sa = {0}; sa.sa_handler = my_fn; sigaction(SIGCONT, &sa, NULL);`
- `volatile sig_atomic_t` flag pattern: handler just sets a flag; the flag is checked in a normal loop elsewhere. (Never do heavy work inside a handler.)
- After `kill(pid, SIGTERM/SIGKILL/SIGSTOP)`, the target only actually changes state at some point later — if you need to know it's really gone, you still must `wait`/`waitpid` on it (a killed process becomes a zombie until reaped, exactly like a normally-exited one).
- **Pause ≠ Terminate**: pause = `SIGSTOP` (reversible, no reaping yet, process stays alive suspended); terminate = `SIGTERM`/`SIGKILL` + reap (process is gone for good, must call `waitpid` afterward or it zombies).

### Timekeeping
- `clock_gettime(CLOCK_MONOTONIC, &ts)` → wall-clock-ish time immune to system clock adjustments; convert `ts.tv_sec + ts.tv_nsec/1e9` to a double.
- "Active time only" pattern (Lab 2's `custom_sleep`): accumulate elapsed time in a loop, but reset your "last checked" timestamp when a resume happens, so paused intervals don't count.

### File descriptors / I/O redirection
- `open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644)` then `dup2(fd, STDOUT_FILENO)` — redirect stdout to a file (test-mode logging pattern).
- Descriptors are inherited across `fork()`, so redirecting before forking affects children too.

### Data structures for tracking children
- Parallel arrays or a struct array: `pid[i]`/`filename[i]` or a `Task` struct with id/pid/status/timestamps.
- Linear search by id or by pid (`find()`-style) since there's no built-in map in C.
- An enum for state machines: e.g. `RUNNING, PAUSED, TERMINATED, DONE` — and the *rules* for which transitions are legal (can't pause a DONE task, resuming a RUNNING task is a no-op, etc.) are usually the actual bug surface.

---

## 2. Practice scenarios (combine the blocks above)

Do these on paper or in a scratch `.c` file, no peeking at the labs. Each builds on the last.

### Scenario A — Basic parallel fork/exec/wait
Write a program that takes a list of shell commands (e.g. `"ls"`, `"sleep 2"`, `"false"`) as
`argv`, forks one child per command, execs each with `execvp`, and after launching all of them,
waits for all to finish and prints `"<cmd>: OK"` or `"<cmd>: FAILED"` based on exit status.
*Trains: fork loop + pid bookkeeping array, execvp with NULL-terminated argv, wait-loop matching
pid back to the right command, WIFEXITED/WEXITSTATUS.*

### Scenario B — Fail-fast vs fail-soft
Same as A, but: if `fork()` itself fails partway through launching children, don't crash — just
record that one as failed and keep launching the rest. At the end, only print a final "ALL OK"
if literally everything (including ones that failed to even fork) succeeded.
*Trains: handling `pid < 0` as a distinct case from "process ran but exited nonzero."*

### Scenario C — Non-blocking polling loop
Rewrite the waiting phase of Scenario A to use `waitpid(pid, &status, WNOHANG)` in a polling loop
instead of blocking `wait()`, printing a `"still running..."` message once per second for any task
not yet finished. *Trains: WNOHANG semantics, combining with `sleep()`/timestamps, avoiding busy-spinning without sleeping.*

### Scenario D — Pause/resume/terminate state machine
Design (just the state machine, then implement) a task table supporting `start`, `pause`,
`resume`, `terminate` commands read from stdin, where each task is a child running `sleep 100`.
- `pause <id>` → SIGSTOP, mark PAUSED, no-op if already PAUSED/DONE/TERMINATED/unknown id.
- `resume <id>` → SIGCONT, mark RUNNING, no-op if not currently PAUSED.
- `terminate <id>` → SIGKILL (or SIGTERM) + waitpid to reap, mark TERMINATED, no-op if already DONE/TERMINATED/unknown id.
- Periodically (or after every command) poll all RUNNING tasks with `waitpid(..., WNOHANG)` to catch ones that exited on their own, marking them DONE.
*Trains: the exact shape of Lab 2's `cmd_pause`/`cmd_resume`/`cmd_terminate`/`check_status` — the discipline is almost entirely "guard on current status before acting, and update status after."*

### Scenario E — Active-time-only sleep
Implement a `custom_active_sleep(int seconds)` that a **child** process calls: it should sleep for
`seconds` seconds of wall-clock time it is actually scheduled (not counting time it spent stopped
via SIGSTOP from the parent). Use `SIGCONT`'s default catchable behavior with a `volatile
sig_atomic_t` flag to detect a resume, and `clock_gettime` to accumulate only the running
intervals. Test it by: starting the child, letting it run 2s, pausing it for 5s, resuming it, and
confirming it takes 5 more seconds (not 3) to finish a 5s task.
*Trains: signal handler + flag pattern, the "reset last-checkpoint on resume" timing trick.*

### Scenario F — Exec variant that turns into another program
Extend Scenario D: add a task kind where, instead of `sleep`, the child execs into
`/bin/echo` with some fixed args. Make sure: (1) the exec failure path exits with a nonzero code
distinguishable from a normal echo exit, (2) once exec succeeds you never reach any code after it,
(3) it still participates correctly in pause/resume/terminate/reap (a SIGSTOP'd `/bin/echo` behaves
the same as a SIGSTOP'd sleep, from the parent's point of view — the child doesn't need to know or
care what signals are pausing it).
*Trains: worker()-style branching between task kinds, and realizing pause/resume/terminate work identically regardless of what the child execs into, since signals target the PID, not the program.*

### Scenario G — Zombie/cleanup audit
Take any of your programs above and deliberately break it (e.g. skip reaping a terminated task,
or forget to waitpid a normally-exited one), then run `ps` / watch for `<defunct>` entries to see
a zombie. Fix it. Then handle the "shutdown" case: when your program itself is about to exit, walk
the task table and make sure every non-DONE task is either force-killed+reaped (if paused/running)
or already reaped, so nothing is left behind.
*Trains: recognizing that "signal sent" is not "process gone" — a killed/terminated child MUST be waitpid'd or it zombies; this is the single most common eval bug.*

### Scenario H — Redirect + test-mode logging
Add a `--log-to-file` flag to any of the above that, if present, opens `out.txt` and `dup2`s it
onto `STDOUT_FILENO` **before** any forking happens, so both parent and child prints land in the
file. Verify by comparing to actual stdout output with the flag off.
*Trains: fd redirection ordering relative to fork (must redirect before fork so children inherit it), open() flags (`O_WRONLY|O_CREAT|O_TRUNC`).*

---

## 3. Quick self-check questions

- What's the difference in effect between `kill(pid, SIGSTOP)` and `kill(pid, SIGTERM)`? Which one can a process ignore?
- Why must `execvp`'s argv array end in `NULL`? What happens if it doesn't?
- If `fork()` returns `-1`, does a child process exist? What should you *not* do next?
- Why use `_exit()` instead of `exit()` right after `fork()` in the child?
- What does `WNOHANG` change about `waitpid`'s return value when the child hasn't exited yet?
- A process was `SIGKILL`'d. Is it fully gone from the system? What call finishes cleaning it up?
- Why does resuming a paused sleep task need to reset a "last checked" timestamp rather than just continuing to accumulate elapsed time?
- If you `dup2(fd, STDOUT_FILENO)` after forking N children instead of before, what breaks?
