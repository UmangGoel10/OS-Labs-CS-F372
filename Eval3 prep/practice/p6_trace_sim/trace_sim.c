/*
 * P6 -- trace-driven demand-paging simulator (write the core yourself)
 *
 *   gcc -Wall -Wextra -std=c99 trace_sim.c -o sim && ./sim tests/01_textbook_fifo.txt
 *   or:  cd practice && ./run.sh p6_trace_sim        (add `sol` for solution.c)
 *
 * No framework here: you simulate physical frames, page replacement and the
 * statistics yourself.  Parsing, main() and the final report are PROVIDED;
 * you write find_frame / find_free_frame / next_use / choose_victim / do_access.
 *
 * ---------------------------------------------------------------------
 * INPUT (one directive per line; lines starting with '#' are comments)
 *
 *   frames <n>                  physical frames, 1..16            (default 3)
 *   policy FIFO|LRU|CLOCK|OPT   replacement policy                (default FIFO)
 *   pagesize <bytes>            virtual page size                 (default 64)
 *   tlb <n>                     STRETCH: n-entry TLB, 0 = none    (default 0)
 *   R <addr>                    read  the byte at virtual address <addr>
 *   W <addr>                    write the byte at virtual address <addr>
 *
 *   vpn = addr / pagesize        offset = addr % pagesize
 *
 * ---------------------------------------------------------------------
 * SEMANTICS
 *
 *   - Access numbers are 1-based:  the k-th R/W line is access #k, and "time"
 *     below means that number.
 *   - A page is HIT if some frame already holds its vpn, otherwise FAULT.
 *   - On a fault use the LOWEST-numbered free frame if there is one.
 *     If none, choose a victim FRAME with the policy and reuse that frame.
 *   - A loaded page starts clean.  A write (hit OR the faulting write itself)
 *     makes it dirty.  Evicting a dirty page counts as a write-back.
 *   - Per-frame bookkeeping you will need:
 *         load_time   time when the page was loaded into the frame
 *         last_use    time of the most recent access (hit or load)
 *         ref         reference bit: set on load and on every hit
 *
 *   FIFO   evict the frame with the smallest load_time.
 *   LRU    evict the frame with the smallest last_use.
 *   CLOCK  a hand over frames 0..n-1 (starts at 0, persists, is NOT moved
 *          when a free frame is used).  Loop: ref==1 -> clear it, hand++;
 *          ref==0 -> that frame is the victim, leave hand at victim+1.
 *   OPT    (Belady) evict the resident page whose NEXT use lies farthest in the
 *          future; a page never used again is farthest of all; ties -> lowest
 *          frame index.  The whole trace is already parsed into op[]/addr[].
 *
 * ---------------------------------------------------------------------
 * OUTPUT -- exactly this, one line per access
 *
 *   #<k> <R|W> addr=<addr> vpn=<vpn> off=<off>: HIT frame=<f>
 *   #<k> <R|W> addr=<addr> vpn=<vpn> off=<off>: FAULT load frame=<f>
 *   #<k> <R|W> addr=<addr> vpn=<vpn> off=<off>: FAULT evict vpn=<old> frame=<f>
 *   #<k> <R|W> addr=<addr> vpn=<vpn> off=<off>: FAULT evict vpn=<old> frame=<f> (dirty, write-back)
 *
 * (main() then prints the FINAL and SUMMARY lines for you.)
 *
 * STRETCH (tests 08, 09 fail until you do this): with `tlb <n>` > 0 add a fully
 * associative n-entry TLB, LRU replacement, checked BEFORE the page table.
 *   - each access line gets  " tlb=HIT" or " tlb=MISS"  right after off=<off>
 *     (before the colon):   #1 R addr=0 vpn=0 off=0 tlb=MISS: FAULT load frame=0
 *   - a TLB miss installs vpn->frame afterwards (after any fault is resolved)
 *   - when a page is evicted, its TLB entry MUST be invalidated
 *   - the SUMMARY line gains  " tlb=<n> tlb_hits=<h> tlb_misses=<m>"
 * ---------------------------------------------------------------------
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES   16
#define MAX_TLB      16
#define MAX_ACCESSES 4096

typedef enum { POL_FIFO, POL_LRU, POL_CLOCK, POL_OPT } Policy;

typedef struct {
    int  vpn;        /* -1 = free frame */
    int  dirty;
    int  ref;
    long load_time;
    long last_use;
} Frame;

