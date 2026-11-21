#include "process/process.h"
#include "cpu/gdt.h"
#include "memory/paging.h"
#include "stdlib/string.h"

static uint32_t ceil_div(uint32_t a, uint32_t b) {
    return (a / b) + (a % b != 0);
}

struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX] = {0};
struct ProcessManagerState process_manager_state = {
    .is_process_active = {false},
    .active_process_count = 0,
};

static int32_t process_list_get_inactive_index(void) {
    for (int32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        if (!process_manager_state.is_process_active[i] && _process_list[i].metadata.state == PROCESS_STATE_INACTIVE) {
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

int32_t process_create_user_process(struct EXT2DriverRequest request) {
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
    void *user_stack_virt_addr = (void *) (KERNEL_VIRTUAL_ADDRESS_BASE - FRAME_SIZE);
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

    // 6. initialize CPU context
    uint32_t user_data_selector = GDT_USER_DATA_SEGMENT_SELECTOR | 0x3;
    new_pcb->context.cpu.segment.ds = user_data_selector;
    new_pcb->context.cpu.segment.es = user_data_selector;
    new_pcb->context.cpu.segment.fs = user_data_selector;
    new_pcb->context.cpu.segment.gs = user_data_selector;
    new_pcb->context.cpu.stack.esp = KERNEL_VIRTUAL_ADDRESS_BASE - 4;

    new_pcb->context.eip = 0x00000000;
    new_pcb->context.eflags = CPU_EFLAGS_BASE_FLAG | CPU_EFLAGS_FLAG_INTERRUPT_ENABLE;
    new_pcb->context.page_directory_virtual_addr = (uint32_t) new_page_dir;

    // 7. initialize PCB metadata and manager bookkeeping
    new_pcb->metadata.pid = process_generate_new_pid();
    new_pcb->metadata.state = PROCESS_STATE_READY;
    uint32_t copy_len = request.name_len < PROCESS_NAME_LENGTH_MAX - 1 ? request.name_len : PROCESS_NAME_LENGTH_MAX - 1;
    memcpy(new_pcb->metadata.name, request.name, copy_len);
    new_pcb->metadata.name[copy_len] = '\0';

    process_manager_state.is_process_active[p_index] = true;
    process_manager_state.active_process_count++;

exit_cleanup:
    return retcode;
}
