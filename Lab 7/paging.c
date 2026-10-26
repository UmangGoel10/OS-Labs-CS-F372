// #include "paging_sim.h"

// /*
//  * TASK 1: select_victim_page
//  *
//  * Select one resident page using these priority categories, in order:
//  *   1. anonymous + clean
//  *   2. file-backed + clean
//  *   3. anonymous + dirty
//  *   4. file-backed + dirty
//  *
//  * Within the first non-empty category, choose the page with the smallest
//  * last_access_time (LRU). Break timestamp ties using the smaller page_id.
//  * Return the page ID, or -1 if no page is resident.
//  */
// int select_victim_page(void) {
//     // TODO 1: Implement the four-category priority + LRU policy.
//     return -1;
// }

// /*
//  * TASK 2: evict_page
//  *
//  * Anonymous page:
//  *   - always preserve its contents in swap, regardless of dirty state
//  *   - allocate a swap slot if it does not already have one
//  *   - write the frame contents to that slot
//  *
//  * File-backed page:
//  *   - if dirty, write the frame back to the backing file
//  *   - if clean, skip write-back
//  *
//  * Finish by marking the page non-resident with sim_mark_page_evicted().
//  */
// void evict_page(int victim_page_id) {
//     (void)victim_page_id; /* Remove this line when implementing the task. */
//     // TODO 2
// }

// /*
//  * TASK 3: load_page
//  *
//  * Anonymous page:
//  *   - if it has a swap slot, read from swap and free the slot
//  *   - otherwise, this is a first-touch page: zero-fill the frame
//  *
//  * File-backed page:
//  *   - read the page from its backing file
//  *
//  * Finish by recording the mapping with sim_mark_page_loaded().
//  */
// void load_page(int page_id, int frame_id) {
//     (void)page_id;   /* Remove these lines when implementing the task. */
//     (void)frame_id;
//     // TODO 3
// }

// /*
//  * PROVIDED: do not modify.
//  * Finds a free frame, or selects and evicts a victim, then loads the faulting
//  * page into the assigned frame.
//  */
// int handle_page_fault(int page_id) {
//     PageInfo faulting = sim_get_page_info(page_id);
//     printf("[PAGE FAULT] Page %d (%s)\n",
//            page_id, faulting.type == PAGE_ANON ? "ANON" : "FILE");

//     int assigned_frame = -1;
//     for (int i = 0; i < NUM_FRAMES; i++) {
//         bool used = false;
//         for (int p = 0; p < sim_page_count(); p++) {
//             PageInfo owner = sim_get_page_info(p);
//             if (owner.resident && owner.frame_id == i) {
//                 used = true;
//                 break;
//             }
//         }
//         if (!used) {
//             assigned_frame = i;
//             break;
//         }
//     }

//     if (assigned_frame == -1) {
//         int victim_id = select_victim_page();
//         assigned_frame = sim_get_page_info(victim_id).frame_id;
//         evict_page(victim_id);
//     }

//     load_page(page_id, assigned_frame);
//     return assigned_frame;
// }

#include "paging_sim.h"

/* Lower number means a higher eviction priority. */
static int eviction_priority(const PageInfo *page) {
    if (page->type == PAGE_ANON)
        return page->dirty ? 2 : 0;

    return page->dirty ? 3 : 1;
}

int select_victim_page(void) {
    int victim = -1;
    int best_priority = 5;
    uint64_t oldest = UINT64_MAX;

    for (int i = 0; i < sim_page_count(); i++) {
        PageInfo page = sim_get_page_info(i);
        if (!page.resident)
            continue;

        int priority = eviction_priority(&page);
        bool better_category = priority < best_priority;
        bool same_category_but_older =
            priority == best_priority &&
            (victim == -1 || page.last_access_time < oldest ||
             (page.last_access_time == oldest && page.page_id < victim));

        if (victim == -1 || better_category || same_category_but_older) {
            victim = page.page_id;
            best_priority = priority;
            oldest = page.last_access_time;
        }
    }

    return victim;
}

void evict_page(int victim_page_id) {
    PageInfo victim = sim_get_page_info(victim_page_id);

    if (victim.type == PAGE_ANON) {
        /* Anonymous contents have to be preserved even when dirty == false. */
        int slot = victim.swap_slot;
        if (slot == -1) {
            slot = sim_alloc_swap_slot();
            sim_set_swap_slot(victim_page_id, slot);
        }
        sim_write_to_swap(victim.frame_id, slot);
    } else if (victim.dirty) {
        sim_write_to_file(victim.frame_id, victim.file_id, victim.file_offset);
    }

    sim_mark_page_evicted(victim_page_id);
}

void load_page(int page_id, int frame_id) {
    PageInfo page = sim_get_page_info(page_id);

    if (page.type == PAGE_ANON) {
        if (page.swap_slot != -1) {
            sim_read_from_swap(frame_id, page.swap_slot);
            sim_free_swap_slot(page.swap_slot);
        } else {
            sim_zero_fill_frame(frame_id);
        }
    } else {
        sim_read_from_file(frame_id, page.file_id, page.file_offset);
    }

    sim_mark_page_loaded(page_id, frame_id);
}

/* Provided orchestration: do not ask students to implement this function. */
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
