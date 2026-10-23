#include "paging_sim.h"

/*
 * P5 -- Write handle_page_fault yourself, with sequential file read-ahead
 * (reference solution)
 *
 * select_victim_page / evict_page / load_page are plain Lab 7.
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

/* Lowest-numbered frame that no resident page occupies, or -1 if all are used. */
static int find_free_frame(void) {
    for (int f = 0; f < NUM_FRAMES; f++) {
        bool used = false;
        for (int i = 0; i < sim_page_count(); i++) {
            PageInfo p = sim_get_page_info(i);
            if (p.resident && p.frame_id == f) {
                used = true;
                break;
            }
        }
        if (!used)
            return f;
    }
    return -1;
}

int handle_page_fault(int page_id) {
    PageInfo faulting = sim_get_page_info(page_id);
    printf("[PAGE FAULT] Page %d (%s)\n",
           page_id, faulting.type == PAGE_ANON ? "ANON" : "FILE");

    /* 1. frame for the faulting page: free one, else evict a victim */
    int frame = find_free_frame();
    if (frame == -1) {
        int victim = select_victim_page();
        frame = sim_get_page_info(victim).frame_id;   /* before evict clears it */
        evict_page(victim);
    }
    load_page(page_id, frame);

    /* 2. read-ahead: next page of the SAME file, only into a FREE frame */
    if (faulting.type == PAGE_FILE && page_id + 1 < sim_page_count()) {
        PageInfo next = sim_get_page_info(page_id + 1);
        if (next.type == PAGE_FILE && next.file_id == faulting.file_id &&
            next.file_offset == faulting.file_offset + PAGE_SIZE &&
            !next.resident) {
            int spare = find_free_frame();
            if (spare != -1) {
                printf("  [READAHEAD] Page %d -> Frame %d\n", page_id + 1, spare);
                load_page(page_id + 1, spare);
            }
        }
    }

    return frame;
}
