/*
 * P6 -- trace-driven demand-paging simulator (reference solution, incl. TLB)
 *
 *   gcc -Wall -Wextra -std=c99 solution.c -o sim && ./sim tests/01_textbook_fifo.txt
 *
 * See trace_sim.c for the full input/output specification.
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
    long load_time;  /* 1-based access index at which it was loaded */
    long last_use;   /* 1-based access index of the latest use */
} Frame;

typedef struct {
    int  vpn;        /* -1 = invalid entry */
    int  frame;
    long last_use;
} TlbEntry;

static Frame    frames[MAX_FRAMES];
static TlbEntry tlb[MAX_TLB];
static int      nframes = 3;
static int      tlb_n = 0;
static long     page_size = 64;
static Policy   policy = POL_FIFO;
static const char *policy_name = "FIFO";

static char op[MAX_ACCESSES];
static long addr[MAX_ACCESSES];
static int  naccess = 0;

static int  hand = 0;                       /* CLOCK hand (a frame index) */
static long hits, faults, evictions, writebacks, tlb_hits, tlb_misses;

static int vpn_of(int i) { return (int)(addr[i] / page_size); }

static int find_frame(int vpn) {
    for (int f = 0; f < nframes; f++)
        if (frames[f].vpn == vpn)
            return f;
    return -1;
}

static int find_free_frame(void) {
    for (int f = 0; f < nframes; f++)
        if (frames[f].vpn == -1)
            return f;
    return -1;
}

/* Distance from access index i to the next use of vpn; "infinity" if never. */
static int next_use(int vpn, int i) {
    for (int j = i + 1; j < naccess; j++)
        if (vpn_of(j) == vpn)
            return j - i;
    return naccess + 1;
}

/* All frames are occupied.  Pick the frame to reuse for access index i. */
static int choose_victim(int i) {
    int best = 0;

    switch (policy) {
    case POL_FIFO:
        for (int f = 1; f < nframes; f++)
            if (frames[f].load_time < frames[best].load_time)
                best = f;
        return best;

    case POL_LRU:
        for (int f = 1; f < nframes; f++)
            if (frames[f].last_use < frames[best].last_use)
                best = f;
        return best;

    case POL_CLOCK:
        for (;;) {
            int f = hand;
            hand = (hand + 1) % nframes;
            if (frames[f].ref)
                frames[f].ref = 0;          /* second chance */
            else
                return f;
        }

    case POL_OPT: {
        int best_dist = next_use(frames[0].vpn, i);
        for (int f = 1; f < nframes; f++) {
            int d = next_use(frames[f].vpn, i);
            if (d > best_dist) {            /* strict: ties keep lowest frame */
                best_dist = d;
                best = f;
            }
        }
        return best;
    }
    }
    return 0;
}

static int tlb_find(int vpn) {
    for (int t = 0; t < tlb_n; t++)
        if (tlb[t].vpn == vpn)
            return t;
    return -1;
}

static void tlb_insert(int vpn, int frame, long now) {
    int slot = -1;
    for (int t = 0; t < tlb_n; t++)
        if (tlb[t].vpn == -1) { slot = t; break; }
    if (slot == -1) {                        /* full: replace the LRU entry */
        slot = 0;
        for (int t = 1; t < tlb_n; t++)
            if (tlb[t].last_use < tlb[slot].last_use)
                slot = t;
    }
    tlb[slot].vpn = vpn;
    tlb[slot].frame = frame;
    tlb[slot].last_use = now;
}

static void do_access(int i) {
    long now = i + 1;
    int  vpn = vpn_of(i);
    long off = addr[i] % page_size;
    int  is_write = (op[i] == 'W');

    printf("#%ld %c addr=%ld vpn=%d off=%ld", now, op[i], addr[i], vpn, off);

    /* TLB first (only when enabled) */
    int tlb_hit = 0;
    if (tlb_n > 0) {
        int t = tlb_find(vpn);
        if (t >= 0) {
            tlb_hit = 1;
            tlb_hits++;
            tlb[t].last_use = now;
        } else {
            tlb_misses++;
        }
        printf(" tlb=%s", tlb_hit ? "HIT" : "MISS");
    }

    int f = find_frame(vpn);
    if (f >= 0) {
        hits++;
        printf(": HIT frame=%d\n", f);
    } else {
        faults++;
        f = find_free_frame();
        if (f >= 0) {
            printf(": FAULT load frame=%d\n", f);
        } else {
            f = choose_victim(i);
            int old = frames[f].vpn;
            int was_dirty = frames[f].dirty;
            evictions++;
            if (was_dirty)
                writebacks++;
            printf(": FAULT evict vpn=%d frame=%d%s\n", old, f,
                   was_dirty ? " (dirty, write-back)" : "");

            /* a mapping that no longer exists must not stay cached */
            if (tlb_n > 0) {
                int t = tlb_find(old);
                if (t >= 0)
                    tlb[t].vpn = -1;
            }
        }
        frames[f].vpn = vpn;
        frames[f].dirty = 0;
        frames[f].ref = 0;
        frames[f].load_time = now;
    }

    /* every access (hit or just-loaded) updates use info */
    frames[f].last_use = now;
    frames[f].ref = 1;
    if (is_write)
        frames[f].dirty = 1;

    if (tlb_n > 0 && !tlb_hit)
        tlb_insert(vpn, f, now);
}

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
    for (int t = 0; t < tlb_n; t++)
        tlb[t].vpn = -1;

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
