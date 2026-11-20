#include "cpu/scheduling.h"
#include "cpu/interrupt.h"
#include "memory/paging.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static Queue ready;
static Queue waiting[MAX_IO_DEVICES];
static struct ProcessControlBlock *running = NULL;

Queue make_queue() {
    Queue q;
    for (int i=0; i<QUEUE_CAPACITY; i++) {
        q.buffer[i] = NULL;
    }
    q.head = 0;
    q.tail = 0;
    return q;
}

int push(Queue *q, struct ProcessControlBlock *p) {
    if (p == NULL || queue_length(q) == QUEUE_CAPACITY) return 1;
    int i = q->head;
    if (q->buffer[i] != NULL) {
        q->head = (q->head+1) % QUEUE_CAPACITY;
        bool resume = q->buffer[i]->metadata.priority > p->metadata.priority;
        while (resume) {
            q->buffer[(i+1)%QUEUE_CAPACITY] = q->buffer[i];
            i = (QUEUE_CAPACITY+i-1) % QUEUE_CAPACITY;
            resume = i != (QUEUE_CAPACITY+q->tail-1) % QUEUE_CAPACITY;
            if (resume)
                resume = q->buffer[i]->metadata.priority > p->metadata.priority;
        }
        i = (i+1) % QUEUE_CAPACITY;
    }
    q->buffer[i] = p;
    return 0;
}

int pop(Queue *q, struct ProcessControlBlock **p) {
    if (p == NULL || queue_length(q) == 0) return 1;
    *p = q->buffer[q->tail];
    q->buffer[q->tail] = NULL;
    if (q->tail != q->head) {
        q->tail = (q->tail+1) % QUEUE_CAPACITY;
    }
    return 0;
}

uint32_t queue_length(const Queue *q) {
    int len = 0;
    len = (QUEUE_CAPACITY + q->head - q->tail) % QUEUE_CAPACITY;
    if (q->buffer[q->head] != NULL) len++;
    return (uint32_t) len;
}

int cpu_save(struct InterruptFrame *frame) {
    if (frame == NULL) return 1;
    if (running == NULL) return 0;

    struct Context *context = &running->context;
    context->interrupt_frame = frame;
    context->cpu = frame->cpu;
    /* Save the stack position after iret, not PUSHAD's ignored ESP slot. */
    context->cpu.stack.esp = (uint32_t) frame + sizeof(*frame);
    context->eip = frame->int_stack.eip;
    context->cs = frame->int_stack.cs;
    context->eflags = frame->int_stack.eflags;
    context->page_directory_virtual_addr =
        (uint32_t) paging_get_current_page_directory_addr();
    return 0;
}

struct InterruptFrame *cpu_load(struct InterruptFrame *current_frame) {
    if (running == NULL) return current_frame;

    struct Context *context = &running->context;
    struct InterruptFrame *frame = context->interrupt_frame;
    bool first_dispatch = frame == NULL;
    if (frame == NULL) {
        /* First dispatch: construct the same frame the ISR stub normally restores. */
        if (context->cpu.stack.esp < sizeof(*frame)) return current_frame;
        frame = (struct InterruptFrame *)(context->cpu.stack.esp - sizeof(*frame));
        context->interrupt_frame = frame;
    }

    if (context->page_directory_virtual_addr != 0) {
        paging_use_page_directory(
            (struct PgDir *) context->page_directory_virtual_addr
        );
    }

    if (first_dispatch) {
        frame->int_number = 0;
        frame->int_stack.error_code = 0;
    }
    frame->cpu = context->cpu;
    /* POPAD ignores this slot; the stub switches ESP to frame itself. */
    frame->cpu.stack.esp = (uint32_t) frame + 32;
    frame->int_stack.eip = context->eip;
    frame->int_stack.cs = context->cs;
    frame->int_stack.eflags = context->eflags;
    return frame;
}

void scheduler_init(void) {
    ready = make_queue();
    for (int i=0; i<MAX_IO_DEVICES; i++) {
        waiting[i] = make_queue();
    }
    running = NULL;
    activate_timer_interrupt();
}

int queue_to_queue(Queue *q1, Queue *q2) {
    struct ProcessControlBlock *p = NULL;
    if (pop(q1, &p)) return 1;
    if (push(q2, p)) {
        (void) push(q1, p);
        return 1;
    }
    return 0;
}

int rr_switch() {
    if (running != NULL) {
        running->metadata.state = PROCESS_STATE_READY;
        if (push(&ready, running)) return 1;
    }
    if (pop(&ready, &running)) return 1;
    running->metadata.state = PROCESS_STATE_RUNNING;
    return 0;
}

int io_start(uint32_t device_idx) {
    if (device_idx >= MAX_IO_DEVICES || running == NULL) return 1;
    struct ProcessControlBlock *blocked = running;
    if (push(&waiting[device_idx], blocked)) return 1;
    blocked->metadata.state = PROCESS_STATE_BLOCKED;
    running = NULL;
    if (pop(&ready, &running)) return 1;
    running->metadata.state = PROCESS_STATE_RUNNING;
    return 0;
}

int io_finish(uint32_t device_idx) {
    if (device_idx >= MAX_IO_DEVICES) return 1;
    struct ProcessControlBlock *woken = NULL;
    if (pop(&waiting[device_idx], &woken)) return 1;
    if (push(&ready, woken)) {
        (void) push(&waiting[device_idx], woken);
        return 1;
    }
    woken->metadata.state = PROCESS_STATE_READY;
    return 0;
}
