#include "process/scheduler.h"
#include "boot/kernel-entrypoint.h"
#include "cpu/interrupt.h"
#include "memory/paging.h"
#include "process/process.h"
#include <stddef.h>
#include <stdint.h>

static struct ProcessControlBlock
    *wait_queues[SCHEDULER_WAIT_QUEUE_COUNT][PROCESS_COUNT_MAX];

static uint32_t process_index(const struct ProcessControlBlock *pcb) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; ++i) {
        if (&_process_list[i] == pcb) {
            return i;
        }
    }
    return PROCESS_COUNT_MAX;
}

/**
 * Pick the highest-priority READY process after `current_index`. Scanning in
 * rotation gives round-robin fairness between processes with equal priority.
 */
static int32_t select_next(uint32_t current_index) {
    int32_t best = -1;
    uint8_t best_priority = 0;

    for (uint32_t offset = 1; offset < PROCESS_COUNT_MAX; ++offset) {
        uint32_t candidate = (current_index + offset) % PROCESS_COUNT_MAX;
        struct ProcessControlBlock *pcb = &_process_list[candidate];

        if (!process_manager_state.is_process_active[candidate] ||
            pcb->metadata.state != PROCESS_STATE_READY) {
            continue;
        }

        if (best < 0 || pcb->metadata.priority > best_priority) {
            best = (int32_t) candidate;
            best_priority = pcb->metadata.priority;
        }
    }

    return best;
}

static void switch_to(uint32_t index) {
    struct ProcessControlBlock *next = &_process_list[index];

    next->metadata.state = PROCESS_STATE_RUNNING;
    process_interrupt_frame_valid = false;

    paging_use_page_directory(
        (struct PgDir *) next->context.page_directory_virtual_addr
    );
    set_tss_kernel_stack(next->context.kernel_stack_top);
    context_restore(
        next->context.kernel_stack_top, next->context.interrupt_frame
    );
}

void scheduler_init(void) {
    for (uint32_t queue = 0; queue < SCHEDULER_WAIT_QUEUE_COUNT; ++queue) {
        for (uint32_t i = 0; i < PROCESS_COUNT_MAX; ++i) {
            wait_queues[queue][i] = NULL;
        }
    }
    process_interrupt_frame_valid = false;
}

bool scheduler_tick(void) {
    if (!process_interrupt_frame_valid) {
        return false;
    }

    struct ProcessControlBlock *current =
        process_get_current_running_pcb_pointer();
    if (current == NULL) {
        return false;
    }

    uint32_t current_index = process_index(current);
    if (current_index == PROCESS_COUNT_MAX) {
        return false;
    }

    int32_t next_index = select_next(current_index);
    if (next_index < 0) {
        return false;
    }

    current->metadata.state = PROCESS_STATE_READY;
    switch_to((uint32_t) next_index);

    // Unreachable: context_restore() irets into the next process.
    return true;
}

bool scheduler_block_current(uint32_t wait_queue) {
    if (wait_queue >= SCHEDULER_WAIT_QUEUE_COUNT) {
        return false;
    }

    struct ProcessControlBlock *current =
        process_get_current_running_pcb_pointer();
    if (current == NULL) {
        return false;
    }

    uint32_t current_index = process_index(current);
    if (current_index == PROCESS_COUNT_MAX) {
        return false;
    }

    int32_t next_index = select_next(current_index);
    if (next_index < 0) {
        return false;
    }

    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; ++i) {
        if (wait_queues[wait_queue][i] == NULL) {
            wait_queues[wait_queue][i] = current;
            break;
        }
    }
    current->metadata.state = PROCESS_STATE_BLOCKED;
    switch_to((uint32_t) next_index);

    // Unreachable: context_restore() irets into the next process.
    return true;
}

void scheduler_wake_waiting(uint32_t wait_queue) {
    if (wait_queue >= SCHEDULER_WAIT_QUEUE_COUNT) {
        return;
    }

    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; ++i) {
        struct ProcessControlBlock *pcb = wait_queues[wait_queue][i];
        if (pcb == NULL) {
            continue;
        }

        if (pcb->metadata.state == PROCESS_STATE_BLOCKED) {
            pcb->metadata.state = PROCESS_STATE_READY;
        }
        wait_queues[wait_queue][i] = NULL;
    }
}

__attribute__((noreturn)) void scheduler_terminate_current(void) {
    struct ProcessControlBlock *current =
        process_get_current_running_pcb_pointer();
    if (current == NULL) {
        paging_use_page_directory(&_pg_kernel_pg_dir);
        set_tss_kernel_stack((uint32_t) &_linker_kernel_stack_top);
        __asm__ volatile("cli");
        while (true) {
            __asm__ volatile("hlt");
        }
    }

    // A dying process may be the only reason the remaining ones are blocked
    // (for example on keyboard input). Give them a chance to run instead of
    // halting the machine.
    for (uint32_t queue = 0; queue < SCHEDULER_WAIT_QUEUE_COUNT; ++queue) {
        scheduler_wake_waiting(queue);
    }

    uint32_t current_index = process_index(current);
    int32_t next_index =
        current_index == PROCESS_COUNT_MAX ? -1 : select_next(current_index);
    uint32_t pid = current->metadata.pid;

    // process_destroy() switches CR3 back to the kernel directory when it
    // destroys the running process; switch_to() then loads the next one.
    process_destroy(pid);

    if (next_index < 0) {
        set_tss_kernel_stack((uint32_t) &_linker_kernel_stack_top);
        __asm__ volatile("cli");
        while (true) {
            __asm__ volatile("hlt");
        }
    }

    switch_to((uint32_t) next_index);
    __builtin_unreachable();
}
