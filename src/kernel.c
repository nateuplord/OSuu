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
#include "text/framebuffer.h"
#include <stdbool.h>
#include <stdint.h>

#define USER_PROGRAM_VIRTUAL_ADDR ((uint8_t *) 0)
#define USER_STACK_PAGE_VIRTUAL_ADDR ((uint8_t *) 0x3FF000)

/** Report a setup failure on the framebuffer instead of jumping into garbage. */
static void halt_with_error(int8_t code) {
    framebuffer_write(0, 0, 'E', 0xC, 0);
    framebuffer_write(0, 1, (char) ('0' + (code & 0xF)), 0xC, 0);
    framebuffer_set_cursor(0, 2);
    while (true) {
        __asm__ volatile("hlt");
    }
}

void kernel_setup(struct Multiboot2Info *mb_info) {
    load_gdt(&gdtr);
    gdt_install_tss();
    set_tss_register();
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

    // USER_PROGRAM_VIRTUAL_ADDR/USER_STACK_PAGE_VIRTUAL_ADDR both fall inside
    // the kernel's own statically identity-mapped low region (needed by the
    // VBE real-mode trampoline, but only for addresses around 0x7000-0x9000 -
    // nowhere near these two). pg_alloc_user_frame refuses to allocate over
    // an already-present PTE, so clear just these two entries first, now
    // that vbe_set_mode()'s real-mode work is done and doesn't need them.
    struct PgTableEntryFlags cleared_flags = {0};
    pg_update_entry(
        &_pg_kernel_pg_dir, 0, USER_PROGRAM_VIRTUAL_ADDR, cleared_flags
    );
    pg_update_entry(
        &_pg_kernel_pg_dir, 0, USER_STACK_PAGE_VIRTUAL_ADDR, cleared_flags
    );

    // Code page and stack page must both be reachable from ring 3.
    if (!pg_alloc_user_frame(&_pg_kernel_pg_dir, USER_PROGRAM_VIRTUAL_ADDR) ||
        !pg_alloc_user_frame(
            &_pg_kernel_pg_dir, USER_STACK_PAGE_VIRTUAL_ADDR
        )) {
        halt_with_error(-1);
    }

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
