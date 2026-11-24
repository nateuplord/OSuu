#include "process/scheduler.h"
#include "boot/kernel-entrypoint.h"
#include "cpu/interrupt.h"
#include "memory/paging.h"
#include "process/process.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Priority queue of runnable processes. Index 0 is always the next process to
 * run. Insertion keeps higher priorities first and preserves arrival order for
 * equal priorities, giving round-robin fairness within a priority level.
 */
struct Queue {
    struct ProcessControlBlock *items[PROCESS_COUNT_MAX];
    uint32_t length;
};

static struct Queue ready_queue;
static struct Queue wait_queues[SCHEDULER_WAIT_QUEUE_COUNT];
static struct ProcessControlBlock *running;

static void queue_init(struct Queue *queue) {
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; ++i) {
        queue->items[i] = NULL;
    }
    queue->length = 0;
}

static bool queue_is_empty(const struct Queue *queue) {
    return queue->length == 0;
}

static int queue_push(
    struct Queue *queue,
    struct ProcessControlBlock *pcb
) {
    if (queue == NULL || pcb == NULL || queue->length >= PROCESS_COUNT_MAX) {
        return 1;
    }

    uint32_t insert_at = 0;
    while (
        insert_at < queue->length &&
        queue->items[insert_at]->metadata.priority >= pcb->metadata.priority
    ) {
        ++insert_at;
    }

    for (uint32_t i = queue->length; i > insert_at; --i) {
        queue->items[i] = queue->items[i - 1];
    }
    queue->items[insert_at] = pcb;
    ++queue->length;
    return 0;
}

static int queue_pop(
    struct Queue *queue,
    struct ProcessControlBlock **pcb
) {
    if (queue == NULL || pcb == NULL || queue->length == 0) {
        return 1;
    }

    *pcb = queue->items[0];
    for (uint32_t i = 1; i < queue->length; ++i) {
        queue->items[i - 1] = queue->items[i];
    }
    queue->items[--queue->length] = NULL;
    return 0;
}

static void queue_remove(
    struct Queue *queue,
    const struct ProcessControlBlock *pcb
) {
    for (uint32_t i = 0; i < queue->length; ++i) {
        if (queue->items[i] != pcb) {
            continue;
        }

        for (uint32_t j = i + 1; j < queue->length; ++j) {
            queue->items[j - 1] = queue->items[j];
        }
        queue->items[--queue->length] = NULL;
        return;
    }
}

static void switch_to(struct ProcessControlBlock *next) {
    next->metadata.state = PROCESS_STATE_RUNNING;
    running = next;
    process_interrupt_frame_valid = false;

    paging_use_page_directory(
        (struct PgDir *) next->context.page_directory_virtual_addr
    );
    set_tss_kernel_stack(next->context.kernel_stack_top);
    context_restore(
        next->context.kernel_stack_top, next->context.interrupt_frame
    );
}

static __attribute__((noreturn)) void halt_cpu(void) {
    paging_use_page_directory(&_pg_kernel_pg_dir);
    set_tss_kernel_stack((uint32_t) &_linker_kernel_stack_top);
    running = NULL;
    process_interrupt_frame_valid = false;

    __asm__ volatile("cli");
    while (true) {
        __asm__ volatile("hlt");
    }
}

static void wake_one_waiting(uint32_t wait_queue) {
    if (wait_queue >= SCHEDULER_WAIT_QUEUE_COUNT) {
        return;
    }

    struct ProcessControlBlock *pcb = NULL;
    if (queue_pop(&wait_queues[wait_queue], &pcb) != 0) {
        return;
    }

    if (pcb->metadata.state != PROCESS_STATE_BLOCKED) {
        return;
    }

    if (queue_push(&ready_queue, pcb) != 0) {
        queue_push(&wait_queues[wait_queue], pcb);
        return;
    }
    pcb->metadata.state = PROCESS_STATE_READY;
}

static void wake_all_waiting(uint32_t wait_queue) {
    uint32_t blocked_count = wait_queues[wait_queue].length;
    for (uint32_t i = 0; i < blocked_count; ++i) {
        wake_one_waiting(wait_queue);
    }
}

void scheduler_init(void) {
    queue_init(&ready_queue);
    for (uint32_t queue = 0; queue < SCHEDULER_WAIT_QUEUE_COUNT; ++queue) {
        queue_init(&wait_queues[queue]);
    }
    running = NULL;
    process_interrupt_frame_valid = false;
}

void scheduler_register_process(struct ProcessControlBlock *pcb) {
    if (pcb != NULL) {
        queue_push(&ready_queue, pcb);
    }
}

__attribute__((noreturn)) void scheduler_start(void) {
    struct ProcessControlBlock *next = NULL;
    if (queue_pop(&ready_queue, &next) != 0) {
        halt_cpu();
    }

    switch_to(next);
    __builtin_unreachable();
}

bool scheduler_tick(void) {
    if (!process_interrupt_frame_valid || running == NULL) {
        return false;
    }

    // The ISR already saved the interrupted frame into `running`.
    running->metadata.state = PROCESS_STATE_READY;
    if (queue_push(&ready_queue, running) != 0) {
        running->metadata.state = PROCESS_STATE_RUNNING;
        return false;
    }

    struct ProcessControlBlock *next = NULL;
    if (queue_pop(&ready_queue, &next) != 0) {
        running->metadata.state = PROCESS_STATE_RUNNING;
        return false;
    }

    if (next == running) {
        // Still the highest-priority runnable process; no switch needed.
        running->metadata.state = PROCESS_STATE_RUNNING;
        return false;
    }

    switch_to(next);
    return true;
}

bool scheduler_block_current(uint32_t wait_queue) {
    if (wait_queue >= SCHEDULER_WAIT_QUEUE_COUNT || running == NULL ||
        queue_is_empty(&ready_queue)) {
        return false;
    }

    struct ProcessControlBlock *blocked = running;
    if (queue_push(&wait_queues[wait_queue], blocked) != 0) {
        return false;
    }

    blocked->metadata.state = PROCESS_STATE_BLOCKED;

    struct ProcessControlBlock *next = NULL;
    if (queue_pop(&ready_queue, &next) != 0) {
        queue_remove(&wait_queues[wait_queue], blocked);
        blocked->metadata.state = PROCESS_STATE_RUNNING;
        return false;
    }

    switch_to(next);
    return true;
}

void scheduler_wake_waiting(uint32_t wait_queue) {
    wake_one_waiting(wait_queue);
}

__attribute__((noreturn)) void scheduler_terminate_current(void) {
    if (running == NULL) {
        halt_cpu();
    }

    uint32_t pid = running->metadata.pid;
    queue_remove(&ready_queue, running);
    for (uint32_t queue = 0; queue < SCHEDULER_WAIT_QUEUE_COUNT; ++queue) {
        queue_remove(&wait_queues[queue], running);
    }
    running = NULL;

    // process_destroy() switches CR3 back to the kernel directory when it
    // destroys the running process; switch_to() then loads the next one.
    process_destroy(pid);

    // A dying process may be the only reason the remaining ones are blocked.
    for (uint32_t queue = 0; queue < SCHEDULER_WAIT_QUEUE_COUNT; ++queue) {
        wake_all_waiting(queue);
    }

    struct ProcessControlBlock *next = NULL;
    if (queue_pop(&ready_queue, &next) != 0) {
        halt_cpu();
    }

    switch_to(next);
    __builtin_unreachable();
}
