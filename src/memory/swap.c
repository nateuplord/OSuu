#include "memory/swap.h"
#include "memory/paging.h"
#include <stdint.h>

struct SwapManager swap_manager;

// Dir indexes 0..767 are user space (768 == KERNEL_VIRT_BASE >> 22); never evict kernel pages.
#define USER_PG_DIR_COUNT 768

// Helper func to handle Out Of Memory by evicting a page
static bool evict_page_to_swap(struct PgDir *dir) {
    // Brutal FIFO/Scan: find the first present user page and swap it out
    for (uint32_t d = 0; d < USER_PG_DIR_COUNT; d++) {
        // Check presence directly first - pg_get_table() allocates a brand
        // new page table as a side effect when one doesn't exist yet, which
        // would burn exactly the frames we're trying to reclaim.
        if (!dir->table[d].flags.present) {
            continue;
        }

        struct PgTable *table = pg_get_table(dir, d);
        if (!table)
            continue;

        for (uint32_t t = 0; t < PG_ENTRY_COUNT; t++) {
            struct PgTableEntry *pte = &table->data[t];
            if (pte->flags.present == 1) {
                // Reconstruct the victim's virtual address
                void *victim_vaddr = (void *) ((d << 22) | (t << 12));

                if (swap_out(dir, victim_vaddr)) {
                    return true;
                }
            }
        }
    }
    return false; // Total system failure, out of RAM && Swap Space
}

// Initializes the struct SwapManager
void swap_init(void) {
    for (uint32_t i = 0; i < PAGE_FRAME_ON_SWAP_MAX_COUNT; i++) {
        swap_manager.swap_used[i] = false;
    }
    swap_manager.free_count = PAGE_FRAME_ON_SWAP_MAX_COUNT;
}

bool swap_in(
    struct PgDir *page_dir,
    void *virtual_addr
) {
    // Calc indexes
    uint32_t v_addr = (uint32_t) virtual_addr;
    uint32_t dir_idx = v_addr >> 22;
    uint32_t table_idx = (v_addr >> 12) & 0x3FF;

    struct PgTable *table = pg_get_table(page_dir, dir_idx);
    if (!table)
        return false;

    struct PgTableEntry *pte = &table->data[table_idx];

    if (!is_page_swapped(pte))
        return false;

    uint16_t swap_slot = get_swap_slot(pte);

    if (!pg_alloc_user_frame(page_dir, virtual_addr))
        return false;

    uint32_t lba_start = SWAP_PARTITION_START_LBA + (swap_slot * 8);

    void *page = (void *) ((uint32_t) virtual_addr & ~(FRAME_SIZE - 1));
    read_blocks(page, lba_start, 8);

    swap_manager.swap_used[swap_slot] = false;
    swap_manager.free_count++;

    // Flush the TLB
    pg_flush_single_tlb(virtual_addr);

    return true;
}

bool swap_out(
    struct PgDir *page_dir,
    void *virtual_addr
) {
    if (swap_manager.free_count == 0)
        return false; // Out of swap space

    // Calc (short for calculate) the indexes for dir and table
    uint32_t v_addr = (uint32_t) virtual_addr;
    /**
    * Page Offset = 0-11
    * Page Table Index = 12-21
    * Page Directory Index = 22-31
    */
    uint32_t dir_idx = v_addr >> 22;
    uint32_t table_idx = (v_addr >> 12) & 0x3FF;

    // Get the page table and PTE
    struct PgTable *table = pg_get_table(page_dir, dir_idx);
    if (!table)
        return false;

    struct PgTableEntry *pte = &table->data[table_idx];

    // Ensure the page is actually present before swapping
    if (pte->flags.present == 0)
        return false;

    int swap_slot = -1;
    for (uint32_t i = 0; i < PAGE_FRAME_ON_SWAP_MAX_COUNT; i++) {
        if (!swap_manager.swap_used[i]) {
            swap_slot = i;
            break;
        }
    }

    if (swap_slot == -1)
        return false;

    // Extract phys address
    uint32_t phys_addr = (pte->address_high << 16) | (pte->address_low << 12);

    // Write to disk, skipping by sectors (i * 8)
    uint32_t lba_start = SWAP_PARTITION_START_LBA + (swap_slot * 8);
    void *page = (void *) ((uint32_t) virtual_addr & ~(FRAME_SIZE - 1));
    write_blocks(page, lba_start, 8);

    // Update swap manager state
    swap_manager.swap_used[swap_slot] = true;
    swap_manager.free_count--;

    // Update PTE and free phys frame
    set_page_swapped(pte, swap_slot);
    pg_free_frame_num(phys_addr >> 12);

    // Flush TLB so the CPU knows the page is gone
    pg_flush_single_tlb(virtual_addr);

    return true;
}

void handle_page_fault(uint32_t error_code) {
    // Get the faulting v_addr and curr page dir
    void *faulting_addr = read_cr2();
    struct PgDir *pg_dir = pg_get_cur_addr();

    bool present = (error_code & 0x1);
    bool is_write = (error_code & 0x2);
    bool is_user = (error_code & 0x4);

    uint32_t v_addr = (uint32_t) faulting_addr;
    uint32_t dir_idx = v_addr >> 22;
    uint32_t table_idx = (v_addr >> 12) & 0x3FF;

    struct PgTable *table = pg_get_table(pg_dir, dir_idx);
    if (!table) {
        // No page table at all for this region: truly unmapped access.
        // TODO: segfault / kill the process
        return;
    }

    struct PgTableEntry *pte = &table->data[table_idx];

    if (!present) {
        // Non-present fault: either swapped out, or first-time demand paging.
        if (is_page_swapped(pte)) {
            // Page is on disk, move it back to RAM
            while (!swap_in(pg_dir, faulting_addr)) {
                // If swap in failed, RAM is completely full, we must evict a different page to make room
                if (!evict_page_to_swap(pg_dir)) {
                    // Out of RAM and out of swap space.
                    // TODO: kernel panic / kill the process
                    return;
                }
            }
            return;
        }

        // Case B: demand paging (first time access)
        while (!pg_alloc_user_frame(pg_dir, faulting_addr)) {
            // If allocation failed, RAM is full. Evict a page.
            if (!evict_page_to_swap(pg_dir)) {
                // TODO: kernel panic / kill the process
                return;
            }
        }
    } else {
        // Case C: Protection Violation (error_code & 0x1 == 1)
        // i.e writing to read only memory. Segfault the user process
        if (is_user && !pte->flags.user_supervisor) {
            // User mode tried to touch Ring 0 (Kernel) memory.
            // TODO: Terminate process with SIGSEGV (Segmentation Fault).
        } else if (is_write && !pte->flags.read_write) {
            // TODO: Terminate process with SIGSEGV (Segmentation Fault).
        }
    }
}
