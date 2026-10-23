#include "paging_sim.h"

/*
 * P3 -- Swap cache: don't rewrite an anonymous page whose swap copy is current
 * (reference solution)
 *
 * Victim selection is the Lab 7 four-bucket + LRU policy, unchanged.
 *
 * load_page:  swap-in an anonymous page but KEEP its swap slot assigned.
 *             (sim_mark_page_loaded() wipes swap_slot back to -1, so the slot
 *              must be re-attached with sim_set_swap_slot() AFTER it.)
 * evict_page: anonymous page
 *               no slot yet            -> allocate, write, record
 *               slot + dirty           -> its swap copy is STALE: overwrite the
 *                                         SAME slot (never allocate a second)
 *               slot + clean           -> swap copy still current: skip the write
 */

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
        if (p.swap_slot == -1) {
            int slot = sim_alloc_swap_slot();
            sim_write_to_swap(p.frame_id, slot);
            sim_set_swap_slot(victim_page_id, slot);
        } else if (p.dirty) {
            sim_write_to_swap(p.frame_id, p.swap_slot);
        }
        /* else: clean and the slot still holds the identical bytes */
    } else if (p.dirty) {
        sim_write_to_file(p.frame_id, p.file_id, p.file_offset);
    }

    sim_mark_page_evicted(victim_page_id);   /* keeps swap_slot untouched */
}

void load_page(int page_id, int frame_id) {
    PageInfo p = sim_get_page_info(page_id);
    int keep_slot = -1;

    if (p.type == PAGE_ANON) {
        if (p.swap_slot != -1) {
            sim_read_from_swap(frame_id, p.swap_slot);
            keep_slot = p.swap_slot;         /* NOT freed any more */
        } else {
            sim_zero_fill_frame(frame_id);
        }
    } else {
        sim_read_from_file(frame_id, p.file_id, p.file_offset);
    }

    sim_mark_page_loaded(page_id, frame_id); /* resets swap_slot to -1 ... */
    if (keep_slot != -1)
        sim_set_swap_slot(page_id, keep_slot); /* ... so put it back */
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
