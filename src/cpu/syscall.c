#include "cpu/syscall.h"
#include "driver/keyboard.h"
#include "fs/ext2.h"
#include "text/framebuffer.h"
#include <stdint.h>

void syscall(struct InterruptFrame frame) {
    switch (frame.cpu.general.eax) {
        case SYSCALL_FS_READ:
            // read() takes its request by value - dereference ebx's pointer.
            *(int8_t *) frame.cpu.general.ecx =
                read(*(struct EXT2DriverRequest *) frame.cpu.general.ebx);
            break;

        case SYSCALL_FS_READ_DIRECTORY:
            // read_directory() already takes a pointer - pass ebx straight through.
            *(int8_t *) frame.cpu.general.ecx = read_directory(
                (struct EXT2DriverRequest *) frame.cpu.general.ebx
            );
            break;

        case SYSCALL_FS_WRITE:
            *(int8_t *) frame.cpu.general.ecx =
                write((struct EXT2DriverRequest *) frame.cpu.general.ebx);
            break;

        case SYSCALL_FS_DELETE:
            // delete() takes its request by value, same as read().
            *(int8_t *) frame.cpu.general.ecx =
                delete (*(struct EXT2DriverRequest *) frame.cpu.general.ebx);
            break;

        case SYSCALL_KEYBOARD_GETCHAR:
            get_keyboard_buffer((char *) frame.cpu.general.ebx);
            break;

        case SYSCALL_TEXT_PUTCHAR:
            putchar(
                (char) frame.cpu.general.ebx, (uint8_t) frame.cpu.general.ecx
            );
            break;

        case SYSCALL_TEXT_PUTS:
            puts(
                (const char *) frame.cpu.general.ebx,
                frame.cpu.general.ecx,
                (uint8_t) frame.cpu.general.edx
            );
            break;

        case SYSCALL_KEYBOARD_ACTIVATE:
            keyboard_state_activate();
            break;

        default:
            break;
    }
}
