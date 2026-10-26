#include "paging_sim.h"

/*
 * P7 -- Robustness: validate arguments, return status codes
 *
 * TWIST (the Lab 6 lesson): half of what graders test is NOT the happy path.
 * Lab 6 spends tests 05 and 07 on "free 99", "free 1" twice, "alloc 0",
 * "alloc 1025".  The driver commands below call three wrappers around the
 * Lab 7 functions; each must REFUSE bad input -- changing no simulator state,
 * allocating no swap slot, printing no I/O line -- and report it.
 *
 *   int safe_evict(int page_id)
 *       return 0 (INVALID) if page_id is out of range, or the page is not
 *       resident (this also covers evicting the same page twice).
 *       Otherwise evict_page(page_id) and return 1.
 *
 *   int safe_load(int page_id, int frame_id)
 *       return 0 (INVALID) if page_id or frame_id is out of range, or the page
 *       is already resident, or some OTHER resident page already occupies
 *       frame_id.  Otherwise load_page(page_id, frame_id) and return 1.
 *
 *   int safe_fault(int page_id)
 *       return -1 (INVALID) if page_id is out of range.
 *       If the page is ALREADY resident this is not a fault: return its
 *       frame, print nothing, change nothing.
 *       Otherwise return handle_page_fault(page_id).
 *
 * Driver output:  SEVICT <p> -> OK|INVALID   SLOAD <p> <f> -> OK|INVALID
 *                 SFAULT <p> -> frame <f>|INVALID
 *
 * DANGER: sim_get_page_info() does no bounds checking.  Check the range
 * BEFORE you call it, or you read garbage (undefined behaviour).
 *
 * The Lab 7 functions below are provided and unchanged.
 *
 * Run:   cd practice && ./run.sh p7_robust_ids        (add `sol` for solution.c)
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

/* ------------------------- TODO: the three wrappers ------------------------- */

int safe_evict(int page_id) {
    (void)page_id;
    return 0;
}

int safe_load(int page_id, int frame_id) {
    (void)page_id;
    (void)frame_id;
    return 0;
}

int safe_fault(int page_id) {
    (void)page_id;
    return -1;
}
