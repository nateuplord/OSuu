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

        case SYSCALL_FS_GET_NODE:
            get_node(
                (struct EXT2Inode *) frame.cpu.general.ebx,
                frame.cpu.general.ecx
            );
            break;

        case SYSCALL_FS_SYNC_NODE:
            sync_node(
                (struct EXT2Inode *) frame.cpu.general.ebx,
                frame.cpu.general.ecx
            );
            break;

        case SYSCALL_FS_READ_BLOCKS:
            read_blocks(
                (void *) frame.cpu.general.ebx,
                frame.cpu.general.ecx,
                (uint8_t) frame.cpu.general.edx
            );
            break;

        case SYSCALL_FS_WRITE_BLOCKS:
            write_blocks(
                (const void *) frame.cpu.general.ebx,
                frame.cpu.general.ecx,
                (uint8_t) frame.cpu.general.edx
            );
            break;

        case SYSCALL_TEXT_WRITE_AT:
            // Every real call site uses white-on-black, so that's hardcoded
            // here rather than spending a 4th register on fg/bg.
            framebuffer_write(
                (uint8_t) frame.cpu.general.ebx,
                (uint8_t) frame.cpu.general.ecx,
                (char) frame.cpu.general.edx,
                0xF,
                0
            );
            break;

        case SYSCALL_TEXT_SET_CURSOR:
            framebuffer_set_cursor(
                (uint8_t) frame.cpu.general.ebx, (uint8_t) frame.cpu.general.ecx
            );
            break;

        case SYSCALL_TEXT_SCROLL_UP:
            framebuffer_scroll_up();
            break;

        default:
            break;
    }
}
