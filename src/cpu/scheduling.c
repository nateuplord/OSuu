#include "cpu/scheduling.h"
#include <stddef.h>

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