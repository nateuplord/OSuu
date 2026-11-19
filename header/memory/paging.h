#ifndef _PAGING_H
#define _PAGING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Note: MB often referring to MiB in context of memory management
 * change this to be a small number to test if swap working properly
 * kernel will be tested with a small number of SYS_MEM_MB
 */
#define SYS_MEM_MB 128

/// Number of entries in a page directory
#define PG_ENTRY_COUNT 1024

/// Page Frame (PF) Size: 4 KiB
#define FRAME_SIZE 0x1000u

#define KERNEL_RESERVED_FRAME_COUNT (0x400000u / FRAME_SIZE)
#define RECURSIVE_PG_TABLES_VIRT_ADDR 0xFFC00000u
#define RECURSIVE_PG_DIR_INDEX 1023u

/// Maximum usable page frame. Default count: 128 MiB / 4 KiB = 32768 frames
#define FRAME_COUNT_MAX ((SYS_MEM_MB << 20) / FRAME_SIZE)

/// TODO: Define ts
#define PAGING_DIRECTORY_TABLE_MAX_COUNT

/// kernel's page directory, using 4 KiB pages
extern struct PgDir _pg_kernel_pg_dir;
extern struct PgTable _pg_kernel_pg_table;

/**
 * Page directory entry flags
 * @note Only bits 7:0 (first 8 bits)
 * @see [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - ch. 4: _Paging - Figure 4-4 PDE: 4KB page_
 */
struct PgDirEntryFlags {
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
    uint32_t pg_size : 1;
} __attribute__((packed));

/**
 * Page table entry flags
 * @note Only bits 7:0 (first 8 bits)
 * @see [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - ch. 4: _Paging - Figure 4-4 PDE: 4KB page_
 */
struct PgTableEntryFlags {
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
    uint32_t pg_attr_table : 1;
} __attribute__((packed));

/**
 * A page directory entry
 * @note Can also be used as a page table entry
 * @see [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - ch. 4: _Paging - Figure 4-4 PDE: 4KB page_
 */
struct PgDirEntry {
    /// Entry flags
    struct PgDirEntryFlags flags;

    /// Ignored bits
    uint16_t ignored : 4;

    /// Lower 4 bits of the physical frame number
    uint16_t addr_low : 4;

    /// Upper 16 bits of the physical frame number
    uint16_t addr_high : 16;
} __attribute__((packed));

/**
 * A page table entry
 * @see [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - ch. 4: _Paging - Figure 4-4 PDE: 4KB page_
 */
struct PgTableEntry {
    /// Entry flags
    struct PgTableEntryFlags flags;

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
 *
 * @note
 * This data structure is volatile (i.e. can be modified from outside this code).
 * > Check "C volatile" keyword.
 */
struct PgDir {
    struct PgDirEntry table[PG_ENTRY_COUNT];
} __attribute__((packed));

/**
 *  A page table
 */
struct PgTable {
    struct PgTableEntry data[PG_ENTRY_COUNT];
} __attribute__((packed));

/**
 * Page manager states.
 */
struct PgState {
    bool map[FRAME_COUNT_MAX];
    uint32_t freed;
} __attribute__((packed));

struct PgDir *pg_get_cur_addr(void);

/**
 * Edit page directory with respective parameter
 *
 * @param dir
 * Page directory to update
 *
 * @param phys_dir
 * Physical address to map
 *
 * @param virt_addr
 * Virtual address to map
 *
 * @param flags
 * Page entry flags
 */
void pg_update_entry(
    struct PgDir *dir,
    void *phys_dir,
    void *virt_dir,
    struct PgTableEntryFlags flags
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
 * @param dir
 * Page directory to read
 *
 * @param dir_index
 * Page table's position/index on page directory
 *
 */
struct PgTable *pg_get_table(
    struct PgDir *dir,
    uint32_t dir_index
);

/**
 * Invalidate page that contain virtual address in parameter
 *
 * @param virt_addr
 * Virtual address to flush
 */
void pg_flush_single_tlb(void *virt_addr);

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
bool pg_check_alloc(uint32_t amount);

/**
 * Allocate single user page frame in page directory
 *
 * @param dir
 * Page directory to update
 *
 * @param virt_addr
 * Virtual address to be allocated
 *
 * @return
 * Physical address of allocated frame
 */
bool pg_alloc_user_frame(
    struct PgDir *dir,
    void *virt_addr
);

/**
 * Deallocate single user page frame in page directory
 *
 * @param dir
 * Page directory to update
 *
 * @param virt_addr
 * Virtual address to be allocated
 *
 * @return
 * True if successfull, false otherwise
 */
bool pg_free_user_frame(
    struct PgDir *dir,
    void *virt_addr
);

/**
 * Free a single frame from the physical address
*/
void pg_free_frame_num(uint32_t frame_num);

#endif
