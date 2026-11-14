#ifndef _PAGING_H
#define _PAGING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Note: MB often referring to MiB in context of memory management
 * change this to be a small number to test if swap working properly
 * kernel will be tested with a small number of SYSTEM_MEMORY_MB
 */
#define SYSTEM_MEMORY_MB 128

/// Number of entries in a page directory
#define PAGE_ENTRY_COUNT 1024

/// Page Frame (PF) Size: 4 KiB
#define PAGE_FRAME_SIZE 0x1000u

#define KERNEL_RESERVED_PAGE_FRAME_COUNT (0x400000u / PAGE_FRAME_SIZE)
#define RECURSIVE_PAGE_TABLES_VIRTUAL_ADDRESS 0xFFC00000u
#define RECURSIVE_PAGE_DIRECTORY_INDEX 1023u

/// Maximum usable page frame. Default count: 128 MiB / 4 KiB = 32768 frames
#define PAGE_FRAME_MAX_COUNT ((SYSTEM_MEMORY_MB << 20) / PAGE_FRAME_SIZE)

/// kernel's  page directory, using 4 KiB pages
extern struct PageDirectory _paging_kernel_page_directory;
extern struct PageTable _paging_kernel_page_table;

/**
 * Page directory entry flags
 * @note Only bits 7:0 (first 8 bits)
 * @see [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - ch. 4: _Paging - Figure 4-4 PDE: 4KB page_
 */
struct PageDirectoryEntryFlag {
    /**
     * Presence in memory (P)
     *
     * @details
     * Settings:
     * - `1`: Present
     * - `0`: Not present
     */
    uint32_t present : 1;

    /**
     * Read/write permission (R/W)
     *
     * @details
     * Settings:
     * - `1`: Read + write
     * - `0`: Read only
     */
    uint32_t read_write : 1;

    /**
     * Access privilege level (U/S)
     *
     * @details
     * Settings:
     * - `1`: Supervisor + user
     * - `0`: Supervisor only
     */
    uint32_t user_supervisor : 1;

    /**
     * Write policy (PWT)
     *
     * @details
     * Settings:
     * - `1`: Write-through (writing goes to cache and memory)
     * - `0`: Write-back
     */
    uint32_t write_through : 1;

    /**
     * Whether caching is disabled (PCD)
     *
     * @details
     * Settings:
     * - `1`: Never cache
     * - `0`: Allow caching
     */
    uint32_t cache_disable : 1;

    /**
     * Accessed flag (A)
     *
     * @details
     * The CPU sets this to 1 the first time the entry is used in address translation,
     * but never clears it, so the OS has to.
     */
    uint32_t accessed : 1;

    /// Ignored bit
    uint32_t reserved : 1;

    /**
     * Page size (PS)
     *
     * @details
     * Settings:
     * - `1`: Maps a 4 MB page directly
     * - `0`: Points to a page table (4 KB pages)
     */
    uint32_t page_size : 1;
} __attribute__((packed));

/**
 * Page table entry flags
 * @note Only bits 7:0 (first 8 bits)
 * @see [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - ch. 4: _Paging - Figure 4-4 PDE: 4KB page_
 */
struct PageTableEntryFlag {
    /**
     * Presence in memory (P)
     *
     * @details
     * Settings:
     * - `1`: Present
     * - `0`: Not present
     */
    uint32_t present : 1;

    /**
     * Read/write permission (R/W)
     *
     * @details
     * Settings:
     * - `1`: Read + write
     * - `0`: Read only
     */
    uint32_t read_write : 1;

    /**
     * Access privilege level (U/S)
     *
     * @details
     * Settings:
     * - `1`: Supervisor + user
     * - `0`: Supervisor only
     */
    uint32_t user_supervisor : 1;

    /**
     * Write policy (PWT)
     *
     * @details
     * Settings:
     * - `1`: Write-through (writing goes to cache and memory)
     * - `0`: Write-back
     */
    uint32_t write_through : 1;

    /**
     * Whether caching is disabled (PCD)
     *
     * @details
     * Settings:
     * - `1`: Never cache
     * - `0`: Allow caching
     */
    uint32_t cache_disable : 1;

    /**
     * Accessed flag (A)
     *
     * @details
     * This flag indicates whether a process has accessed
     * the referenced page of this entry
     *
     * @note
     * The CPU sets this to 1 the first time the entry is used in address translation,
     * but never clears it, so the OS has to.
     */
    uint32_t accessed : 1;

