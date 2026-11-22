#include "process/scheduler.h"
#include "cpu/interrupt.h"
#include "memory/paging.h"
#include "process/process.h"
#include <stdint.h>

bool scheduler_tick(void) {
    if (!process_interrupt_frame_valid) {
        return false;
    }

    struct ProcessControlBlock *current =
        process_get_current_running_pcb_pointer();
    if (current == NULL) {
        return false;
    }

    uint32_t current_index = 0;
    while (
        current_index < PROCESS_COUNT_MAX &&
        &_process_list[current_index] != current
    ) {
        ++current_index;
    }
    if (current_index == PROCESS_COUNT_MAX) {
        return false;
    }

    // Round-robin: first READY process after the current one, wrapping around.
    uint32_t next_index = current_index;
    for (uint32_t offset = 1; offset < PROCESS_COUNT_MAX; ++offset) {
        uint32_t candidate = (current_index + offset) % PROCESS_COUNT_MAX;
        if (process_manager_state.is_process_active[candidate] &&
            _process_list[candidate].metadata.state == PROCESS_STATE_READY) {
            next_index = candidate;
            break;
        }
    }

    // Nothing else to run - stay on the current process.
    if (next_index == current_index) {
        return false;
    }

    current->metadata.state = PROCESS_STATE_READY;
    struct ProcessControlBlock *next = &_process_list[next_index];
    next->metadata.state = PROCESS_STATE_RUNNING;

    paging_use_page_directory(
        (struct PgDir *) next->context.page_directory_virtual_addr
    );
    set_tss_kernel_stack(next->context.kernel_stack_top);
    context_restore(
        next->context.kernel_stack_top, next->context.interrupt_frame
    );

    // Unreachable: context_restore() irets into the next process.
    return true;
}
