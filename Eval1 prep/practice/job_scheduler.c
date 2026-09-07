/*
 * job_scheduler: run a DAG of build-style jobs with limited concurrency,
 * per-job timeouts, and retries.
 *
 * This combines everything from Lab01 (fork/execvp/wait, exit-status
 * checking) and Lab 2 (waitpid+WNOHANG polling, signals, active-time
 * tracking) with two things neither lab asked for:
 *   - a dependency graph between jobs (topological readiness), instead
 *     of a flat list or a fixed task table -- assume the input files are
 *     always acyclic, no need to detect or guard against cycles
 *   - a concurrency cap + timeout escalation (SIGTERM, then SIGKILL if
 *     it doesn't die) + a limited number of automatic retries
 *
 * ---------------------------------------------------------------------
 * INPUT FILE FORMAT (one directive per line; job/depend lines must name
 * jobs that are already defined by the time they're referenced):
 *
 *   maxjobs <N>        max jobs allowed to run at once (default 1)
 *   timeout <N>        seconds of active run time before a job is
 *                      killed for running too long (default: -1, i.e.
 *                      no timeout)
 *   retries <N>        how many extra attempts a FAILED job gets before
 *                      being marked permanently FAILED (default 0)
 *   job <name> <cmd> [args...]
 *                      define a job named <name> that runs <cmd> with
 *                      the given args via execvp
 *   depend <A> <B>     job A cannot start until job B has finished with
 *                      status DONE. (A depends on B.)
 *
 * Example:
 *   maxjobs 2
 *   timeout 5
 *   retries 1
 *   job A gcc -c a.c
 *   job B gcc -c b.c
 *   job C gcc -c c.c
 *   depend C A
 *   depend C B
 *
 * ---------------------------------------------------------------------
 * SEMANTICS
 *
 * Every job starts PENDING. The scheduler repeatedly:
 *   - reaps any RUNNING job that has exited (non-blocking), classifying
 *     it DONE (clean exit 0) or FAILED (nonzero exit, or killed by a
 *     signal)
 *   - kills (SIGTERM, then SIGKILL if it's still alive shortly after)
 *     any RUNNING job whose active run time has exceeded `timeout`, and
 *     classifies it FAILED
 *   - for any job that just became FAILED and still has attempts left
 *     (attempts <= retries), resets it back to PENDING instead of
 *     leaving it FAILED, so it gets tried again later
 *   - for any job that is now PERMANENTLY FAILED (out of retries) or
 *     SKIPPED, recursively marks every job that (directly or
 *     indirectly) depends on it as SKIPPED — those must never run
 *   - starts new PENDING jobs (fork + execvp) whenever all of that
 *     job's dependencies are DONE and fewer than `maxjobs` jobs are
 *     currently RUNNING
 *
 * The scheduler finishes when every job is DONE, permanently FAILED, or
 * SKIPPED.
 *
 * At the end, print one line per job in definition order:
 *   "<name>: DONE"  /  "<name>: FAILED"  /  "<name>: SKIPPED"
 * and return 0 if every job ended DONE, else 1.
 *
 * Build:
 *     gcc -Wall -Wextra -o job_scheduler job_scheduler.c
 * Run:
 *     ./job_scheduler jobs_basic.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_JOBS 64
#define MAX_ARGS 16
#define MAX_DEPS 16
#define NAME_LEN 32

typedef enum { PENDING, RUNNING, DONE, FAILED, SKIPPED } Status;

typedef struct {
    char name[NAME_LEN];
    char *argv[MAX_ARGS + 1]; /* NULL-terminated, ready for execvp */
    int deps[MAX_DEPS];       /* indices of jobs that must be DONE first */
    int ndeps;
    Status status;
    int attempts; /* how many times this job has been forked so far */
    pid_t pid;    /* only meaningful while RUNNING */
    double start; /* set when the current attempt was forked */
} Job;

Job jobs[MAX_JOBS];
int njobs = 0;
int maxjobs = 1;
int timeout_secs = -1; /* -1 = no timeout */
int max_retries = 0;

double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int find_job(const char *name) {
    for (int i = 0; i < njobs; i++)
        if (!strcmp(jobs[i].name, name))
            return i;
    return -1;
}

