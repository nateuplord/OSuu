#ifndef _SCHEDULING_H
#define _SCHEDULING_H

#include <stdint.h>

#define QUEUE_CAPACITY 16

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cs;
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;
    uint32_t ss;
} PCB;

typedef struct {
    PCB *buffer[QUEUE_CAPACITY];
    int head;
    int tail;
} Queue;

Queue make_queue();
int push(Queue *q, PCB *block);
int pop(Queue *q, PCB *block);

#endif
