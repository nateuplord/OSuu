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
#include "process/process.h"
#include <stdbool.h>
#include <stdint.h>

#define USER_PROGRAM_VIRTUAL_ADDR ((uint8_t *) 0)
#define USER_PROGRAM_MAX_PAGES                                                 \
    256 // 1 MiB, matching linker.ld's program size cap
#define USER_STACK_PAGE_VIRTUAL_ADDR ((uint8_t *) 0x3FF000)
#define USER_STACK_PAGES 16 // 64 KiB, growing down from 0x400000

/** Report a setup failure on the framebuffer instead of jumping into garbage. */
static void halt_with_error(int8_t code) {
    framebuffer_write(0, 0, 'E', 0xC, 0);
    framebuffer_write(0, 1, (char) ('0' + (code & 0xF)), 0xC, 0);
    framebuffer_set_cursor(0, 2);
    while (true) {
        __asm__ volatile("hlt");
    }
}

static bool make_user_accessible(
    uint8_t *start_addr,
    uint32_t page_count
) {
    struct PgTableEntryFlags cleared_flags = {0};
    for (uint32_t i = 0; i < page_count; i++) {
        uint8_t *addr = start_addr + i * 0x1000;
        pg_update_entry(&_pg_kernel_pg_dir, 0, addr, cleared_flags);
        if (!pg_alloc_user_frame(&_pg_kernel_pg_dir, addr)) {
            return false;
        }
    }
    return true;
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

    if (!make_user_accessible(
            USER_PROGRAM_VIRTUAL_ADDR, USER_PROGRAM_MAX_PAGES
        ) ||
        !make_user_accessible(
            USER_STACK_PAGE_VIRTUAL_ADDR - (USER_STACK_PAGES - 1) * 0x1000,
            USER_STACK_PAGES
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
