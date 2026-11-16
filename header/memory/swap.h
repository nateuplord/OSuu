#ifndef _SWAP_H
#define _SWAP_H

#include "memory/paging.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_FRAME_ON_SWAP_MAX_COUNT                                           \
    10 //can be changed to be as many as you want.

struct SwapManager {
    bool swap_used[PAGE_FRAME_ON_SWAP_MAX_COUNT];
    uint32_t free_count;
} __attribute__((packed));

extern struct SwapManager swap_manager;

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
void swap_out(
    struct PgDir *page_dir,
    void *virtual_addr
);

/**
 * read page fault address from cr2
 */
uint32_t read_cr2() {
    uint32_t fault_address;
    __asm__ volatile("mov %%cr2, %0"
                     : "=r"(fault_address));
    return fault_address;
}

/**
 * handle page fault
 * @param error_code error code read from cpu state
 * make sure to handle every cases including page replacement
 */
void handle_page_fault(uint32_t error_code);

#endif
