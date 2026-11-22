#include "process/process.h"
#include "cpu/gdt.h"
#include "memory/paging.h"
#include "stdlib/string.h"

static uint32_t ceil_div(
    uint32_t a,
    uint32_t b
) {
    return (a / b) + (a % b != 0);
}

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX] = {0};

/** Dedicated kernel stack per process, used whenever ring 3 is interrupted. */
static uint8_t process_kernel_stacks[PROCESS_COUNT_MAX]
                                    [PROCESS_KERNEL_STACK_SIZE]
    __attribute__((aligned(16)));

struct ProcessManagerState process_manager_state = {
    .is_process_active = {false},
    .active_process_count = 0,
};

static int32_t process_list_get_inactive_index(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (!process_manager_state.is_process_active[i] &&
            _process_list[i].metadata.state == PROCESS_STATE_INACTIVE) {
            return i;
        }
    }
    return -1;
}

static uint32_t process_generate_new_pid(void) {
    static uint32_t next_pid = 1;
    return next_pid++;
}

struct ProcessControlBlock *process_get_current_running_pcb_pointer(void) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (process_manager_state.is_process_active[i] &&
            _process_list[i].metadata.state == PROCESS_STATE_RUNNING) {
            return &_process_list[i];
        }
    }
    return NULL;
}

void context_init(
    struct Context *context,
    uint32_t entrypoint,
    uint32_t page_directory_virtual_addr,
    uint32_t kernel_stack_top
) {
    memset(context, 0, sizeof(struct Context));

    uint32_t *frame = context->interrupt_frame;
    uint32_t data_selector = GDT_USER_DATA_SEGMENT_SELECTOR_RPL3;

    frame[CONTEXT_FRAME_GS] = data_selector;
    frame[CONTEXT_FRAME_FS] = data_selector;
    frame[CONTEXT_FRAME_ES] = data_selector;
    frame[CONTEXT_FRAME_DS] = data_selector;
    frame[CONTEXT_FRAME_EIP] = entrypoint;
    frame[CONTEXT_FRAME_CS] = GDT_USER_CODE_SEGMENT_SELECTOR_RPL3;
    frame[CONTEXT_FRAME_EFLAGS] =
        CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    frame[CONTEXT_FRAME_USER_ESP] = KERNEL_VIRTUAL_ADDRESS_BASE - 4;
    frame[CONTEXT_FRAME_USER_SS] = data_selector;

    context->cpu.segment.ds = data_selector;
    context->cpu.segment.es = data_selector;
    context->cpu.segment.fs = data_selector;
    context->cpu.segment.gs = data_selector;
    context->cpu.stack.esp = frame[CONTEXT_FRAME_USER_ESP];

    context->eip = entrypoint;
    context->eflags = frame[CONTEXT_FRAME_EFLAGS];
    context->page_directory_virtual_addr = page_directory_virtual_addr;
    context->kernel_stack_top = kernel_stack_top;
}

volatile bool process_interrupt_frame_valid = false;

void process_save_current_context(
    const void *interrupt_frame,
    bool from_ring3
) {
    struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
    process_interrupt_frame_valid = from_ring3 && pcb != NULL;
    if (!process_interrupt_frame_valid) {
        return;
    }

    const uint32_t *src = (const uint32_t *) interrupt_frame;
    for (uint32_t i = 0; i < CONTEXT_INTERRUPT_FRAME_DWORD_COUNT; i++) {
        pcb->context.interrupt_frame[i] = src[i];
    }
}

