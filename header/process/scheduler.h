#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

struct ProcessControlBlock;

/// Wait queues the scheduler knows about. Keyboard is the only device with a
/// blocking syscall today; the remaining slots follow the same IRQ-index model
/// so more devices can be added without changing the scheduler API.
#define SCHEDULER_WAIT_KEYBOARD 0u
#define SCHEDULER_WAIT_QUEUE_COUNT 16u

/**
 * Clear scheduler bookkeeping. Call before creating the first process.
 */
void scheduler_init(void);

/**
 * Add a freshly created READY process to the scheduler's priority queue.
 * Called by process_create_user_process() after the PCB is fully initialized.
 */
void scheduler_register_process(struct ProcessControlBlock *pcb);

/**
 * Remove a process from all scheduler queues. Called before a PCB is destroyed
 * so stale pointers cannot survive in the ready or wait queues.
 */
void scheduler_unregister_process(const struct ProcessControlBlock *pcb);

/**
 * Get the process currently owned by the scheduler, or NULL if none is running.
 */
struct ProcessControlBlock *scheduler_get_running_process(void);

/**
 * Pop the highest-priority READY process and dispatch it. Never returns.
 */
__attribute__((noreturn)) void scheduler_start(void);

/**
 * Priority / round-robin scheduling point, called from the timer ISR after
 * the current process's interrupt frame has been saved. Requeues the current
 * process and switches to the next runnable process.
 *
 * @return false when no switch happened (same process stayed highest priority,
 *         no other runnable process, or the interrupt did not come from ring 3);
 *         the ISR then resumes normally.
 * @note   Never returns true - context_restore() does not return.
 */
bool scheduler_tick(void);

/**
 * Move the running process to a wait queue and switch to the highest-priority
 * remaining READY process. If nothing else can run, the process stays RUNNING
 * and this returns false so callers can fall back to nonblocking behaviour.
 *
 * @note Never returns true - context_restore() does not return on success.
 */
bool scheduler_block_current(uint32_t wait_queue);

/**
 * Move one blocked process from a wait queue back to READY. Called from a
 * device ISR; does not switch context itself.
 */
void scheduler_wake_waiting(uint32_t wait_queue);

/**
 * Destroy the running process and switch to the next READY process. If no
 * process remains, halts the CPU. Never returns.
 */
__attribute__((noreturn)) void scheduler_terminate_current(void);

/**
 * Destroy every registered process and halt. Used when the shell exits so a
 * second shell instance cannot keep running after the session is over.
 */
__attribute__((noreturn)) void scheduler_terminate_all(void);

/**
 * Terminate a process by PID. If the PID belongs to the running process this
 * never returns; otherwise the caller continues and the target is unlinked
 * from all queues and destroyed.
 *
 * @return true if a matching active process was found and destroyed.
 */
bool scheduler_terminate_pid(uint32_t pid);

#endif
