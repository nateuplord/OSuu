#ifndef _SWAP_H
#define _SWAP_H

#include "driver/disk.h"
#include "memory/paging.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SWAP_PARTITION_START_LBA 8192 // Start of swap sector based on 5MiB

#define PAGE_FRAME_ON_SWAP_MAX_COUNT                                           \
    10 //can be changed to be as many as you want.

struct SwapManager {
    bool swap_used[PAGE_FRAME_ON_SWAP_MAX_COUNT];
    uint32_t free_count;
} __attribute__((packed));

extern struct SwapManager swap_manager;

/**
 * Initialize the swap manager: all slots free.
 * Must be called once before any swap_in/swap_out/handle_page_fault use.
 */
void swap_init(void);

/**
 * Read and put frame back to memory
 * @param page_dir        Page directory to read
 * @param virtual_addr    Virtual Address that caused page fault
 */
bool swap_in(
    struct PgDir *page_dir,
    void *virtual_addr
);

/**
 * write frame to swap
 * @param page_dir        Page directory to read
 * @param virtual_addr    Virtual Address that caused page fault
 */
bool swap_out(
    struct PgDir *page_dir,
    void *virtual_addr
);

/**
 * read page fault address from cr2
 */
static inline void *read_cr2() {
    void *fault_address;
    __asm__ volatile("mov %%cr2, %0"
                     : "=r"(fault_address));
    return fault_address;
}

/**
 * TLB Flush for a specific virtual address
*/
static inline void flush_tlb(void *m) {
    __asm__ volatile("invlpg (%0)"
                     :
                     : "b"(m)
                     : "memory");
}

/**
 * handle page fault
 * @param error_code error code read from cpu state
 * make sure to handle every cases including page replacement
 */
void handle_page_fault(uint32_t error_code);

/* --- Swap Utils --- */
/**
 * Checks if a Page Table Entry points to a swapped-out page.
 * We use the 'ignored' field (bits 9-11) as our custom swapped flag.
 */
static inline bool is_page_swapped(struct PgTableEntry *pte) {
    // If it's present, it's in RAM. If ignored != 1, it was never mapped.
    return (pte->flags.present == 0) && (pte->ignored == 1);
}

/**
 * Marks a PTE as swapped out and stores the swap slot index.
 * Since the page is not in RAM, the physical address fields are unused.
 * We stash the slot index in `address_high`.
 */
static inline void set_page_swapped(
    struct PgTableEntry *pte,
    uint16_t swap_slot
) {
    pte->flags.present = 0;

    // Set our custom flag to distinguish from an unallocated page
    pte->ignored = 1;

    // Stash the swap slot index (we have 16 bits here, plenty for 10 slots)
    pte->address_high = swap_slot;
    pte->address_low = 0;
}

/**
 * Extracts the swap slot index from a swapped-out PTE.
 */
static inline uint16_t get_swap_slot(struct PgTableEntry *pte) {
    return pte->address_high;
}

#endif
