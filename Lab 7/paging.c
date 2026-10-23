#include "paging_sim.h"

/*
 * TASK 1: select_victim_page
 *
 * Select one resident page using these priority categories, in order:
 *   1. anonymous + clean
 *   2. file-backed + clean
 *   3. anonymous + dirty
 *   4. file-backed + dirty
 *
 * Within the first non-empty category, choose the page with the smallest
 * last_access_time (LRU). Break timestamp ties using the smaller page_id.
 * Return the page ID, or -1 if no page is resident.
 */
int select_victim_page(void) {
    // TODO 1: Implement the four-category priority + LRU policy.
    return -1;
}

/*
 * TASK 2: evict_page
 *
 * Anonymous page:
 *   - always preserve its contents in swap, regardless of dirty state
 *   - allocate a swap slot if it does not already have one
 *   - write the frame contents to that slot
 *
 * File-backed page:
 *   - if dirty, write the frame back to the backing file
 *   - if clean, skip write-back
 *
 * Finish by marking the page non-resident with sim_mark_page_evicted().
 */
void evict_page(int victim_page_id) {
    (void)victim_page_id; /* Remove this line when implementing the task. */
    // TODO 2
}

/*
 * TASK 3: load_page
 *
 * Anonymous page:
 *   - if it has a swap slot, read from swap and free the slot
 *   - otherwise, this is a first-touch page: zero-fill the frame
 *
 * File-backed page:
 *   - read the page from its backing file
 *
 * Finish by recording the mapping with sim_mark_page_loaded().
 */
void load_page(int page_id, int frame_id) {
    (void)page_id;   /* Remove these lines when implementing the task. */
    (void)frame_id;
    // TODO 3
}

/*
 * PROVIDED: do not modify.
 * Finds a free frame, or selects and evicts a victim, then loads the faulting
 * page into the assigned frame.
 */
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
