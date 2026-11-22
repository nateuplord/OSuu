#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

/// Wait queues the scheduler knows about. Keyboard is the only blocking
/// device the current syscall surface exposes.
#define SCHEDULER_WAIT_KEYBOARD 0u
#define SCHEDULER_WAIT_QUEUE_COUNT 1u

/**
 * Clear scheduler bookkeeping. Call before creating the first process.
 */
void scheduler_init(void);

/**
 * Round-robin / priority scheduling point, called from the timer ISR after
 * the current process's interrupt frame has been saved. Marks the current
 * process READY and switches to the highest-priority READY process.
 *
 * @return false when no switch happened (single runnable process, or the
 *         interrupt did not come from ring 3); the ISR then resumes normally.
 * @note   Never returns true - context_restore() does not return.
 */
bool scheduler_tick(void);

/**
 * Move the running process to a wait queue and switch to another READY
 * process. If nothing else can run, the process stays RUNNING and this
 * returns false so callers can fall back to nonblocking behaviour.
 *
 * @note Never returns true - context_restore() does not return on success.
 */
bool scheduler_block_current(uint32_t wait_queue);

/**
 * Move every process blocked on a wait queue back to READY. Called from the
 * keyboard ISR; does not switch context itself.
 */
void scheduler_wake_waiting(uint32_t wait_queue);

/**
 * Destroy the running process and switch to the next READY process. If no
 * process remains, halts the CPU. Never returns.
 */
__attribute__((noreturn)) void scheduler_terminate_current(void);

#endif
