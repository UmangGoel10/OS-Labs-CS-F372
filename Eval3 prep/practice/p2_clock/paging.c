#include "paging_sim.h"

/*
 * P2 -- Clock (second-chance) victim selection
 *
 * TWIST: this framework copy exposes a hardware REFERENCE BIT:
 *     PageInfo.referenced          set on every access and on load
 *     sim_clear_referenced(page)   clears it
 *
 * Replace the four-bucket policy with CLOCK:
 *
 *   - A "hand" points at a physical FRAME (0 .. NUM_FRAMES-1) and moves
 *     around in a circle, one frame per step.
 *   - Look at the page sitting in the hand's frame:
 *         referenced == 1  -> clear the bit (second chance), advance the hand
 *         referenced == 0  -> this page is the victim; leave the hand pointing
 *                             at the frame AFTER the victim's frame
 *   - Skip frames that hold no page (SELECT can be called with free frames).
 *   - The hand lives across calls -> static state.
 *   - Return -1 (and do not loop forever!) if no page is resident.
 *
 * Note select_victim_page() has a SIDE EFFECT here (it clears ref bits and
 * moves the hand), even when called by the SELECT command rather than a fault.
 *
 * evict_page / load_page are unchanged from Lab 7 and provided.
 *
 * Run:   cd practice && ./run.sh p2_clock        (add `sol` to run solution.c)
 */

/* You may add static state / helpers here (you will want frame -> page). */

/* TODO: implement CLOCK. */
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
