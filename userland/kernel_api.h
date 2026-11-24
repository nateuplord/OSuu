#ifndef _USER_KERNEL_API_H
#define _USER_KERNEL_API_H

/**
 * Wrappers around the kernel's syscall() trap, with signatures matching
 * header/fs/ext2.h, header/driver/disk.h, header/text/framebuffer.h and
 * header/driver/keyboard.h exactly, so shell.c's calls don't need to change.
 */

#include "driver/cmos.h"
#include "driver/pit.h"
#include "fs_types.h"
#include "gui/gui.h"
#include "process/process_info.h"
#include <stdint.h>

#define FRAMEBUFFER_WIDTH 80
#define FRAMEBUFFER_HEIGHT 60

#define KEYBOARD_KEY_UP 0x1C
#define KEYBOARD_KEY_DOWN 0x1D
#define KEYBOARD_KEY_LEFT 0x1E
#define KEYBOARD_KEY_RIGHT 0x1F

int8_t read(struct EXT2DriverRequest request);
int8_t read_directory(struct EXT2DriverRequest *request);
int8_t write(struct EXT2DriverRequest *request);
int8_t delete(struct EXT2DriverRequest request);

void get_node(
    struct EXT2Inode *node,
    uint32_t inode
);
void sync_node(
    struct EXT2Inode *node,
    uint32_t inode
);
void read_blocks(
    void *ptr,
    uint32_t logical_block_address,
    uint8_t block_count
);
void write_blocks(
    const void *ptr,
    uint32_t logical_block_address,
    uint8_t block_count
);

void framebuffer_write(
    uint8_t row,
    uint8_t col,
    char c,
    uint8_t fg,
    uint8_t bg
);
void framebuffer_putchar(
    char c,
    uint8_t attribute
);
void framebuffer_puts(
    const char *buf,
    uint32_t count,
    uint8_t attribute
);
void framebuffer_set_cursor(
    uint8_t r,
    uint8_t c
);
void framebuffer_scroll_up(void);
void framebuffer_clear(void);

/** Built-in GUI primitives backed by the linear framebuffer. */
void gui_fill_rect(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    uint8_t color
);

void play_bad_apple(
    uint16_t offset_x,
    uint16_t offset_y
);

void get_keyboard_buffer(char *buf);
void poll_keyboard_buffer(char *buf);
void keyboard_state_activate(void);

/** Process management - backed by int 0x30 services 17-24. */
__attribute__((noreturn)) void exit_process(void);
__attribute__((noreturn)) void exit_all_processes(void);
uint32_t getpid(void);
void yield(void);

/**
 * Replace the current process image. Returns 0 only on failure; success never
 * returns to this instruction stream.
 */
int32_t exec_process(struct EXT2DriverRequest request);
uint32_t process_list(
    struct ProcessInfo *buffer,
    uint32_t capacity
);

/** Returns 0 on success, 1 when no process has that PID. */
int32_t kill_process(uint32_t pid);

/**
 * Spawn a new process from a root-directory executable. `priority` 0 selects
 * the kernel default. On success `pid` receives the new process ID.
 */
int32_t spawn_process(
    const char *name,
    uint8_t priority,
    uint32_t *pid
);

/** CMOS/RTC wrappers - backed by int 0x30 services 25-26. */
/** cmos_read_datetime() and cmos_get_periodic_ticks() come from driver/cmos.h. */

/** PIT tick counter wrapper - backed by int 0x30 service 29. */
/** pit_get_ticks() comes from driver/pit.h. */

#endif
