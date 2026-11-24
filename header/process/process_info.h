#ifndef _PROCESS_INFO_H
#define _PROCESS_INFO_H

#include <stdint.h>

#define PROCESS_NAME_LENGTH_MAX 32
#define PROCESS_COUNT_MAX 16

typedef enum PROCESS_STATE {
    PROCESS_STATE_INACTIVE,
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
} PROCESS_STATE;

/**
 * Kernel-to-userland snapshot of one process. Kept separate from
 * ProcessControlBlock so userland never sees kernel-only pointers or context
 * state.
 */
struct ProcessInfo {
    uint32_t pid;
    uint8_t state;
    uint8_t priority;
    char name[PROCESS_NAME_LENGTH_MAX];
};

/** Result of a process-create syscall: status plus the new process PID. */
struct ProcessCreateResult {
    int32_t status;
    uint32_t pid;
};

#endif