static Frame frames[MAX_FRAMES];
static int   nframes = 3;
static int   tlb_n = 0;                 /* parsed for you; only the STRETCH uses it */
static long  page_size = 64;
static Policy policy = POL_FIFO;
static const char *policy_name = "FIFO";

static char op[MAX_ACCESSES];           /* 'R' or 'W' */
static long addr[MAX_ACCESSES];
static int  naccess = 0;

static int  hand = 0;                   /* CLOCK hand (a frame index) */
static long hits, faults, evictions, writebacks, tlb_hits, tlb_misses;

/* ------------------------- TODO: write these -------------------------
 * (The -Wunused warnings you see before you start disappear once do_access
 *  calls these helpers.) */

/* Frame index that currently holds vpn, or -1. */
static int find_frame(int vpn) {
    (void)vpn;
    return -1;
}

/* Lowest-numbered free frame, or -1 if every frame is occupied. */
static int find_free_frame(void) {
    return -1;
}

/* Distance (in accesses) from access index i to the next use of vpn.
 * 0-based i = the access being served now; look at i+1 .. naccess-1.
 * Return naccess + 1 ("infinity") if vpn is never used again. */
static int next_use(int vpn, int i) {
    (void)vpn; (void)i;
    return 0;
}

/* Every frame is occupied: return the index of the frame to reuse.
 * i = 0-based index of the access being served (OPT needs it). */
static int choose_victim(int i) {
    (void)i;
    return 0;
}

/* Serve access index i (0-based): translate, hit/fault, evict, update stats,
 * print the access line.  Must update: hits, faults, evictions, writebacks,
 * and every touched frame's load_time / last_use / ref / dirty. */
static void do_access(int i) {
    (void)i;
}

/* ------------------------- PROVIDED: do not edit ------------------------- */

static void parse(FILE *in) {
    char line[256];
    while (fgets(line, sizeof line, in)) {
        char word[64], arg[64];
        if (line[0] == '#' || sscanf(line, "%63s %63s", word, arg) < 2)
            continue;

        if (strcmp(word, "frames") == 0) {
            nframes = atoi(arg);
        } else if (strcmp(word, "pagesize") == 0) {
            page_size = atol(arg);
        } else if (strcmp(word, "tlb") == 0) {
            tlb_n = atoi(arg);
        } else if (strcmp(word, "policy") == 0) {
            for (char *p = arg; *p; p++)
                *p = (char)toupper((unsigned char)*p);
            if (strcmp(arg, "FIFO") == 0) { policy = POL_FIFO; policy_name = "FIFO"; }
            else if (strcmp(arg, "LRU") == 0) { policy = POL_LRU; policy_name = "LRU"; }
            else if (strcmp(arg, "CLOCK") == 0) { policy = POL_CLOCK; policy_name = "CLOCK"; }
            else if (strcmp(arg, "OPT") == 0) { policy = POL_OPT; policy_name = "OPT"; }
            else { fprintf(stderr, "unknown policy %s\n", arg); exit(1); }
        } else if ((strcmp(word, "R") == 0 || strcmp(word, "W") == 0) &&
                   naccess < MAX_ACCESSES) {
            op[naccess] = word[0];
            addr[naccess] = atol(arg);
            naccess++;
        } else {
            fprintf(stderr, "bad line: %s", line);
            exit(1);
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <trace.txt>\n", argv[0]);
        return 1;
    }
    FILE *in = fopen(argv[1], "r");
    if (!in) { perror("fopen"); return 1; }
    parse(in);
    fclose(in);

    if (nframes < 1 || nframes > MAX_FRAMES || tlb_n < 0 || tlb_n > MAX_TLB ||
        page_size < 1) {
        fprintf(stderr, "bad configuration\n");
        return 1;
    }

    for (int f = 0; f < nframes; f++)
        frames[f].vpn = -1;

    for (int i = 0; i < naccess; i++)
        do_access(i);

    printf("FINAL");
    for (int f = 0; f < nframes; f++) {
        if (frames[f].vpn == -1)
            printf(" frame%d=-", f);
        else
            printf(" frame%d=%d", f, frames[f].vpn);
    }
    printf("\n");
    printf("SUMMARY policy=%s frames=%d accesses=%d hits=%ld faults=%ld "
           "evictions=%ld writebacks=%ld",
           policy_name, nframes, naccess, hits, faults, evictions, writebacks);
    if (tlb_n > 0)
        printf(" tlb=%d tlb_hits=%ld tlb_misses=%ld", tlb_n, tlb_hits, tlb_misses);
    printf("\n");
    return 0;
}
