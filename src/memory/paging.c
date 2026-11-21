#include "memory/paging.h"
#include "process/process.h"
#include "stdlib/string.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_VIRT_BASE 0xC0000000u

__attribute__((aligned(0x1000))) static struct PgDir
    page_directory_list[PAGING_DIRECTORY_TABLE_MAX_COUNT] = {0};

static struct {
    bool page_directory_used[PAGING_DIRECTORY_TABLE_MAX_COUNT];
} page_directory_manager = {
    .page_directory_used = {false},
};

struct PgDir *paging_create_new_page_directory(void) {
    for (uint32_t i = 0; i < PAGING_DIRECTORY_TABLE_MAX_COUNT; i++) {
        if (!page_directory_manager.page_directory_used[i]) {
            // Mark as used
            page_directory_manager.page_directory_used[i] = true;
            struct PgDir *new_dir = &page_directory_list[i];

            // Clear the table of the new dir to rid of any garbage values
            memset(new_dir->table, 0, sizeof(new_dir->table));

            // copy higher-half kernel PDEs (0x300 ... 0x300 + KERNEL_RESERVED_PAGE_TABLE_COUNT - 1)
            // from _pg_kernel_pg_dir so code, data, stack, and IDT are still mapped
            for (uint32_t k = 0; k < KERNEL_RESERVED_PAGE_TABLE_COUNT; k++) {
                new_dir->table[0x300 + k] = _pg_kernel_pg_dir.table[0x300 + k];
            }

            // set up recursive page dir entry at 0xFFC00000 pointing to new_dir phys address
            uint32_t new_dir_phys_addr = (uint32_t) new_dir - KERNEL_VIRTUAL_ADDRESS_BASE;
            uint32_t frame_num = new_dir_phys_addr >> 12;

            new_dir->table[RECURSIVE_PG_DIR_INDEX].flags =
                (struct PgDirEntryFlags) {
                    .present = true,
                    .read_write = true,
                    .user_supervisor = false,
                };
            new_dir->table[RECURSIVE_PG_DIR_INDEX].addr_low = frame_num & 0xF;
            new_dir->table[RECURSIVE_PG_DIR_INDEX].addr_high = (frame_num >> 4) & 0xFFFF;
            /*
                > present bit    true
                > write bit      true
                > pagesize 4 mb  true
                > lower address  0
            */

            // new_dir->table[0x300].flags.present = 1;
            // new_dir->table[0x300].flags.read_write = 1;
            // new_dir->table[0x300].flags.pg_size = 1;
            // new_dir->table[0x300].addr_low = 0;

            return new_dir;
        }
    }
    return NULL;
}

bool paging_free_page_directory(struct PgDir *page_dir) {
    for (uint32_t i = 0; i < PAGING_DIRECTORY_TABLE_MAX_COUNT; i++) {
        if (&page_directory_list[i] == page_dir) {
            // Mark as unused
            page_directory_manager.page_directory_used[i] = false;

            // Clear all page directory
            memset(
                page_directory_list[i].table, 0,
                sizeof(page_directory_list[i].table)
            );

            return true;
        }
    }
    return false;
}

struct PgDir *paging_get_current_page_directory_addr(void) {
    uint32_t current_page_directory_phys_addr;
    __asm__ volatile("mov %%cr3, %0"
                     : "=r"(current_page_directory_phys_addr)
                     : /* <Empty> */);
    uint32_t virtual_addr_page_dir =
        current_page_directory_phys_addr + KERNEL_VIRTUAL_ADDRESS_BASE;
    return (struct PgDir *) virtual_addr_page_dir;
}

void paging_use_page_directory(struct PgDir *page_dir_virtual_addr) {
    uint32_t physical_addr_page_dir = (uint32_t) page_dir_virtual_addr;
    // Additional layer of check & mistake safety net
    if ((uint32_t) page_dir_virtual_addr > KERNEL_VIRTUAL_ADDRESS_BASE)
        physical_addr_page_dir -= KERNEL_VIRTUAL_ADDRESS_BASE;
    __asm__ volatile("mov %0, %%cr3"
                     : /* <Empty> */
                     : "r"(physical_addr_page_dir)
                     : "memory");
}

/**
 * _pg_kernel_pg_dir and _pg_kernel_pg_table is initialized on kernel-entrypoint.s
 */
__attribute__((aligned(0x1000))) struct PgDir _pg_kernel_pg_dir;
__attribute__((aligned(0x1000))) struct PgTable _pg_kernel_pg_table[KERNEL_RESERVED_PAGE_TABLE_COUNT];

/**
 * Global page frame manager state tracking free/busy frames.
 */
struct PgState state = {
    .map =
        {[0 ... KERNEL_RESERVED_FRAME_COUNT - 1] = true,
         [KERNEL_RESERVED_FRAME_COUNT... FRAME_COUNT_MAX - 1] = false},
    .freed = FRAME_COUNT_MAX - KERNEL_RESERVED_FRAME_COUNT,
};

struct PgDir *pg_get_cur_addr(void) {
    uint32_t cur_dir_phys_addr;
    __asm__ volatile("mov %%cr3, %0"
                     : "=r"(cur_dir_phys_addr));

    return (struct PgDir *) (cur_dir_phys_addr + KERNEL_VIRT_BASE);
}

