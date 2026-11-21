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
    __asm__ volatile("mov %0, %%ebx" : /* <Empty> */ : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : /* <Empty> */ : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : /* <Empty> */ : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : /* <Empty> */ : "r"(eax));
    __asm__ volatile("int $0x30");
}

#endif
