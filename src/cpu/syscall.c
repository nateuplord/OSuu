#include "cpu/syscall.h"
#include "driver/cmos.h"
#include "driver/keyboard.h"
#include "driver/pit.h"
#include "fs/ext2.h"
#include "gui/gui.h"
#include "misc/badapple.h"
#include "process/process.h"
#include "process/scheduler.h"
#include "stdlib/string.h"
#include "text/framebuffer.h"
#include <stdint.h>

/**
 * True if `[addr, addr + len)` lies wholly in user space. Bulk-copy syscalls
 * (palette, blit) check this so a process can't point the kernel at kernel
 * memory and have it copied to the screen.
 */
static bool user_range_ok(uint32_t addr, uint32_t len) {
    return addr + len >= addr && addr + len <= KERNEL_VIRTUAL_ADDRESS_BASE;
}

/**
 * Syscalls that draw on the shared text/graphics screen. While a process owns
 * the screen (full-screen mode via KEYBOARD_SET_RAW), these are dropped for
 * every *other* process - e.g. the background clock - so nothing scribbles
 * over the owner's frame. Dropped, not queued: background drawers redraw on
 * their own schedule, and the clock catches up within a second of release.
 * Kernel-internal drawing isn't affected; only this syscall path is gated.
 */
static bool syscall_draws_on_screen(uint32_t number) {
    switch (number) {
        case SYSCALL_TEXT_PUTCHAR:
        case SYSCALL_TEXT_PUTS:
        case SYSCALL_TEXT_WRITE_AT:
        case SYSCALL_TEXT_SET_CURSOR:
        case SYSCALL_TEXT_SCROLL_UP:
        case SYSCALL_TEXT_CLEAR:
        case SYSCALL_GUI_FILL_RECT:
        case SYSCALL_PLAY_BADAPPLE:
            return true;
        default:
            return false;
    }
}

