#include "cpu/scheduling.h"
#include <stddef.h>

static Queue ready;
static Queue waiting;
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
    if (q->buffer[q->head] != NULL) {
        if ((q->head+1)%QUEUE_CAPACITY == q->tail) return 1;
        else q->head = (q->head+1) % QUEUE_CAPACITY;
    }
    q->buffer[q->head] = block;
    return 0;
}

int pop(Queue *q, PCB *block) {
    if (q->buffer[q->tail] == NULL) return 1;
    *block = *q->buffer[q->tail];
    q->buffer[q->tail] = NULL;
    if (q->tail != q->head) {
        q->tail = (q->tail+1) % QUEUE_CAPACITY;
    }
    return 0;
}

int start_scheduling() {
    ready = make_queue();
    waiting = make_queue();
    running = NULL;
    return 0;
}

int queue_to_queue(Queue *q1, Queue *q2) {
    PCB *block;
    if (pop(q1, block)) return 1;
    if (push(q2, block)) return 1;
    return 0;
}