    /**
     * Dirty flag (D)
     *
     * @details
     * A dirty page has been modified since it was loaded,
     * so it must be written back to disk before being evicted.
     *
     * A clean page can just be dropped and reloaded later.
     *
     * @note
     * The CPU sets this to 1 the first time the entry goes through a write,
     * but never clears it, so the OS has to
     */
    uint32_t dirty : 1;

    /**
     * Page attribute table (PAT)
     *
     * @details
     * If supported, indirectly decides the memory caching type
     * for this page (write-back, uncacheable, etc.).
     *
     * @note
     * Leave at 0 unless the PAT is set up (needs CPU support).
     */
    uint32_t page_attr_table : 1;
} __attribute__((packed));

/**
 * A page directory entry
 * @note Can also be used as a page table entry
 * @see [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - ch. 4: _Paging - Figure 4-4 PDE: 4KB page_
 */
struct PageDirectoryEntry {
    /// Entry flags
    struct PageDirectoryEntryFlag flags;

    /// Ignored bits
    uint16_t ignored : 4;

    /// Lower 4 bits of the physical frame number
    uint16_t address_low : 4;

    /// Upper 16 bits of the physical frame number
    uint16_t address_high : 16;
} __attribute__((packed));

/**
 * A page table entry
 * @see [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - ch. 4: _Paging - Figure 4-4 PDE: 4KB page_
 */
struct PageTableEntry {
    /// Entry flags
    struct PageTableEntryFlag flags;

    /**
     * Global flag (G)
     *
     * @details
     * If enabled, translation isn't flushed from the TLB
     * when register `CR3` is reloaded (e.g. on a context switch).
     *
     * @note
     * Only takes effect if bit `PGE` in register `CR4` is enabled,
     * otherwise it's ignored.
     */
    uint32_t global : 1;

    /// Ignored bits
    uint16_t ignored : 3;

    /// Lower 4 bits of the physical frame number
    uint16_t address_low : 4;

    /// Upper 16 bits of the physical frame number
    uint16_t address_high : 16;
} __attribute__((packed));

/**
 * A page directory
 * @note
 * This data structure is volatile (i.e. can be modified from outside this code).
 * > Check "C volatile keyword".
 */
struct PageDirectory {
    struct PageDirectoryEntry table[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 *  A page table
 */
struct PageTable {
    struct PageTableEntry data[PAGE_ENTRY_COUNT];
} __attribute__((packed));

/**
 * Page manager states.
 */
struct PageManagerState {
    bool page_frame_map[PAGE_FRAME_MAX_COUNT];
    uint32_t free_page_frame_count;
} __attribute__((packed));

struct PageDirectory *paging_get_current_page_directory_addr(void);

/**
 * Edit page directory with respective parameter
 *
 * @param page_dir
 * Page directory to update
 *
 * @param physical_addr
 * Physical address to map
 *
 * @param virtual_addr
 * Virtual address to map
 *
 * @param flag
 * Page entry flags
 */
void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageTableEntryFlag flag
);

/**
 * Get the requested page table from respective parameter
 *
 * @note
 * Sudah diberikan implementasinya pada `paging.c`
 * dengan pendekatan _fractal / recursive memory mapping_.
 *
 * @see [OSDev Wiki](https://wiki.osdev.org/Fractal_Page_Mapping)
 *
 * @param page_dir
 * Page directory to read
 *
 * @param page_directory_index
 * Page table's position/index on page directory
 *
 */
struct PageTable *paging_get_page_table(
    struct PageDirectory *page_dir,
    uint32_t page_directory_index
);

/**
 * Invalidate page that contain virtual address in parameter
 *
 * @param virtual_addr
 * Virtual address to flush
 */
void flush_single_tlb(void *virtual_addr);

/* --- Memory Management --- */

/**
 * Check whether a certain amount of physical memory is available
 *
 * @param amount
 * Requested amount of physical memory in bytes
 *
 * @return
 * Returns true when there's enough free memory available
 */
bool paging_allocate_check(uint32_t amount);

/**
 * Allocate single user page frame in page directory
 *
 * @param page_dir
 * Page directory to update
 *
 * @param virtual_addr
 * Virtual address to be allocated
 *
 * @return
 * Physical address of allocated frame
 */
bool paging_allocate_user_page_frame(
    struct PageDirectory *page_dir,
    void *virtual_addr
);

/**
 * Deallocate single user page frame in page directory
 *
 * @param page_dir
 * Page directory to update
 *
 * @param virtual_addr
 * Virtual address to be allocated
 *
 * @return
 * True if successfull, false otherwise
 */
bool paging_free_user_page_frame(
    struct PageDirectory *page_dir,
    void *virtual_addr
);

#endif
