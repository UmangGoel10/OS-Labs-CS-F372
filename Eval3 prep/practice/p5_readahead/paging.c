#include "paging_sim.h"

/*
 * P5 -- Write handle_page_fault yourself (+ sequential read-ahead)
 *
 * TWIST: in Lab 7 the page-fault handler was PROVIDED.  Here it is YOURS.
 * select_victim_page / evict_page / load_page are plain Lab 7 and provided.
 *
 * handle_page_fault(page_id) must, in this order:
 *
 *   1. print   "[PAGE FAULT] Page <id> (ANON|FILE)\n"      (exact text, as in Lab 7)
 *   2. get a frame for the faulting page:
 *        - the LOWEST-numbered free frame, if there is one;
 *        - otherwise ask select_victim_page(), remember the victim's frame,
 *          then evict_page(victim).
 *   3. load_page(page_id, frame)
 *   4. READ-AHEAD.  If the faulting page is file-backed, also bring in page
 *      (page_id + 1) -- but ONLY if ALL of these hold:
 *        - page_id + 1 is a valid page (< sim_page_count())
 *        - it is file-backed, same file_id, and its file_offset is exactly
 *          the faulting page's file_offset + PAGE_SIZE
 *        - it is NOT already resident
 *        - a FREE frame exists right now.  Read-ahead must NEVER evict anything.
 *      When it happens, first print  "  [READAHEAD] Page <n> -> Frame <f>\n"
 *      (two leading spaces) and then call load_page(n, f).
 *   5. return the frame that holds the FAULTING page (not the read-ahead page).
 *
 * Run:   cd practice && ./run.sh p5_readahead        (add `sol` for solution.c)
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

/* PROVIDED (unchanged from Lab 7). */
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

/* TODO 1: helper -- lowest-numbered free frame, or -1.  (You will call it twice.) */

/* TODO 2: the whole page-fault handler, per the spec above. */
int handle_page_fault(int page_id) {
    (void)page_id;
    return -1;
}
