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
    /**
     * TODO: Find free physical frame and map virtual frame into it
     * - Find free physical frame in page_manager_state.page_frame_map[] using any strategies
     * - Mark page_manager_state.page_frame_map[]
     * - Update page directory with user flags:
     *     > present bit    true
     *     > write bit      true
     *     > user bit       true
     *     > pagesize 4 mb  true
     */
    return true;
}

bool pg_free_user_frame(
    struct PgDir *dir,
    void *virt_addr
) {
    /*
     * TODO: Deallocate a physical frame from respective virtual address
     * - Use the page_dir.table values to check mapped physical frame
     * - Remove the entry by setting it into 0
     */
    return true;
}
