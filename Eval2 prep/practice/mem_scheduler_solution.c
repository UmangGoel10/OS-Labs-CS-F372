/*
 * mem_scheduler: run a list of jobs (fork + execvp, like Lab01/Lab 2) that
 * each must first reserve a slice of ONE shared buddy-allocated arena
 * (like Lab05's buddy.c) before they're allowed to start. A job's memory
 * is returned to the pool -- freed and coalesced -- the moment its process
 * exits, possibly letting a job that didn't fit before start now.
 *
 * This combines fork/execvp/waitpid+WNOHANG polling and exit-status
 * classification (Lab01/Lab 2) with the exact split/coalesce buddy logic
 * from Lab05, plus one thing neither lab asked for: memory as a scarce
 * resource that gates *when* a job is allowed to run, not just what it
 * runs. The allocator itself lives only in this one process -- jobs don't
 * touch it themselves, they just consume a reservation from it for as
 * long as they're alive.
 *
 * ---------------------------------------------------------------------
 * INPUT FILE FORMAT (one directive per line):
 *
 *   arena <bytes>       total arena size, must be a power of two (default 1024)
 *   minblock <bytes>    smallest block size, must divide arena evenly,
 *                       itself a power of two (default 64)
 *   job <name> <mem_bytes> <cmd> [args...]
 *                       job named <name> needs <mem_bytes> from the arena
 *                       before it may fork+execvp <cmd> with the given args
 *
 * Example:
 *   arena 1024
 *   minblock 64
 *   job A 300 sleep 1
 *   job B 300 sleep 1
 *   job C 500 sleep 1
 *
 * ---------------------------------------------------------------------
 * SEMANTICS
 *
 * Every job starts PENDING. No dependency graph this time -- any PENDING
 * job is eligible to start the moment enough contiguous arena space is
 * free, in definition order. The scheduler repeatedly:
 *   - reaps any RUNNING job that has exited (non-blocking), classifies it
 *     DONE (clean exit 0) or FAILED (nonzero exit, or killed by a signal),
 *     and immediately frees its reserved block back to the arena
 *   - walks PENDING jobs in definition order; for each one, tries to
 *     reserve round_size(mem_bytes) bytes from the arena and, if that
 *     succeeds, forks+execvp's it and marks it RUNNING -- if a job's
 *     request doesn't currently fit, it stays PENDING and the scheduler
 *     moves on to try the *next* PENDING job (a smaller later job may fit
 *     even when an earlier bigger one doesn't right now)
 *
 * The scheduler finishes when every job is DONE or FAILED.
 *
 * At the end, print one line per job in definition order:
 *   "<name>: DONE"  /  "<name>: FAILED"
 * and return 0 if every job ended DONE, else 1.
 *
 * Build:
 *     gcc -Wall -Wextra -o mem_scheduler mem_scheduler.c
 * Run:
 *     ./mem_scheduler mem_basic.txt
 */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_JOBS 64
#define MAX_ARGS 16
#define NAME_LEN 32
#define MAX_SLOTS 256

typedef enum { PENDING, RUNNING, DONE, FAILED } Status;

typedef struct {
    char name[NAME_LEN];
    char *argv[MAX_ARGS + 1]; /* NULL-terminated, ready for execvp */
    size_t mem_bytes;         /* raw request from the input file */
    Status status;
    pid_t pid;   /* only meaningful while RUNNING */
    void *block; /* pointer returned by my_malloc, only while RUNNING */
} Job;

Job jobs[MAX_JOBS];
int njobs = 0;

/* ---------------------------------------------------------------------
 * GIVEN: the buddy allocator, unchanged from Lab05's buddy.c. Only ONE
 * process (this scheduler) ever touches it -- jobs never call my_malloc
 * or my_free themselves, they just get reserved a block by the scheduler
 * for the duration of their run.
 * ------------------------------------------------------------------ */
size_t ARENA_SIZE = 1024;
size_t MIN_BLOCK = 64;
int SLOT_COUNT;

unsigned char arena_storage[65536]; /* backing storage, sized generously */
unsigned char *arena = arena_storage;

typedef struct {
    size_t size;
    int free;
} Block;
Block blocks[MAX_SLOTS];

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

/* ---------------------------------------------------------------------
 * GIVEN: parses the input file into the globals above. Not the point of
 * this exercise -- don't spend time here, just know what it leaves you
 * with: ARENA_SIZE/MIN_BLOCK/SLOT_COUNT set, blocks[0] initialized to one
 * free block spanning the whole arena, and jobs[0..njobs-1] fully
 * populated (name, argv, mem_bytes, status = PENDING).
 * ------------------------------------------------------------------ */
