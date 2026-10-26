#include "paging_sim.h"

/*
 * P8 -- Multi-key tie-break chain + your own bookkeeping with exact update rules
 * (reference solution)
 *
 * This is the shape of Lab 6's find_worst_fit():
 *     "largest block; if equal, smallest last_used; if equal, smallest offset"
 * plus its rules about WHEN last_used changes.  Here:
 *
 *     victim = resident page with
 *         1. the FEWEST evictions so far          (anti-thrash)
 *         2. then the smallest last_access_time   (LRU)
 *         3. then the smallest page_id
 *
 *     evictions[p] is incremented every time evict_page(p) runs, whoever
 *     calls it, and is never reset (not by reloading, not by SELECT).
 *
 * The four-bucket rule of Lab 7 is NOT used.  select_victim_page is a pure
 * chooser: it must not change any state.
 */

static int evictions[NUM_PAGES];

int select_victim_page(void) {
    int best = -1;
    uint64_t best_time = 0;

    /* Ascending page id + strict '<' on every key: a full tie keeps the
     * smaller page_id automatically. */
    for (int i = 0; i < sim_page_count(); i++) {
        PageInfo p = sim_get_page_info(i);
        if (!p.resident)
            continue;

        if (best == -1 ||
            evictions[i] < evictions[best] ||
            (evictions[i] == evictions[best] && p.last_access_time < best_time)) {
            best = i;
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
    } else if (p.dirty) {
        sim_write_to_file(p.frame_id, p.file_id, p.file_offset);
    }

    sim_mark_page_evicted(victim_page_id);
    evictions[victim_page_id]++;
}

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
