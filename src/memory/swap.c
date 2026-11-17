#include "memory/swap.h"
#include "memory/paging.h"
#include <stdint.h>

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

void handle_page_fault(uint32_t error_code);
