#include "cpu/syscall.h"
#include "driver/keyboard.h"
#include "fs/ext2.h"
#include "misc/badapple.h"
#include "process/process.h"
#include "process/scheduler.h"
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
                delete(*(struct EXT2DriverRequest *) frame.cpu.general.ebx);
            break;

        case SYSCALL_KEYBOARD_GETCHAR: {
            char *buffer = (char *) frame.cpu.general.ebx;
            get_keyboard_buffer(buffer);

            // No key yet: let another runnable process use the CPU. If every
            // process is waiting, scheduler_block_current() returns false and
            // this call falls back to the old nonblocking behaviour.
            if (*buffer == 0) {
                scheduler_block_current(SCHEDULER_WAIT_KEYBOARD);
            }
            break;
        }

        case SYSCALL_TEXT_PUTCHAR:
            putchar(
                (char) frame.cpu.general.ebx, (uint8_t) frame.cpu.general.ecx
            );
            break;

        case SYSCALL_TEXT_PUTS:
            puts(
                (const char *) frame.cpu.general.ebx, frame.cpu.general.ecx,
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
                (void *) frame.cpu.general.ebx, frame.cpu.general.ecx,
                (uint8_t) frame.cpu.general.edx
            );
            break;

        case SYSCALL_FS_WRITE_BLOCKS:
            write_blocks(
                (const void *) frame.cpu.general.ebx, frame.cpu.general.ecx,
                (uint8_t) frame.cpu.general.edx
            );
            break;

        case SYSCALL_TEXT_WRITE_AT:
            // Every real call site uses white-on-black, so that's hardcoded
            // here rather than spending a 4th register on fg/bg.
            framebuffer_write(
                (uint8_t) frame.cpu.general.ebx,
                (uint8_t) frame.cpu.general.ecx, (char) frame.cpu.general.edx,
                0xF, 0
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

        case SYSCALL_PLAY_BADAPPLE:
            // Blocks in ring 0 until playback finishes or is interrupted by
            // a keypress - already self-contained (its own read()/PIT
            // pacing/keyboard poll), so no extra syscall surface is needed.
            play_bad_apple(
                (uint16_t) frame.cpu.general.ebx,
                (uint16_t) frame.cpu.general.ecx
            );
            break;

        case SYSCALL_TEXT_CLEAR:
            framebuffer_clear();
            break;

        case SYSCALL_PROCESS_EXIT:
            scheduler_terminate_current();
            break;

        case SYSCALL_PROCESS_GETPID: {
            struct ProcessControlBlock *pcb =
                process_get_current_running_pcb_pointer();
            *(uint32_t *) frame.cpu.general.ebx =
                pcb == NULL ? 0 : pcb->metadata.pid;
            break;
        }

        case SYSCALL_PROCESS_YIELD:
            scheduler_tick();
            break;

        case SYSCALL_PROCESS_EXIT_ALL:
            scheduler_terminate_all();
            break;

        case SYSCALL_PROCESS_EXEC: {
            // On success process_exec_current() iret's into the new image and
            // never reaches the status write.
            int32_t result = process_exec_current(
                *(struct EXT2DriverRequest *) frame.cpu.general.ebx
            );
            *(int32_t *) frame.cpu.general.ecx = result;
            break;
        }

        default:
            break;
    }
}