int32_t process_create_user_process(
    struct EXT2DriverRequest request,
    uint8_t priority
) {
    int32_t retcode = PROCESS_CREATE_SUCCESS;
    if (process_manager_state.active_process_count >= PROCESS_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    // Ensure entrypoint is not located at kernel's section at higher half
    if ((uint32_t) request.buf >= KERNEL_VIRTUAL_ADDRESS_BASE) {
        retcode = PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT;
        goto exit_cleanup;
    }

    // Check whether memory is enough for the executable and additional frame for user stack
    uint32_t page_frame_count_needed =
        ceil_div(request.buffer_size + FRAME_SIZE, FRAME_SIZE);

    if (!pg_check_alloc(page_frame_count_needed) ||
        page_frame_count_needed > PROCESS_PAGE_FRAME_COUNT_MAX) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    // Process PCB
    int32_t p_index = process_list_get_inactive_index();

    if (p_index == -1) {
        retcode = PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED;
        goto exit_cleanup;
    }

    struct ProcessControlBlock *new_pcb = &(_process_list[p_index]);
    memset(new_pcb, 0, sizeof(struct ProcessControlBlock));

    if (priority == 0) {
        priority = PROCESS_PRIORITY_DEFAULT;
    }

    uint32_t kernel_stack_top =
        (uint32_t) &process_kernel_stacks[p_index][PROCESS_KERNEL_STACK_SIZE];

    // 1. create isolated page dir and switch CR3 so mappings and read() target it
    struct PgDir *old_page_dir = paging_get_current_page_directory_addr();
    struct PgDir *new_page_dir = paging_create_new_page_directory();

    if (new_page_dir == NULL) {
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }

    paging_use_page_directory(new_page_dir);

    // 2. map user code/data frames starting from virt addr 0x00000000
    uint32_t code_page_count = page_frame_count_needed - 1;
    uint32_t mapped_count = 0;
    for (uint32_t i = 0; i < code_page_count; i++) {
        void *virt_addr = (void *) (i * FRAME_SIZE);
        if (!pg_alloc_user_frame(new_page_dir, virt_addr)) {
            paging_use_page_directory(old_page_dir);
            paging_free_page_directory(new_page_dir);
            retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
            goto exit_cleanup;
        }
        new_pcb->memory.virtual_addr_used[mapped_count++] = virt_addr;
    }

    // 3. map 1 user stack frame right below higher-half kernel at 0xBFFFF000
    void *user_stack_virt_addr =
        (void *) (KERNEL_VIRTUAL_ADDRESS_BASE - FRAME_SIZE);
    if (!pg_alloc_user_frame(new_page_dir, user_stack_virt_addr)) {
        paging_use_page_directory(old_page_dir);
        paging_free_page_directory(new_page_dir);
        retcode = PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY;
        goto exit_cleanup;
    }
    new_pcb->memory.virtual_addr_used[mapped_count++] = user_stack_virt_addr;
    new_pcb->memory.page_frame_used_count = mapped_count;

    // 4. load executable from EXT2 directly into 0x00000000 of new address space
    request.buf = (void *) 0x00000000;
    request.is_directory = false;
    int8_t fs_read_status = read(request);
    if (fs_read_status != 0) {
        paging_use_page_directory(old_page_dir);
        paging_free_page_directory(new_page_dir);
        memset(new_pcb, 0, sizeof(struct ProcessControlBlock));
        retcode = PROCESS_CREATE_FAIL_FS_READ_FAILURE;
        goto exit_cleanup;
    }

    // 5. restore caller's page directory
    paging_use_page_directory(old_page_dir);

    // 6. initialize CPU context (including the frame used by context_restore)
    context_init(
        &new_pcb->context, 0, (uint32_t) new_page_dir, kernel_stack_top
    );

    // 7. initialize PCB metadata and manager bookkeeping
    new_pcb->metadata.pid = process_generate_new_pid();
    new_pcb->metadata.state = PROCESS_STATE_READY;
    new_pcb->metadata.priority = priority;

    uint32_t copy_len = request.name_len < PROCESS_NAME_LENGTH_MAX - 1
                          ? request.name_len
                          : PROCESS_NAME_LENGTH_MAX - 1;

    memcpy(new_pcb->metadata.name, request.name, copy_len);
    new_pcb->metadata.name[copy_len] = '\0';

    process_manager_state.is_process_active[p_index] = true;
    process_manager_state.active_process_count++;

exit_cleanup:
    return retcode;
}

/**
 * Free the page-table frames allocated by pg_get_table() for a process. PDE
 * entries for the higher-half kernel and the recursive mapping are shared,
 * so they are deliberately left alone.
 */
static void process_free_page_tables(struct PgDir *page_dir) {
    const uint32_t kernel_page_table_start = KERNEL_VIRTUAL_ADDRESS_BASE >> 22;
    for (uint32_t i = 0; i < PG_ENTRY_COUNT; ++i) {
        // Low indices are user space. The kernel PDEs at 0x300+ and the
        // recursive PDE at 0x3FF are shared and must not be freed.
        if (i >= kernel_page_table_start) {
            continue;
        }
        if (!page_dir->table[i].flags.present) {
            continue;
        }

        uint32_t frame_num = ((uint32_t) page_dir->table[i].addr_high << 4) |
                             page_dir->table[i].addr_low;
        if (frame_num >= KERNEL_RESERVED_FRAME_COUNT &&
            frame_num < FRAME_COUNT_MAX) {
            pg_free_frame_num(frame_num);
        }
    }
}

bool process_destroy(uint32_t pid) {
    int32_t index = -1;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; ++i) {
        if (process_manager_state.is_process_active[i] &&
            _process_list[i].metadata.pid == pid) {
            index = (int32_t) i;
            break;
        }
    }
    if (index < 0) {
        return false;
    }

    struct ProcessControlBlock *pcb = &_process_list[index];
    struct PgDir *page_dir =
        (struct PgDir *) pcb->context.page_directory_virtual_addr;
    struct PgDir *current_page_dir = paging_get_current_page_directory_addr();
    bool destroying_current = page_dir == current_page_dir;

    if (page_dir != NULL) {
        // pg_free_user_frame() needs its directory to be the active one.
        if (!destroying_current) {
            paging_use_page_directory(page_dir);
        }

        for (uint32_t i = 0; i < pcb->memory.page_frame_used_count; ++i) {
            if (pcb->memory.virtual_addr_used[i] != NULL) {
                pg_free_user_frame(page_dir, pcb->memory.virtual_addr_used[i]);
            }
        }
        process_free_page_tables(page_dir);
        paging_free_page_directory(page_dir);

        if (destroying_current) {
            paging_use_page_directory(&_pg_kernel_pg_dir);
        } else {
            paging_use_page_directory(current_page_dir);
        }
    }

    memset(pcb, 0, sizeof(struct ProcessControlBlock));
    process_manager_state.is_process_active[index] = false;
    if (process_manager_state.active_process_count > 0) {
        --process_manager_state.active_process_count;
    }

    return true;
}
