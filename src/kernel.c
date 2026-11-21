#include "boot/kernel-entrypoint.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/interrupt.h"
#include "driver/keyboard.h"
#include "fs/ext2.h"
#include "memory/paging.h"
#include "memory/swap.h"
#include "text/framebuffer.h"
#include "process/process.h"
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

void kernel_setup(void) {
    load_gdt(&gdtr);
    pic_remap();
    initialize_filesystem_ext2();
    swap_init();
    initialize_idt();

    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    keyboard_state_activate();

    // process initialization
    gdt_install_tss();
    set_tss_register();
    set_tss_kernel_current_stack();

    struct EXT2DriverRequest request = {
        .buf = (uint8_t *) 0x0,
        .name = "shell",
        .parent_inode = 1,
        .buffer_size = 4 * FRAME_SIZE,
        .name_len = 5,
        .is_directory = false,
    };

    int32_t create_status = process_create_user_process(request);
    if (create_status != PROCESS_CREATE_SUCCESS) {
        while (true) {
            __asm__ volatile("hlt");
        }
    }

    _process_list[0].metadata.state = PROCESS_STATE_RUNNING;
    paging_use_page_directory(
        (struct PgDir *) _process_list[0].context.page_directory_virtual_addr
    );
    kernel_execute_user_program((void *) _process_list[0].context.eip);

    // commented out because after kernel_execute_user_program() runs, CPU never returns to kernel_setup()
    // while (true) {
    //     char c;
    //     get_keyboard_buffer(&c);
    //     if (!c) {
    //         continue;
    //     }

    //     switch (c) {
    //         case '\n':
    //             new_line();
    //             break;
    //         case '\b':
    //             handle_backspace();
    //             break;
    //         default:
    //             framebuffer_write(row, col, c, 0xF, 0);
    //             ++col;

    //             if (col >= FRAMEBUFFER_WIDTH) {
    //                 new_line();
    //             }
    //             break;
    //     }
    //     framebuffer_set_cursor(row, col);
    // }
}