void syscall(struct InterruptFrame frame) {
    uint32_t screen_owner = framebuffer_get_owner();
    if (screen_owner != 0 && syscall_draws_on_screen(frame.cpu.general.eax)) {
        struct ProcessControlBlock *caller =
            process_get_current_running_pcb_pointer();
        if (caller == NULL || caller->metadata.pid != screen_owner) {
            return;
        }
    }

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

        case SYSCALL_KEYBOARD_POLL:
            // Same buffer read as GETCHAR, but never blocks the caller.
            get_keyboard_buffer((char *) frame.cpu.general.ebx);
            break;

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

        case SYSCALL_GUI_FILL_RECT: {
            const struct GuiRect *rect =
                (const struct GuiRect *) frame.cpu.general.ebx;
            framebuffer_fill_rect(
                rect->x, rect->y, rect->width, rect->height, rect->color
            );
            break;
        }

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

        case SYSCALL_PROCESS_LIST:
            *(uint32_t *) frame.cpu.general.ebx = process_list_snapshot(
                (struct ProcessInfo *) frame.cpu.general.ecx,
                frame.cpu.general.edx
            );
            break;

        case SYSCALL_PROCESS_KILL: {
            bool killed = scheduler_terminate_pid(frame.cpu.general.ebx);
            *(int32_t *) frame.cpu.general.ecx = killed ? 0 : 1;
            break;
        }

        case SYSCALL_PROCESS_CREATE: {
            struct EXT2DriverRequest *user_request =
                (struct EXT2DriverRequest *) frame.cpu.general.ebx;
            struct ProcessCreateResult *result =
                (struct ProcessCreateResult *) frame.cpu.general.edx;
            struct EXT2DriverRequest kernel_request;
            char name[UINT8_MAX + 1];

            result->status = PROCESS_CREATE_FAIL_INVALID_NAME;
            result->pid = 0;
            memcpy(&kernel_request, user_request, sizeof(kernel_request));
            if (kernel_request.name == NULL || kernel_request.name_len == 0) {
                break;
            }

            // process_create_user_process() switches CR3 before reading the
            // executable, so the filename must live in kernel memory first.
            memcpy(name, kernel_request.name, kernel_request.name_len);
            name[kernel_request.name_len] = '\0';
            kernel_request.name = name;
            kernel_request.buf = NULL;

            result->status = process_create_user_process(
                kernel_request, (uint8_t) frame.cpu.general.ecx, &result->pid
            );
            if (result->status != PROCESS_CREATE_SUCCESS) {
                result->pid = 0;
            }
            break;
        }

        case SYSCALL_CMOS_READ:
            *(bool *) frame.cpu.general.ecx = cmos_read_datetime(
                (struct CmosDateTime *) frame.cpu.general.ebx
            );
            break;

        case SYSCALL_CMOS_TICKS:
            *(uint32_t *) frame.cpu.general.ebx = cmos_get_periodic_ticks();
            break;

        case SYSCALL_PIT_TICKS:
            *(uint32_t *) frame.cpu.general.ebx = pit_get_ticks();
            break;

        case SYSCALL_KEYBOARD_GET_EVENT: {
            // ebx = struct KeyEvent *out, ecx = int32_t *ok (1 = popped)
            struct ProcessControlBlock *pcb =
                process_get_current_running_pcb_pointer();
            uint32_t pid = pcb == NULL ? 0 : pcb->metadata.pid;
            *(int32_t *) frame.cpu.general.ecx = keyboard_pop_event(
                (struct KeyEvent *) frame.cpu.general.ebx, pid
            ) ? 1 : 0;
            break;
        }

        case SYSCALL_MEMORY_MAP_REGION: {
            // ebx = void *start, ecx = uint32_t size, edx = int32_t *ok
            bool ok = process_map_region(
                (void *) frame.cpu.general.ebx, frame.cpu.general.ecx
            );
            if (frame.cpu.general.edx != 0) {
                *(int32_t *) frame.cpu.general.edx = ok ? 1 : 0;
            }
            break;
        }

        case SYSCALL_KEYBOARD_SET_RAW: {
            // ebx = bool raw, ecx = int32_t *ok (1 = applied)
            struct ProcessControlBlock *pcb =
                process_get_current_running_pcb_pointer();
            uint32_t pid = pcb == NULL ? 0 : pcb->metadata.pid;
            // Raw mode = full-screen mode: the keyboard and the screen are
            // taken (and given back) together, all or nothing.
            bool ok = false;
            if (pid != 0 && frame.cpu.general.ebx != 0) {
                if (framebuffer_claim(pid)) {
                    ok = keyboard_set_raw(true, pid);
                    if (!ok) {
                        framebuffer_release(pid);
                    }
                }
            } else if (pid != 0) {
                ok = keyboard_set_raw(false, pid);
                framebuffer_release(pid); // also restores the palette
            }
            if (frame.cpu.general.ecx != 0) {
                *(int32_t *) frame.cpu.general.ecx = ok ? 1 : 0;
            }
            break;
        }

        case SYSCALL_VIDEO_SET_PALETTE: {
            // ebx = const uint8_t rgb[768], ecx = int32_t *ok
            struct ProcessControlBlock *pcb =
                process_get_current_running_pcb_pointer();
            uint32_t pid = pcb == NULL ? 0 : pcb->metadata.pid;
            uint32_t rgb = frame.cpu.general.ebx;
            bool ok = pid != 0 && framebuffer_get_owner() == pid &&
                      user_range_ok(rgb, FRAMEBUFFER_PALETTE_BYTES);
            if (ok) {
                framebuffer_set_palette((const uint8_t *) rgb);
            }
            if (frame.cpu.general.ecx != 0) {
                *(int32_t *) frame.cpu.general.ecx = ok ? 1 : 0;
            }
            break;
        }

        case SYSCALL_VIDEO_BLIT: {
            // ebx = const uint8_t *buf, ecx = width, edx = height
            struct ProcessControlBlock *pcb =
                process_get_current_running_pcb_pointer();
            uint32_t pid = pcb == NULL ? 0 : pcb->metadata.pid;
            uint32_t buf = frame.cpu.general.ebx;
            uint32_t width = frame.cpu.general.ecx;
            uint32_t height = frame.cpu.general.edx;
            // Width/height are bounded by framebuffer_blit(); check them
            // first so width * height can't overflow the range check.
            if (pid != 0 && framebuffer_get_owner() == pid &&
                width <= 4096 && height <= 4096 &&
                user_range_ok(buf, width * height)) {
                framebuffer_blit((const uint8_t *) buf, width, height);
            }
            break;
        }

        default:
            break;
    }
}