/* ---------------------------------------------------------------------
 * GIVEN: parses the input file into the globals above. Not the point of
 * this exercise -- don't spend time here, just know what it leaves you
 * with: jobs[0..njobs-1] fully populated (name, argv, deps, ndeps,
 * status = PENDING), plus maxjobs / timeout_secs / max_retries set.
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

        if (!strcmp(tok, "maxjobs")) {
            maxjobs = atoi(strtok(NULL, " \t\r\n"));
        } else if (!strcmp(tok, "timeout")) {
            timeout_secs = atoi(strtok(NULL, " \t\r\n"));
        } else if (!strcmp(tok, "retries")) {
            max_retries = atoi(strtok(NULL, " \t\r\n"));
        } else if (!strcmp(tok, "job")) {
            Job *j = &jobs[njobs];
            memset(j, 0, sizeof(*j));
            strncpy(j->name, strtok(NULL, " \t\r\n"), NAME_LEN - 1);
            int k = 0;
            char *w;
            while ((w = strtok(NULL, " \t\r\n")) && k < MAX_ARGS)
                j->argv[k++] = strdup(w);
            j->argv[k] = NULL;
            j->status = PENDING;
            njobs++;
        } else if (!strcmp(tok, "depend")) {
            char *a = strtok(NULL, " \t\r\n");
            char *b = strtok(NULL, " \t\r\n");
            int ia = find_job(a), ib = find_job(b);
            if (ia == -1 || ib == -1) {
                fprintf(stderr, "unknown job in depend line\n");
                exit(1);
            }
            jobs[ia].deps[jobs[ia].ndeps++] = ib;
        }
    }
    fclose(f);
}

void print_report(void) {
    const char *names[] = {"PENDING", "RUNNING", "DONE", "FAILED", "SKIPPED"};
    for (int i = 0; i < njobs; i++)
        printf("%s: %s\n", jobs[i].name, names[jobs[i].status]);
}

/* =======================================================================
 * TODO 1 -- dependency readiness
 *
 * Return 1 if every dependency of jobs[i] currently has status DONE
 * (i.e. this job is allowed to start), else 0.
 * ===================================================================== */
int deps_satisfied(int i) {
    // YOUR CODE HERE
    return 0;
}

/* =======================================================================
 * TODO 2 -- failure propagation
 *
 * Some job has just become permanently FAILED or SKIPPED. Every job
 * that depends on it -- directly, or transitively through other jobs --
 * must be marked SKIPPED, unless it's already DONE/FAILED/SKIPPED (only
 * a PENDING job should ever change here). This has to cascade: marking
 * some job J as SKIPPED can cause another job that depends on J to also
 * need skipping.
 *
 * Hint: a simple (not maximally efficient) approach is fine -- e.g. loop
 * over all jobs looking for any PENDING job whose deps include a
 * FAILED/SKIPPED job, mark it SKIPPED, and repeat full passes until one
 * pass makes no changes.
 * ===================================================================== */
void propagate_skip(void) {
    // YOUR CODE HERE
}

/* =======================================================================
 * TODO 3 -- start a job
 *
 * Fork a child for jobs[i], have it execvp into jobs[i].argv, and
 * update the job's bookkeeping in the parent (pid, start time,
 * status = RUNNING, attempts++).
 *
 * Hints:
 *  - argv is already built and NULL-terminated by parse_file -- just
 *    hand it to execvp.
 *  - If exec fails inside the child, don't let control fall through
 *    into code that assumes it's still the parent -- perror + a
 *    nonzero _exit.
 *  - If fork() itself fails, there is no child. Think about what that
 *    should do to the job's status -- leaving it PENDING forever isn't
 *    right, since nothing will ever change it.
 * ===================================================================== */
void start_job(int i) {
    // YOUR CODE HERE
}

/* =======================================================================
 * TODO 4 -- poll running jobs
 *
 * For every job currently RUNNING:
 *  (a) Check (non-blocking!) whether it has exited. If so, classify it
 *      DONE (WIFEXITED && exit code 0) or FAILED (anything else,
 *      including WIFSIGNALED), and clear its pid.
 *  (b) If it's still running, and timeout_secs >= 0, and
 *      (get_time() - jobs[i].start) has exceeded timeout_secs,
 *      escalate: send SIGTERM, briefly check again, and if it's still
 *      alive send SIGKILL -- then reap it (blocking waitpid is fine at
 *      that point, it's not going to keep running) and classify it
 *      FAILED.
 *
 * Right after classifying a job FAILED here, compare its attempts
 * against max_retries: if it still has attempts left, reset it to
 * PENDING instead of leaving it FAILED, so the scheduler tries it
 * again later -- otherwise leave it permanently FAILED.
 *
 * Hint: think carefully about *when* attempts should be incremented
 * (TODO 4, or here?) vs. *when* you decide "no attempts left" -- get
 * this wrong and you'll either retry forever or never retry at all.
 * ===================================================================== */
void poll_running(void) {
    // YOUR CODE HERE
}

/* =======================================================================
 * TODO 5 -- the scheduler loop
 *
 * Tie it all together. Loop until every job is DONE, permanently
 * FAILED, or SKIPPED:
 *   - poll_running() to reap/timeout anything currently running
 *   - propagate_skip() to cascade any newly-permanent FAILED / SKIPPED
 *     status onto dependents
 *   - while fewer than maxjobs jobs are RUNNING, look for a PENDING job
 *     with deps_satisfied() and start_job() it; stop looking once none
 *     are eligible right now
 *   - avoid busy-spinning the CPU: a short sleep between iterations is
 *     fine, since jobs take real wall-clock time
 * Then print_report(), and return 0 if every job ended DONE, else 1.
 * ===================================================================== */
int run_scheduler(void) {
    // YOUR CODE HERE
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <jobs_file>\n", argv[0]);
        return 1;
    }
    parse_file(argv[1]);
    return run_scheduler();
}
