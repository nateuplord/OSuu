#include "boot/kernel-entrypoint.h"
#include "boot/multiboot.h"
#include "boot/vbe.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/interrupt.h"
#include "driver/keyboard.h"
#include "driver/pit.h"
#include "fs/ext2.h"
#include "memory/paging.h"
#include "memory/swap.h"
#include "misc/badapple.h"
#include "shell/shell.h"
#include "text/framebuffer.h"
#include <stdbool.h>
#include <stdint.h>

static uint8_t row = 0;
static uint8_t col = 0;

static void new_line(void) {
    ++row;
    col = 0;
}

void kernel_setup(struct Multiboot2Info *mb_info) {
    load_gdt(&gdtr);
    pic_remap();
    pit_init(100);
    initialize_filesystem_ext2();
    swap_init();
    initialize_idt();

    (void)
        mb_info; // no longer used - video mode is set directly via VBE, not read from multiboot

    struct VbeModeInfo vbe_info;
    if (!vbe_set_mode(0x101, &vbe_info)) {
        // if fails, then display white E on red bg
        volatile uint16_t *vga = (volatile uint16_t *) 0xB8000;
        vga[0] = (0x4F << 8) | 'E';
        return;
    }

    framebuffer_init(&vbe_info);

    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    keyboard_state_activate();
    start_shell();
    shell_get_cursor_position(&row, &col);

    while (true) {
        char c;
        get_keyboard_buffer(&c);
        if (!c) {
            continue;
        }

        switch (c) {
            case '\n':
                new_line();
                enter_command();
                shell_get_cursor_position(&row, &col);
                break;
            case '\b':
                remove_char();
                shell_get_cursor_position(&row, &col);
                break;
            case KEYBOARD_KEY_LEFT:
                shell_cursor_left(&row, &col);
                break;
            case KEYBOARD_KEY_RIGHT:
                shell_cursor_right(&row, &col);
                break;
            case KEYBOARD_KEY_UP:
                shell_history_up(&row, &col);
                break;
            case KEYBOARD_KEY_DOWN:
                shell_history_down(&row, &col);
                break;
            default:
                framebuffer_write(row, col, c, 0xF, 0);
                ++col;

                if (col >= FRAMEBUFFER_WIDTH) {
                    new_line();
                }
                add_char(c);
                break;
        }
        framebuffer_set_cursor(row, col);
    }
}
