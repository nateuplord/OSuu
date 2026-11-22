#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <stdbool.h>

/**
 * Round-robin scheduling point, called from the timer ISR after the current
 * process's interrupt frame has been saved. Marks the current process READY,
 * picks the next READY process, switches to its address space and kernel
 * stack and jumps into it via context_restore().
 *
 * @return false when no switch happened (single runnable process, or the
 *         interrupt did not come from ring 3); the ISR then resumes normally.
 * @note   Never returns true - context_restore() does not return.
 */
bool scheduler_tick(void);

#endif
