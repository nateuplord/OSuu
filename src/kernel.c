#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/interrupt.h"
#include "driver/keyboard.h"
#include "kernel-entrypoint.h"
#include "text/framebuffer.h"
#include <stdbool.h>
#include <stdint.h>

void kernel_setup(void) {
    load_gdt(&_gdt_gdtr);
    pic_remap();
    initialize_idt();
    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

// to test disk driver and filesys 2.1-2.2
    // struct BlockBuffer b;
    // for (int i = 0; i < 512; i++) b.buf[i] = i % 16;
    // write_blocks(&b, 17, 1);
    // while (true);

    int row = 0, col = 0;
    keyboard_state_activate();
    while (true) {
        char c;
        get_keyboard_buffer(&c);
        if (c) {
            framebuffer_write(row, col, c, 0xF, 0);
            if (col >= FRAMEBUFFER_WIDTH) {
                ++row;
                col = 0;
            } else {
                ++col;
            }
           framebuffer_set_cursor(row, col);
        }
    }
}
