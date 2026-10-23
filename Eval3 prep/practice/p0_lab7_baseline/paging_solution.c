#include "paging_sim.h"

/*
 * REFERENCE SOLUTION for the original Lab 7 (not a skeleton).
 * Try Lab 7's own paging.c from memory first, then compare against this.
 *
 * The three functions below are also the "baseline" that every practice
 * scenario p1..p5 starts from -- each scenario changes one piece of it.
 */

/* 1 = anon clean, 2 = file clean, 3 = anon dirty, 4 = file dirty */
static int category(PageInfo p) {
    if (p.type == PAGE_ANON)
        return p.dirty ? 3 : 1;
    return p.dirty ? 4 : 2;
}

int select_victim_page(void) {
    int best = -1;
    int best_cat = 0;
    uint64_t best_time = 0;

    /* Ascending page_id scan + strict '<' on the timestamp means an equal
     * timestamp never replaces the earlier (smaller) page_id. */
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
    /* Read the metadata FIRST: sim_mark_page_evicted() clears dirty/frame_id. */
    PageInfo p = sim_get_page_info(victim_page_id);

    if (p.type == PAGE_ANON) {
        /* Always preserved in swap, clean or dirty. */
        int slot = p.swap_slot;
        if (slot == -1)
            slot = sim_alloc_swap_slot();
        sim_write_to_swap(p.frame_id, slot);
        sim_set_swap_slot(victim_page_id, slot);
    } else if (p.dirty) {
        sim_write_to_file(p.frame_id, p.file_id, p.file_offset);
    }
    /* clean file page: nothing to write, the file already has it */

    sim_mark_page_evicted(victim_page_id);
}

void load_page(int page_id, int frame_id) {
    /* Read the metadata FIRST: sim_mark_page_loaded() resets swap_slot to -1. */
    PageInfo p = sim_get_page_info(page_id);

    if (p.type == PAGE_ANON) {
        if (p.swap_slot != -1) {
            sim_read_from_swap(frame_id, p.swap_slot);
            sim_free_swap_slot(p.swap_slot);
        } else {
            sim_zero_fill_frame(frame_id); /* first touch */
        }
    } else {
        sim_read_from_file(frame_id, p.file_id, p.file_offset);
    }

    sim_mark_page_loaded(page_id, frame_id);
}

/* PROVIDED in the lab: do not modify. */
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
