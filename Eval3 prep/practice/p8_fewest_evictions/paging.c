#include "paging_sim.h"

/*
 * P8 -- Multi-key tie-break chain + your own bookkeeping with exact update rules
 *
 * TWIST (the Lab 6 lesson): Lab 6's worst-fit rule is a CHAIN --
 *     "largest block; if equal, smallest last_used; if equal, smallest offset"
 * -- and the lab spends a whole README paragraph on when the extra metadata
 * (last_used) is updated.  Every sentence of that paragraph is a test.
 * Practise exactly that, in paging terms.
 *
 * Replace the four-bucket policy with:
 *
 *     victim = the resident page with
 *         1. the FEWEST evictions so far
 *         2. if tied: the smallest last_access_time
 *         3. if tied: the smallest page_id
 *
 * Bookkeeping rules (you keep the counters yourself):
 *   - evictions[p] goes up by one every time evict_page(p) runs, no matter who
 *     called it (the handler, or the EVICT driver command).
 *   - it is NEVER reset: reloading a page keeps its count.
 *   - select_victim_page() is a pure chooser: calling it changes nothing.
 *   - return -1 only if no page is resident.
 *
 * evict_page's I/O behaviour and load_page are plain Lab 7.
 *
 * Run:   cd practice && ./run.sh p8_fewest_evictions        (add `sol` for solution.c)
 */

/* TODO: add the per-page counter(s) here. */

/* TODO 1: the three-key chooser. */
int select_victim_page(void) {
    return -1;
}

/* TODO 2: Lab 7 eviction -- add the bookkeeping. */
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

/* PROVIDED (unchanged from Lab 7). */
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