struct PgTable *pg_get_table(
    struct PgDir *dir,
    uint32_t dir_index
) {
    //this helper function is given from kit but you can always modified it if you think theres a bug (spoiler alert) or you think theres a better approach
    // makasih yang ngasih tau muach (consensual ofc)

    if (dir_index == RECURSIVE_PG_DIR_INDEX || dir != pg_get_cur_addr()) {
        return NULL;
    }

    struct PgDirEntry *dir_entry = &dir->table[dir_index];

    if (dir_entry->flags.present) {
        return (struct PgTable *) (RECURSIVE_PG_TABLES_VIRT_ADDR +
                                   dir_index * FRAME_SIZE);
    }

    for (
        uint32_t frame_num = KERNEL_RESERVED_FRAME_COUNT;
        frame_num < FRAME_COUNT_MAX; ++frame_num
    ) {
        if (state.map[frame_num]) {
            continue;
        }

        state.map[frame_num] = true;
        --state.freed;

        dir_entry->flags = (struct PgDirEntryFlags){
            .present = true,
            .read_write = true,
            .user_supervisor = true,
        };

        // addr low is the 4 lowest bits
        dir_entry->addr_low = frame_num & 0xF;

        // addr high is the upper 16 bits
        dir_entry->addr_high = (frame_num >> 4) & 0xFFFF;

        struct PgTable *table =
            (struct PgTable *) (RECURSIVE_PG_TABLES_VIRT_ADDR +
                                dir_index * FRAME_SIZE);

        pg_flush_single_tlb(table);
        memset(table, 0, FRAME_SIZE);
        return table;
    }

    return NULL;
}

void pg_update_entry(
    struct PgDir *dir,
    void *phys_addr,
    void *virt_addr,
    struct PgTableEntryFlags flags
) {
    uint32_t dir_index = ((uint32_t) virt_addr >> 22) & 0x3FF;
    uint32_t table_index = ((uint32_t) virt_addr >> 12) & 0x3FF;

    struct PgTable *table = pg_get_table(dir, dir_index);

    if (table == NULL) {
        return;
    }

    memset(&table->data[table_index], 0, sizeof(struct PgTableEntry));

    uint32_t frame_num = (uint32_t) phys_addr >> 12;

    table->data[table_index].flags = flags;
    table->data[table_index].address_low = frame_num & 0xF;
    table->data[table_index].address_high = (frame_num >> 4) & 0xFFFF;

    pg_flush_single_tlb(virt_addr);
}

void pg_flush_single_tlb(void *virt_addr) {
    asm volatile("invlpg (%0)"
                 : /* <Empty> */
                 : "b"(virt_addr)
                 : "memory");
}

/* --- Memory Management --- */
// TODO: Implement
bool pg_check_alloc(uint32_t amount) {
    uint32_t required_frames = (amount + FRAME_SIZE - 1) / FRAME_SIZE;
    return state.freed >= required_frames;
}

bool pg_alloc_user_frame(
    struct PgDir *dir,
    void *virt_addr
) {
    if (dir == NULL)
        return false;

    // get physical address index
    uint32_t pg_dir_index = ((uint32_t) virt_addr >> 22) & 0x3FF;
    uint32_t pg_table_index = ((uint32_t) virt_addr >> 12) & 0x3FF;

    // a WHOLE BUNCH of checks
    if (pg_dir_index == RECURSIVE_PG_DIR_INDEX)
        return false;

    // ensure 2nd level page table exists (allocates one if PDE not present)
    struct PgTable *pg_table = pg_get_table(dir, pg_dir_index);
    if (pg_table == NULL)
        return false;

    // prevent writing to an already mapped page
    if (pg_table->data[pg_table_index].flags.present)
        return false;

    // find a free physical frame
    for (
        uint32_t pfn = KERNEL_RESERVED_FRAME_COUNT; pfn < FRAME_COUNT_MAX; pfn++
    ) {
        if (!state.map[pfn]) {
            state.map[pfn] = true;
            state.freed--;

            void *phys_addr = (void *) (pfn * FRAME_SIZE);
            struct PgTableEntryFlags user_flag = {
                .present = true, .read_write = true, .user_supervisor = true
            };

            pg_update_entry(dir, phys_addr, virt_addr, user_flag);

            // remove stale data from newly mapped 4 KiB page
            memset((void *) ((uint32_t) virt_addr & ~0xFFFu), 0, FRAME_SIZE);
            return true;
        }
    }
    return false;
}

bool pg_free_user_frame(
    struct PgDir *dir,
    void *virt_addr
) {
    if (dir == NULL)
        return false;

    uint32_t pg_dir_index = ((uint32_t) virt_addr >> 22) & 0x3FF;
    uint32_t pg_table_index = ((uint32_t) virt_addr >> 12) & 0x3FF;

    // a WHOLE BUNCH of checks
    if (pg_dir_index == RECURSIVE_PG_DIR_INDEX)
        return false;

    // PDE & PTE must not be absent
    if (!dir->table[pg_dir_index].flags.present)
        return false;
    struct PgTable *pg_table = pg_get_table(dir, pg_dir_index);
    if (pg_table == NULL)
        return false; // PDE check
    struct PgTableEntry *pte = &pg_table->data[pg_table_index];
    if (!pte->flags.present)
        return false; // PTE check

    uint32_t pfn =
        ((uint32_t) pte->address_high << 4) | (pte->address_low & 0xF);
    if (pfn < KERNEL_RESERVED_FRAME_COUNT || pfn >= FRAME_COUNT_MAX ||
        !state.map[pfn])
        return false; // check of

    // freeing the frame
    state.map[pfn] = false;
    state.freed++;
    memset(pte, 0, sizeof(struct PgTableEntry));
    pg_flush_single_tlb(virt_addr);
    return true;
}

void pg_free_frame_num(uint32_t frame_num) {
    state.map[frame_num] = false;
    ++state.freed;
}
