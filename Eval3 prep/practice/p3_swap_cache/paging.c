#include "paging_sim.h"

/*
 * P3 -- Swap cache: don't rewrite an anonymous page whose swap copy is current
 *
 * TWIST: in Lab 7 an anonymous page's swap slot is freed the moment it is
 * swapped back in, so evicting it again always costs a swap write.  Change
 * the I/O rules so that a page which has NOT been modified since it came back
 * from swap is evicted for free:
 *
 *   load_page, anonymous page that has a swap slot:
 *       read it from swap exactly as before, but DO NOT free the slot.
 *       The page keeps owning the slot while it is resident.
 *
 *   evict_page, anonymous page:
 *       - no slot yet            -> allocate one, write, record it   (as before)
 *       - has a slot AND dirty   -> the swap copy is stale: overwrite the SAME
 *                                   slot.  (Allocating a fresh slot would leak
 *                                   the old one.)
 *       - has a slot AND clean   -> the swap copy is current: no write at all.
 *
 *   File-backed behaviour is unchanged.  select_victim_page is unchanged.
 *
 * WATCH OUT -- re-read the simulator's sim_mark_page_loaded() in
 * framework/sim_framework.c before you code load_page.
 *
 * Run:   cd practice && ./run.sh p3_swap_cache        (add `sol` for solution.c)
 */

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

/* TODO 1: new eviction rules (this is the Lab 7 version -- change it). */
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

/* TODO 2: new load rules (this is the Lab 7 version -- change it). */
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