void parse_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("fopen");
        exit(1);
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *tok = strtok(line, " \t\r\n");
        if (!tok)
            continue;

        if (!strcmp(tok, "arena")) {
            ARENA_SIZE = strtoul(strtok(NULL, " \t\r\n"), NULL, 10);
        } else if (!strcmp(tok, "minblock")) {
            MIN_BLOCK = strtoul(strtok(NULL, " \t\r\n"), NULL, 10);
        } else if (!strcmp(tok, "job")) {
            Job *j = &jobs[njobs];
            memset(j, 0, sizeof(*j));
            strncpy(j->name, strtok(NULL, " \t\r\n"), NAME_LEN - 1);
            j->mem_bytes = strtoul(strtok(NULL, " \t\r\n"), NULL, 10);
            int k = 0;
            char *w;
            while ((w = strtok(NULL, " \t\r\n")) && k < MAX_ARGS)
                j->argv[k++] = strdup(w);
            j->argv[k] = NULL;
            j->status = PENDING;
            njobs++;
        }
    }
    fclose(f);

    SLOT_COUNT = (int)(ARENA_SIZE / MIN_BLOCK);
    blocks[0] = (Block){ARENA_SIZE, 1};
}

void print_report(void) {
    const char *names[] = {"PENDING", "RUNNING", "DONE", "FAILED"};
    for (int i = 0; i < njobs; i++)
        printf("%s: %s\n", jobs[i].name, names[jobs[i].status]);
}

/* =======================================================================
 * TODO 1 -- try to start one PENDING job
 *
 * jobs[i] is PENDING. Attempt to reserve its memory and launch it:
 *   - call my_malloc(jobs[i].mem_bytes)
 *   - if it returns NULL, there isn't a free block big enough for this
 *     job RIGHT NOW -- leave the job PENDING and return 0 (the scheduler
 *     should go try a different PENDING job, not give up entirely)
 *   - if it succeeds, fork(). In the child, execvp(jobs[i].argv[0],
 *     jobs[i].argv); if exec fails, perror + a nonzero _exit (don't let
 *     control fall through into parent-only code)
 *   - in the parent: if fork() itself failed (pid < 0), you already
 *     reserved a block that no process will ever use and free -- think
 *     about what call undoes that reservation before you return 0
 *   - in the parent on a successful fork: store the pid and the block
 *     pointer on jobs[i], set status = RUNNING, return 1
 * ===================================================================== */
int try_start_job(int i) {
    void *block = my_malloc(jobs[i].mem_bytes);
    if (block == NULL)
        return 0;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        my_free(block); /* undo the reservation -- nobody will ever use it */
        return 0;
    }
    if (pid == 0) {
        execvp(jobs[i].argv[0], jobs[i].argv);
        perror("execvp");
        _exit(127);
    }

    jobs[i].pid = pid;
    jobs[i].block = block;
    jobs[i].status = RUNNING;
    return 1;
}

/* =======================================================================
 * TODO 2 -- poll running jobs
 *
 * For every job currently RUNNING, check (non-blocking -- WNOHANG)
 * whether it has exited. If it has:
 *   - free its reserved block back into the arena (this is what lets a
 *     PENDING job that didn't fit earlier fit later)
 *   - classify it DONE (WIFEXITED && exit code 0) or FAILED (anything
 *     else, including WIFSIGNALED)
 *   - clear its pid/block bookkeeping
 * If it's still running, leave it alone.
 * ===================================================================== */
void poll_running(void) {
    for (int i = 0; i < njobs; i++) {
        if (jobs[i].status != RUNNING)
            continue;

        int status;
        pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);
        if (result == 0)
            continue; /* still running */

        my_free(jobs[i].block);
        jobs[i].block = NULL;
        jobs[i].pid = 0;
        jobs[i].status =
            (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? DONE : FAILED;
    }
}

/* =======================================================================
 * TODO 3 -- the scheduler loop
 *
 * Tie it together. Loop until every job is DONE or FAILED:
 *   - poll_running() to reap anything that finished and free its memory
 *   - walk PENDING jobs in definition order, calling try_start_job() on
 *     each one that's still PENDING -- do NOT stop at the first one that
 *     fails to start; a later, smaller job might still fit (see the
 *     "think about" note below before you decide the loop's shape)
 *   - avoid busy-spinning the CPU: a short sleep (e.g. usleep(10000))
 *     between iterations is fine, since jobs take real wall-clock time
 * Then print_report(), and return 0 if every job ended DONE, else 1.
 *
 * --- think about ---
 * Walk through mem_contention.txt by hand before running it: which jobs
 * can run simultaneously given the arena size, and in what order do you
 * expect DONE lines to print? Then mem_fragmentation.txt: a job can be
 * stuck PENDING even while the arena has *enough total free bytes*,
 * because the free bytes aren't in one contiguous block. Find the exact
 * moment that happens and explain it in terms of buddy vs non-buddy
 * adjacent free blocks (buddy.c's fragmentation.txt example, section 1
 * of eval_prep.md).
 * ===================================================================== */
int run_scheduler(void) {
    int finished = 0;
    while (finished < njobs) {
        poll_running();

        for (int i = 0; i < njobs; i++)
            if (jobs[i].status == PENDING)
                try_start_job(i);

        finished = 0;
        for (int i = 0; i < njobs; i++)
            if (jobs[i].status == DONE || jobs[i].status == FAILED)
                finished++;

        if (finished < njobs)
            usleep(10000);
    }

    print_report();

    for (int i = 0; i < njobs; i++)
        if (jobs[i].status != DONE)
            return 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <jobs_file>\n", argv[0]);
        return 1;
    }
    parse_file(argv[1]);
    return run_scheduler();
}
