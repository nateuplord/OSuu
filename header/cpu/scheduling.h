#ifndef _SCHEDULING_H
#define _SCHEDULING_H

#include "cpu/interrupt.h"
#include "process/process.h"
#include <stdint.h>

#define QUEUE_CAPACITY 16
#define MAX_IO_DEVICES 16

typedef struct {
    struct ProcessControlBlock *buffer[QUEUE_CAPACITY];
    int head;
    int tail;
} Queue;

Queue make_queue(void);
int push(Queue *q, struct ProcessControlBlock *process);
int pop(Queue *q, struct ProcessControlBlock **process);
uint32_t queue_length(const Queue *q);
/* Save/restore register state through ProcessControlBlock.context. */
int cpu_save(struct InterruptFrame *frame);
struct InterruptFrame *cpu_load(struct InterruptFrame *current_frame);
// Pops from q1 and pushes to q2
int queue_to_queue(Queue *q1, Queue *q2);

void scheduler_init(void);

// Round robin task switch
int rr_switch(void);

// Moves the task process to the waiting queue
int io_start(uint32_t irq);

// Moves the process from the waiting queue to the ready queue
int io_finish(uint32_t irq);

#endif
