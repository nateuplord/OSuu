#ifndef _USER_KERNEL_API_H
#define _USER_KERNEL_API_H

/**
 * Wrappers around the kernel's syscall() trap, with signatures matching
 * header/fs/ext2.h, header/driver/disk.h, header/text/framebuffer.h and
 * header/driver/keyboard.h exactly, so shell.c's calls don't need to change.
 */

#include "driver/cmos.h"
#include "driver/key_event.h"
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

/**
 * Milliseconds since boot, derived from pit_get_ticks() (PIT_HZ = 100, so
 * 10 ms resolution). Wraps after ~49 days, so compare with subtraction
 * (`now - start >= ms`).
 */
uint32_t get_ticks_ms(void);

/**
 * usleep()-style wait: yields to other processes until at least `ms`
 * milliseconds have passed. Rounds up to the 10 ms PIT resolution.
 */
void sleep_ms(uint32_t ms);

/**
 * Raw keyboard mode - backed by int 0x30 services 30-31.
 *
 * While on, every key press AND release (modifiers included) is queued as a
 * KeyEvent for this process, and nothing reaches the ASCII path, so the shell
 * blocked in GETCHAR stays asleep. Only one process can hold it at a time;
 * it's released automatically if the holder exits or is killed.
 *
 * @return true if raw mode was switched (false: another process holds it)
 */
bool raw_keyboard_enable(bool on);

/**
 * Pop the oldest queued key event. Never blocks.
 *
 * @return true if `out` was filled, false if the queue is empty (or this
 *         process doesn't hold raw mode)
 */
bool raw_keyboard_poll(struct KeyEvent *out);

/**
 * Make `[start, start + size)` usable memory for this process (service 34).
 *
 * Touching an address in a 4 MiB region the process has never used kills it;
 * call this first for any big fixed-address buffer (heap, game zone, file
 * images). Memory is only actually allocated as pages are first touched, and
 * comes back zeroed. Freed automatically on exit.
 *
 * @return false if the range is empty, wraps, reaches >= 0xC0000000, or the
 *         kernel ran out of page-table frames
 */
bool map_region(void *start, uint32_t size);

/**
 * Full-screen graphics - backed by int 0x30 services 32-33. Both need the
 * screen, which raw_keyboard_enable(true) claims (and raw_keyboard_enable
 * (false), or exiting, gives back - restoring the default palette).
 */

/**
 * Program all 256 palette entries from 768 bytes of R, G, B (8 bits each).
 * @return false if this process doesn't own the screen
 */
bool video_set_palette(const uint8_t *rgb);

/**
 * Draw a `width` x `height` 8bpp image centred on the 640x480 screen.
 * Silently ignored if this process doesn't own the screen or it doesn't fit.
 */
void video_blit(const uint8_t *buf, uint32_t width, uint32_t height);

/**
 * Clear the screen and exec "shell" from the root directory, replacing the
 * calling program - how a program launched with `exec` hands control back.
 * Exits the process if the shell can't be found or loaded.
 */
__attribute__((noreturn)) void return_to_shell(void);

#endif
