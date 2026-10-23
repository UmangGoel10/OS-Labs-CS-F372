#include "paging_sim.h"

/*
 * P4 -- Fault accounting (reference solution)
 *
 * Policy and I/O behaviour are exactly Lab 7.  New: count what happens.
 *
 *   load_page   anon, no swap slot (zero-fill)     -> MINOR  fault
 *               anon, has swap slot (swap-in)      -> MAJOR  fault, swap_in
 *               file-backed (read from file)       -> MAJOR  fault, file_read
 *   evict_page  anon (always written to swap)      -> swap_out
 *               file, dirty                        -> file_writeback
 *               file, clean                        -> clean_discard (no I/O)
 *
 *   faults = minor + major
 */

static int minor_faults, major_faults;
static int swap_in, swap_out;
static int file_read, file_writeback, clean_discard;

void print_fault_stats(void) {
    printf("STATS faults=%d minor=%d major=%d swap_in=%d swap_out=%d "
           "file_read=%d file_writeback=%d clean_discard=%d\n",
           minor_faults + major_faults, minor_faults, major_faults,
           swap_in, swap_out, file_read, file_writeback, clean_discard);
}

static int category(PageInfo p) {
    if (p.type == PAGE_ANON)
        return p.dirty ? 3 : 1;
    return p.dirty ? 4 : 2;
}

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

void evict_page(int victim_page_id) {
    PageInfo p = sim_get_page_info(victim_page_id);

    if (p.type == PAGE_ANON) {
        int slot = p.swap_slot;
        if (slot == -1)
            slot = sim_alloc_swap_slot();
        sim_write_to_swap(p.frame_id, slot);
        sim_set_swap_slot(victim_page_id, slot);
        swap_out++;
    } else if (p.dirty) {
        sim_write_to_file(p.frame_id, p.file_id, p.file_offset);
        file_writeback++;
    } else {
        clean_discard++;
    }

    sim_mark_page_evicted(victim_page_id);
}

void load_page(int page_id, int frame_id) {
    PageInfo p = sim_get_page_info(page_id);

    if (p.type == PAGE_ANON) {
        if (p.swap_slot != -1) {
            sim_read_from_swap(frame_id, p.swap_slot);
            sim_free_swap_slot(p.swap_slot);
            major_faults++;
            swap_in++;
        } else {
            sim_zero_fill_frame(frame_id);
            minor_faults++;
        }
    } else {
        sim_read_from_file(frame_id, p.file_id, p.file_offset);
        major_faults++;
        file_read++;
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
