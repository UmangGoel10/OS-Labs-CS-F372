#include "paging_sim.h"

/*
 * P7 -- Robustness: validate arguments, return status codes (reference solution)
 *
 * Lab 6's grader spends two of its eight public tests on invalid / repeated
 * operations ("free 99", "free 1" twice, "alloc 0").  Same idea here: three
 * wrappers that must REFUSE bad input without changing ANY simulator state and
 * without printing any I/O line.
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

/* ------------------------------------------------------------------ */

/* Range checks come FIRST: sim_get_page_info(-1) / (12) reads out of bounds. */
static bool valid_page(int page_id) {
    return page_id >= 0 && page_id < sim_page_count();
}

static bool valid_frame(int frame_id) {
    return frame_id >= 0 && frame_id < NUM_FRAMES;
}

int safe_evict(int page_id) {
    if (!valid_page(page_id))
        return 0;
    if (!sim_get_page_info(page_id).resident)
        return 0;                       /* nothing to evict (also: repeated evict) */

    evict_page(page_id);
    return 1;
}

int safe_load(int page_id, int frame_id) {
    if (!valid_page(page_id) || !valid_frame(frame_id))
        return 0;
    if (sim_get_page_info(page_id).resident)
        return 0;                       /* would map the same page twice */

    for (int i = 0; i < sim_page_count(); i++) {
        PageInfo p = sim_get_page_info(i);
        if (p.resident && p.frame_id == frame_id)
            return 0;                   /* frame already holds another page */
    }

    load_page(page_id, frame_id);
    return 1;
}

int safe_fault(int page_id) {
    if (!valid_page(page_id))
        return -1;

    PageInfo p = sim_get_page_info(page_id);
    if (p.resident)
        return p.frame_id;              /* not a fault: report where it lives, print nothing */

    return handle_page_fault(page_id);
}
