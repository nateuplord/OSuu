#include "cpu/scheduling.h"
#include "cpu/interrupt.h"
#include <stddef.h>
#include <stdint.h>

static Queue ready;
static Queue waiting[MAX_IO_DEVICES];
static PCB *running = NULL;

Queue make_queue() {
    Queue q;
    for (int i=0; i<QUEUE_CAPACITY; i++) {
        q.buffer[i] = NULL;
    }
    q.head = 0;
    q.tail = 0;
    return q;
}

int push(Queue *q, PCB *block) {
    if (block == NULL || queue_length(q) == QUEUE_CAPACITY) return 1;
    if (q->buffer[q->head] != NULL) {
        q->head = (q->head + 1) % QUEUE_CAPACITY;
    }
    q->buffer[q->head] = block;
    return 0;
}

int pop(Queue *q, PCB **block) {
    if (block == NULL || queue_length(q) == 0) return 1;
    *block = q->buffer[q->tail];
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

void scheduler_init(void) {
    ready = make_queue();
    for (int i=0; i<MAX_IO_DEVICES; i++) {
        waiting[i] = make_queue();
    }
    running = NULL;
    activate_timer_interrupt();
}

int queue_to_queue(Queue *q1, Queue *q2) {
    PCB *block = NULL;
    if (pop(q1, &block)) return 1;
    if (push(q2, block)) {
        (void) push(q1, block);
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