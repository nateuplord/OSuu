#include "cpu/scheduling.h"
#include "cpu/interrupt.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static Queue ready;
static Queue waiting[MAX_IO_DEVICES];
static Process *running = NULL;

Queue make_queue() {
    Queue q;
    for (int i=0; i<QUEUE_CAPACITY; i++) {
        q.buffer[i] = NULL;
    }
    q.head = 0;
    q.tail = 0;
    return q;
}

int push(Queue *q, Process *p) {
    if (p == NULL || queue_length(q) == QUEUE_CAPACITY) return 1;
    int i = q->head;
    if (q->buffer[i] != NULL) {
        q->head = (q->head+1) % QUEUE_CAPACITY;
        bool resume = q->buffer[i]->priority > p->priority;
        while (resume) {
            q->buffer[(i+1)%QUEUE_CAPACITY] = q->buffer[i];
            i = (QUEUE_CAPACITY+i-1) % QUEUE_CAPACITY;
            resume = i != (QUEUE_CAPACITY+q->tail-1) % QUEUE_CAPACITY;
            if (resume) resume = q->buffer[i]->priority > p->priority;
        }
        i = (i+1) % QUEUE_CAPACITY;
    }
    q->buffer[i] = p;
    return 0;
}

int pop(Queue *q, Process **p) {
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
    running->block.frame = frame;
    running->block.eax = frame->cpu.general.eax;
    running->block.ebx = frame->cpu.general.ebx;
    running->block.ecx = frame->cpu.general.ecx;
    running->block.edx = frame->cpu.general.edx;
    running->block.esi = frame->cpu.index.esi;
    running->block.edi = frame->cpu.index.edi;
    running->block.ebp = frame->cpu.stack.ebp;
    /* Stack position after iret, not PUSHAD's saved-ESP slot. */
    running->block.esp = (uint64_t) frame + sizeof(*frame);
    running->block.eip = frame->int_stack.eip;
    running->block.eflags = frame->int_stack.eflags;
    running->block.cs = frame->int_stack.cs;
    running->block.ds = frame->cpu.segment.ds;
    running->block.es = frame->cpu.segment.es;
    running->block.fs = frame->cpu.segment.fs;
    running->block.gs = frame->cpu.segment.gs;
    /* This interrupt frame does not contain SS for same-privilege interrupts. */
    return 0;
}

struct InterruptFrame *cpu_load(struct InterruptFrame *current_frame) {
    if (running == NULL) return current_frame;

    struct InterruptFrame *frame = running->block.frame;
    if (frame == NULL) {
        /* First dispatch: construct the same frame the ISR stub normally restores. */
        if (running->block.esp < sizeof(*frame)) return current_frame;
        frame = (struct InterruptFrame *)(running->block.esp - sizeof(*frame));
        running->block.frame = frame;
        frame->int_number = 0;
        frame->int_stack.error_code = 0;
    }

    frame->cpu.general.eax = running->block.eax;
    frame->cpu.general.ebx = running->block.ebx;
    frame->cpu.general.ecx = running->block.ecx;
    frame->cpu.general.edx = running->block.edx;
    frame->cpu.index.esi = running->block.esi;
    frame->cpu.index.edi = running->block.edi;
    frame->cpu.stack.ebp = running->block.ebp;
    /* PUSHAD's saved ESP slot is ignored by POPAD. */
    frame->cpu.stack.esp = (uint64_t) frame + 32;
    frame->int_stack.eip = running->block.eip;
    frame->int_stack.eflags = running->block.eflags;
    frame->int_stack.cs = running->block.cs;
    frame->cpu.segment.ds = running->block.ds;
    frame->cpu.segment.es = running->block.es;
    frame->cpu.segment.fs = running->block.fs;
    frame->cpu.segment.gs = running->block.gs;
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
    Process *p = NULL;
    if (pop(q1, &p)) return 1;
    if (push(q2, p)) {
        (void) push(q1, p);
        return 1;
    }
    return 0;
}

int rr_switch() {
    if (push(&ready, running)) return 1;
    return pop(&ready, &running);
}

int io_start(uint32_t device_idx) {
    if (push(&waiting[device_idx], running)) return 1;
    return pop(&ready, &running);
}

int io_finish(uint32_t device_idx) {
    return queue_to_queue(&waiting[device_idx], &ready);
}