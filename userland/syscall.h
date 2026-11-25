#ifndef _USER_SYSCALL_H
#define _USER_SYSCALL_H

#include <stdint.h>

/**
 * Syscall service numbers, passed in eax.
 * @note Must match header/cpu/syscall.h on the kernel side exactly.
 */
#define SYSCALL_FS_READ 0
#define SYSCALL_FS_READ_DIRECTORY 1
#define SYSCALL_FS_WRITE 2
#define SYSCALL_FS_DELETE 3
#define SYSCALL_KEYBOARD_GETCHAR 4
#define SYSCALL_TEXT_PUTCHAR 5
#define SYSCALL_TEXT_PUTS 6
#define SYSCALL_KEYBOARD_ACTIVATE 7

#define SYSCALL_FS_GET_NODE 8
#define SYSCALL_FS_SYNC_NODE 9
#define SYSCALL_FS_READ_BLOCKS 10
#define SYSCALL_FS_WRITE_BLOCKS 11
#define SYSCALL_TEXT_WRITE_AT 12
#define SYSCALL_TEXT_SET_CURSOR 13
#define SYSCALL_TEXT_SCROLL_UP 14
#define SYSCALL_PLAY_BADAPPLE 15
#define SYSCALL_TEXT_CLEAR 16

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
#define SYSCALL_PIT_TICKS 29
#define SYSCALL_KEYBOARD_GET_EVENT 30
#define SYSCALL_KEYBOARD_SET_RAW 31
/* 32-33 reserved: VIDEO_SET_PALETTE, VIDEO_BLIT */
#define SYSCALL_MEMORY_MAP_REGION 34

/**
 * Traps into the kernel via `int 0x30` with the given service number and up
 * to three register-passed arguments (cast to uint32_t as needed - pointers,
 * chars, whatever the service expects).
 *
 * @note
 * None of these services return a value through `syscall()` itself - the FS
 * calls write their result code through a pointer passed in ecx (ebx for
 * getchar); pass a pointer to a local variable and read it back afterward.
 *
 * @warning
 * eax is moved last, deliberately. gcc may use %eax as scratch space while
 * evaluating the other three "r" operands, so setting it any earlier risks
 * it being clobbered before the `int` instruction actually runs.
 */
static inline void syscall(
    uint32_t eax,
    uint32_t ebx,
    uint32_t ecx,
    uint32_t edx
) {
    __asm__ volatile("mov %0, %%ebx"
                     : /* <Empty> */
                     : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx"
                     : /* <Empty> */
                     : "r"(ecx));
    __asm__ volatile("mov %0, %%edx"
                     : /* <Empty> */
                     : "r"(edx));
    __asm__ volatile("mov %0, %%eax"
                     : /* <Empty> */
                     : "r"(eax));
    __asm__ volatile("int $0x30");
}

#endif
