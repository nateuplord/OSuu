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
#include "text/framebuffer.h"
#include <stdbool.h>
#include <stdint.h>

static uint8_t row = 0;
static uint8_t col = 0;
static uint8_t line_length[FRAMEBUFFER_HEIGHT];

static void new_line(void) {
    line_length[row] = col;
    ++row;
    col = 0;
}

static void handle_backspace(void) {
    if (col > 0) {
        --col;
        framebuffer_write(row, col, ' ', 0xF, 0);
    } else if (row > 0) {
        --row;
        col = line_length[row];
        if (col == FRAMEBUFFER_WIDTH) {
            --col;
            framebuffer_write(row, col, ' ', 0xF, 0);
        }
    }
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
    play_bad_apple(288, 216);

    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    keyboard_state_activate();

    while (true) {
        char c;
        get_keyboard_buffer(&c);
        if (!c) {
            continue;
        }

        switch (c) {
            case '\n':
                new_line();
                break;
            case '\b':
                handle_backspace();
                break;
            default:
                framebuffer_write(row, col, c, 0xF, 0);
                ++col;

                if (col >= FRAMEBUFFER_WIDTH) {
                    new_line();
                }
                break;
        }
        framebuffer_set_cursor(row, col);
    }
}
