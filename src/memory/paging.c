#include "memory/paging.h"
#include "stdlib/string.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_VIRT_BASE 0xC0000000u

/**
 * _pg_kernel_pg_dir and _pg_kernel_pg_table is initialized on kernel-entrypoint.s
 */
__attribute__((aligned(0x1000))) struct PgDir _pg_kernel_pg_dir;
__attribute__((aligned(0x1000))) struct PgTable _pg_kernel_pg_table;

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
    if (dir == NULL) return false;
    
    // get physical address index
    uint32_t pg_dir_index = ((uint32_t) virt_addr >> 22) & 0x3FF;
    uint32_t pg_table_index = ((uint32_t) virt_addr >> 12) & 0x3FF;

    // a WHOLE BUNCH of checks
    if (pg_dir_index == RECURSIVE_PG_DIR_INDEX) return false;

    // ensure 2nd level page table exists (allocates one if PDE not present)
    struct PgTable *pg_table = pg_get_table(dir, pg_dir_index);
    if (pg_table == NULL) return false;

    // prevent writing to an already mapped page
    if (pg_table->data[pg_table_index].flags.present) return false;

    // find a free physical frame
    for (uint32_t pfn = KERNEL_RESERVED_FRAME_COUNT; pfn < FRAME_COUNT_MAX; pfn++) {
        if (!state.map[pfn]) {
            state.map[pfn] = true;
            state.freed--;
        

        void *phys_addr = (void *) (pfn * FRAME_SIZE);
        struct PgTableEntryFlags user_flag = {
            .present = true,
            .read_write = true,
            .user_supervisor = true
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
    if (dir == NULL) return false;

    uint32_t pg_dir_index = ((uint32_t) virt_addr >> 22) & 0x3FF;
    uint32_t pg_table_index = ((uint32_t) virt_addr >> 12) & 0x3FF;

    // a WHOLE BUNCH of checks
    if (pg_dir_index == RECURSIVE_PG_DIR_INDEX) return false;

    // PDE & PTE must not be absent
    if (!dir->table[pg_dir_index].flags.present) return false;
    struct PgTable *pg_table = pg_get_table(dir, pg_dir_index);
    if (pg_table == NULL) return false; // PDE check
    struct PgTableEntry *pte = &pg_table->data[pg_table_index];
    if (!pte->flags.present) return false; // PTE check

    uint32_t pfn = ((uint32_t) pte->address_high << 4) | (pte->address_low & 0xF);
    if (pfn < KERNEL_RESERVED_FRAME_COUNT || pfn >= FRAME_COUNT_MAX || !state.map[pfn]) return false; // check of 

    // freeing the frame
    state.map[pfn] = false;
    state.freed++;
    memset(pte, 0, sizeof(struct PgTableEntry));
    pg_flush_single_tlb(virt_addr);
    return true;
}
