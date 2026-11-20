#ifndef _SCHEDULING_H
#define _SCHEDULING_H

#include "cpu/interrupt.h"
#include <stdint.h>

#define QUEUE_CAPACITY 16
#define MAX_IO_DEVICES 16

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint64_t esp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cs;
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;
    uint32_t ss;
    /* Saved interrupt frame; NULL until the process has been interrupted once. */
    struct InterruptFrame *frame;
} PCB;

typedef struct {
    PCB block;
    int priority;
} Process;

typedef struct {
    Process *buffer[QUEUE_CAPACITY];
    int head;
    int tail;
} Queue;

Queue make_queue(void);
int push(Queue *q, Process *block);
int pop(Queue *q, Process **block);
uint32_t queue_length(const Queue *q);
/* Save the active frame into the running PCB and return the next frame to restore. */
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
