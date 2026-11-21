#ifndef _SYSCALL_H
#define _SYSCALL_H

#include "cpu/interrupt.h"

/**
 * Interrupt vector used for system calls, triggered by ring-3 code via
 * `int SYSCALL_INTERRUPT_VECTOR`. Installed with DPL=3 in initialize_idt()
 * (unlike every other vector, which is kernel-only).
 */
#define SYSCALL_INTERRUPT_VECTOR 0x30

/**
 * Syscall service numbers, passed in eax.
 * @see course spec 3.4.1.1 "Designing System Calls" for the full table.
 */
#define SYSCALL_FS_READ 0
#define SYSCALL_FS_READ_DIRECTORY 1
#define SYSCALL_FS_WRITE 2
#define SYSCALL_FS_DELETE 3
#define SYSCALL_KEYBOARD_GETCHAR 4
#define SYSCALL_TEXT_PUTCHAR 5
#define SYSCALL_TEXT_PUTS 6
#define SYSCALL_KEYBOARD_ACTIVATE 7

/**
 * Dispatches a syscall based on the trapped CPU state's eax/ebx/ecx/edx.
 * Called from main_interrupt_handler() for SYSCALL_INTERRUPT_VECTOR - not
 * meant to be called directly.
 *
 * @param frame
 * CPU state at the time of the `int 0x30`
 */
void syscall(struct InterruptFrame frame);

#endif
