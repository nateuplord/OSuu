#include "boot/kernel-entrypoint.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/interrupt.h"
#include "driver/keyboard.h"
#include "fs/ext2.h"
#include "memory/paging.h"
#include "memory/swap.h"
#include "text/framebuffer.h"
#include <stdbool.h>
#include <stdint.h>

#define USER_PROGRAM_VIRTUAL_ADDR ((uint8_t *) 0)
#define USER_STACK_PAGE_VIRTUAL_ADDR ((uint8_t *) 0x3FF000)
#define VGA_TEXT_VIRTUAL_ADDR ((uint8_t *) 0xB8000)

/** Report a setup failure on the framebuffer instead of jumping into garbage. */
static void halt_with_error(int8_t code) {
    framebuffer_write(0, 0, 'E', 0xC, 0);
    framebuffer_write(0, 1, (char) ('0' + (code & 0xF)), 0xC, 0);
    framebuffer_set_cursor(0, 2);
    while (true) {
        __asm__ volatile("hlt");
    }
}

void kernel_setup(void) {
    load_gdt(&gdtr);
    gdt_install_tss();
    set_tss_register();
    pic_remap();
    initialize_filesystem_ext2();
    swap_init();
    initialize_idt();

    activate_keyboard_interrupt();
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    keyboard_state_activate();

    // Code page, stack page and VGA text buffer must all be reachable from ring 3.
    if (!pg_alloc_user_frame(&_pg_kernel_pg_dir, USER_PROGRAM_VIRTUAL_ADDR) ||
        !pg_alloc_user_frame(
            &_pg_kernel_pg_dir, USER_STACK_PAGE_VIRTUAL_ADDR
        )) {
        halt_with_error(-1);
    }

    struct PgTableEntryFlags user_flags = {
        .present = true, .read_write = true, .user_supervisor = true
    };
    pg_update_entry(
        &_pg_kernel_pg_dir, (void *) 0xB8000, VGA_TEXT_VIRTUAL_ADDR, user_flags
    );

    struct EXT2DriverRequest request = {
        .buf = USER_PROGRAM_VIRTUAL_ADDR,
        .name = "shell",
        .parent_inode = 1,
        .buffer_size = 0x100000,
        .name_len = 5,
    };
    int8_t ret = read(request);
    if (ret != 0) {
        halt_with_error(ret);
    }

    set_tss_kernel_current_stack();
    kernel_execute_user_program(USER_PROGRAM_VIRTUAL_ADDR);
}
