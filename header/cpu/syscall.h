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
 * Beyond this point, these are our own additions on top of the official
 * table above - needed because the real shell (ported into userland/)
 * reaches directly into ext2/framebuffer internals rather than just the
 * four high-level FS calls, same as it already did in ring 0.
 */
#define SYSCALL_FS_GET_NODE 8
#define SYSCALL_FS_SYNC_NODE 9
#define SYSCALL_FS_READ_BLOCKS 10
#define SYSCALL_FS_WRITE_BLOCKS 11
#define SYSCALL_TEXT_WRITE_AT 12
#define SYSCALL_TEXT_SET_CURSOR 13
#define SYSCALL_TEXT_SCROLL_UP 14
#define SYSCALL_PLAY_BADAPPLE 15
#define SYSCALL_TEXT_CLEAR 16

/** Process management additions used by the multitasking steps. */
#define SYSCALL_PROCESS_EXIT 17
#define SYSCALL_PROCESS_GETPID 18
#define SYSCALL_PROCESS_YIELD 19
#define SYSCALL_PROCESS_EXIT_ALL 20
#define SYSCALL_PROCESS_EXEC 21
#define SYSCALL_PROCESS_LIST 22
#define SYSCALL_PROCESS_KILL 23
#define SYSCALL_PROCESS_CREATE 24
#define SYSCALL_CMOS_READ 25
#define SYSCALL_CMOS_TICKS 26
#define SYSCALL_KEYBOARD_POLL 27
#define SYSCALL_GUI_FILL_RECT 28

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
