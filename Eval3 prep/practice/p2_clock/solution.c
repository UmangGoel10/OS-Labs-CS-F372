#include "paging_sim.h"

/*
 * P2 -- Clock / second-chance victim selection (reference solution)
 *
 * A "hand" sweeps the physical FRAMES 0..NUM_FRAMES-1 in a circle:
 *     referenced page   -> clear its ref bit (second chance), move on
 *     unreferenced page -> that's the victim; hand stops just AFTER its frame
 *
 * The hand persists between calls, so it needs static state.
 */

static int hand = 0;   /* next frame to examine */

static int page_in_frame(int frame) {
    for (int i = 0; i < sim_page_count(); i++) {
        PageInfo p = sim_get_page_info(i);
        if (p.resident && p.frame_id == frame)
            return i;
    }
    return -1;
}

int select_victim_page(void) {
    /* Guard: with nothing resident the loop below would never terminate. */
    bool any = false;
    for (int i = 0; i < sim_page_count(); i++) {
        if (sim_get_page_info(i).resident) {
            any = true;
            break;
        }
    }
    if (!any)
        return -1;

    /* At most two laps: lap 1 clears every ref bit, lap 2 must find one. */
    for (;;) {
        int frame = hand;
        hand = (hand + 1) % NUM_FRAMES;

        int pg = page_in_frame(frame);
        if (pg == -1)
            continue;                       /* free frame: nothing to judge */

        if (sim_get_page_info(pg).referenced)
            sim_clear_referenced(pg);       /* second chance */
        else
            return pg;
    }
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
