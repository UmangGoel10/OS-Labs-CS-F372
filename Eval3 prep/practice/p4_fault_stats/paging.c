#include "paging_sim.h"

/*
 * P4 -- Fault accounting
 *
 * TWIST: Lab 7's README distinguishes MINOR faults (resolved without disk I/O,
 * e.g. zero-filling a brand-new anonymous page) from MAJOR faults (the
 * contents must be fetched from swap or from the backing file) -- but the
 * original code never counts anything.  Add the bookkeeping and implement
 * print_fault_stats(), which the new `STATS` driver command calls.
 *
 * Count these events (cumulative since program start, never reset):
 *
 *   in load_page():
 *     anonymous, swap_slot == -1  (zero-fill)        -> minor
 *     anonymous, swap_slot != -1  (swap-in)          -> major, swap_in
 *     file-backed                 (file read)        -> major, file_read
 *   in evict_page():
 *     anonymous (always written to swap)             -> swap_out
 *     file-backed, dirty (write-back)                -> file_writeback
 *     file-backed, clean (discarded, NO I/O)         -> clean_discard
 *
 * print_fault_stats() must print EXACTLY one line, in this format:
 *
 *   STATS faults=<minor+major> minor=<m> major=<M> swap_in=<n> swap_out=<n> file_read=<n> file_writeback=<n> clean_discard=<n>
 *
 * Policy and I/O behaviour are otherwise unchanged from Lab 7.
 *
 * Run:   cd practice && ./run.sh p4_fault_stats        (add `sol` for solution.c)
 */

/* TODO 1: add counters here. */

/* TODO 2: print the STATS line. */
void print_fault_stats(void) {
    printf("STATS faults=0 minor=0 major=0 swap_in=0 swap_out=0 "
           "file_read=0 file_writeback=0 clean_discard=0\n");
}

static int category(PageInfo p) {
    if (p.type == PAGE_ANON)
        return p.dirty ? 3 : 1;
    return p.dirty ? 4 : 2;
}

/* PROVIDED (unchanged from Lab 7). */
int select_victim_page(void) {
    int best = -1;
    int best_cat = 0;
    uint64_t best_time = 0;

    for (int i = 0; i < sim_page_count(); i++) {
        PageInfo p = sim_get_page_info(i);
        if (!p.resident)
            continue;

        int cat = category(p);
        if (best == -1 || cat < best_cat ||
            (cat == best_cat && p.last_access_time < best_time)) {
            best = i;
            best_cat = cat;
            best_time = p.last_access_time;
        }
    }
    return best;
}

/* TODO 3: Lab 7 eviction -- add the eviction counters. */
void evict_page(int victim_page_id) {
    PageInfo p = sim_get_page_info(victim_page_id);

    if (p.type == PAGE_ANON) {
        int slot = p.swap_slot;
        if (slot == -1)
            slot = sim_alloc_swap_slot();
        sim_write_to_swap(p.frame_id, slot);
        sim_set_swap_slot(victim_page_id, slot);
    } else if (p.dirty) {
        sim_write_to_file(p.frame_id, p.file_id, p.file_offset);
    }

    sim_mark_page_evicted(victim_page_id);
}

/* TODO 4: Lab 7 load -- add the load counters. */
void load_page(int page_id, int frame_id) {
    PageInfo p = sim_get_page_info(page_id);

    if (p.type == PAGE_ANON) {
        if (p.swap_slot != -1) {
            sim_read_from_swap(frame_id, p.swap_slot);
            sim_free_swap_slot(p.swap_slot);
        } else {
            sim_zero_fill_frame(frame_id);
        }
    } else {
        sim_read_from_file(frame_id, p.file_id, p.file_offset);
    }

    sim_mark_page_loaded(page_id, frame_id);
}

/* PROVIDED: do not modify. */
int handle_page_fault(int page_id) {
    PageInfo faulting = sim_get_page_info(page_id);
    printf("[PAGE FAULT] Page %d (%s)\n",
           page_id, faulting.type == PAGE_ANON ? "ANON" : "FILE");

    int assigned_frame = -1;
    for (int i = 0; i < NUM_FRAMES; i++) {
        bool used = false;
        for (int p = 0; p < sim_page_count(); p++) {
            PageInfo owner = sim_get_page_info(p);
            if (owner.resident && owner.frame_id == i) {
                used = true;
                break;
            }
        }
        if (!used) {
            assigned_frame = i;
            break;
        }
    }

    if (assigned_frame == -1) {
        int victim_id = select_victim_page();
        assigned_frame = sim_get_page_info(victim_id).frame_id;
        evict_page(victim_id);
    }

    load_page(page_id, assigned_frame);
    return assigned_frame;
}
