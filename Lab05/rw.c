/* TA reference solution: writer threads with mutex, file-backed MAP_SHARED mmap. */
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define SHARED_FILE  "shared.bin"
#define NUM_WRITERS  3
#define RECORD_SIZE  64
#define REGION_SIZE  ((1 + NUM_WRITERS) * RECORD_SIZE)

typedef struct {
    uint32_t records_written;
    // Unused filler so the header still occupies one full RECORD_SIZE-byte
    // slot, keeping every slot (header and records alike) the same size and
    // Record data at fixed, predictable offsets in the file.
    char     _pad[RECORD_SIZE - sizeof(uint32_t)];
} Header;

typedef struct {
    int  writer_id;
    char message[RECORD_SIZE - sizeof(int)];
} Record;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *region;

static void *writer_thread(void *arg) {
    int id = *(int *)arg;

    Header *h = (Header *)region;
    Record *r = (Record *)(region + RECORD_SIZE + id * RECORD_SIZE);

    pthread_mutex_lock(&lock);
    r->writer_id = id;
    snprintf(r->message, sizeof(r->message), "hello from writer %d", id);
    // Publish only after the record is complete: the reader treats the
    // counter as "this many records are ready".
    h->records_written++;
    pthread_mutex_unlock(&lock);

    msync(region, REGION_SIZE, MS_SYNC);
    return NULL;
}

// Optional per-writer stagger for the live demo, in milliseconds, set via
// the RW_DEMO_STAGGER_MS environment variable. Defaults to 0 so grading
// runs at full speed; the demo target sets it so writes land a visible
// moment apart, so a concurrently-running `./rw read` shows them arriving
// one at a time instead of all at once.
static useconds_t demo_stagger_us(void) {
    const char *v = getenv("RW_DEMO_STAGGER_MS");
    if (!v) return 0;
    long ms = atol(v);
    return ms > 0 ? (useconds_t)(ms * 1000) : 0;
}

void run_writers(void) {
    int fd = open(SHARED_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) { perror("open"); return; }
    if (ftruncate(fd, REGION_SIZE) == -1) { perror("ftruncate"); close(fd); return; }

    region = mmap(NULL, REGION_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (region == MAP_FAILED) { perror("mmap"); return; }

    pthread_t threads[NUM_WRITERS];
    int ids[NUM_WRITERS];
    useconds_t stagger = demo_stagger_us();
    for (int i = 0; i < NUM_WRITERS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, writer_thread, &ids[i]);
        if (stagger) usleep(stagger);
    }
    for (int i = 0; i < NUM_WRITERS; i++)
        pthread_join(threads[i], NULL);

    munmap(region, REGION_SIZE);
    region = NULL;
}

// Polls the shared region rather than assuming the writers have finished.
// Each pass reads records_written FIRST, then scans the record slots and
// prints every not-yet-printed record whose message is non-empty. The loop
// ends after a pass whose count was NUM_WRITERS. Reading the count before the
// scan matters: writers bump it only after their record is complete, so a
// scan that starts after seeing NUM_WRITERS must find every record. (Reading
// it after the scan could miss a writer that finished in between.)
// When called after run_writers() has already completed (the autograder's
// case), the first pass finds everything ready and returns with no sleep.
void run_reader(void) {
    int fd = open(SHARED_FILE, O_RDONLY);
    if (fd == -1) { perror("open"); return; }

    unsigned char *map = mmap(NULL, REGION_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) { perror("mmap"); return; }

    Header *h = (Header *)map;
    int printed_slot[NUM_WRITERS] = {0};
    uint32_t count = 0;

    while (count < NUM_WRITERS) {
        count = h->records_written;
        for (int i = 0; i < NUM_WRITERS; i++) {
            if (printed_slot[i]) continue;
            Record *r = (Record *)(map + RECORD_SIZE + i * RECORD_SIZE);
            if (r->message[0] != '\0') {
                printf("writer %d: %s\n", r->writer_id, r->message);
                fflush(stdout);
                printed_slot[i] = 1;
            }
        }
        if (count < NUM_WRITERS) usleep(50000);
    }

    printf("records_written: %u\n", count);
    munmap(map, REGION_SIZE);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s write|read\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "write") == 0) {
        run_writers();
    } else if (strcmp(argv[1], "read") == 0) {
        run_reader();
    } else {
        fprintf(stderr, "Unknown mode: %s\n", argv[1]);
        return 1;
    }
    return 0;
}
