#include "paging_sim.h"

/*
 * P1 -- FIFO victim selection
 *
 * TWIST: Lab 7's four-bucket + LRU policy is replaced by plain FIFO.
 *
 *     victim = the resident page that was loaded EARLIEST.
 *
 * Page type, dirty bit and last_access_time are all IGNORED when choosing.
 * Eviction behaviour (evict_page) is unchanged from Lab 7 and is provided.
 *
 * THINK FIRST: your code is never told about a page *hit* -- it only runs on
 * faults.  What does FIFO need to remember, and which function sees every
 * load?  A page that is evicted and later reloaded goes to the BACK of the
 * queue.
 *
 * Tie-break: none needed (every load gets a unique number).
 * Return -1 only when no page is resident.
 *
 * Run:   cd practice && ./run.sh p1_fifo        (add `sol` to run solution.c)
 */

/* You may add static state here. */

/* TODO 1: choose the victim (earliest-loaded resident page). */
int select_victim_page(void) {
    return -1;
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

/* Lab 7 behaviour is provided.  TODO 2: add whatever bookkeeping FIFO needs. */
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
    /* TODO 2 */
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
