#ifndef _USER_KERNEL_API_H
#define _USER_KERNEL_API_H

/**
 * Wrappers around the kernel's syscall() trap, with signatures matching
 * header/fs/ext2.h, header/driver/disk.h, header/text/framebuffer.h and
 * header/driver/keyboard.h exactly, so shell.c's calls don't need to change.
 */

#include "fs_types.h"
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

void get_node(struct EXT2Inode *node, uint32_t inode);
void sync_node(struct EXT2Inode *node, uint32_t inode);
void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count);
void write_blocks(
    const void *ptr,
    uint32_t logical_block_address,
    uint8_t block_count
);

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);
void framebuffer_putchar(char c, uint8_t attribute);
void framebuffer_puts(const char *buf, uint32_t count, uint8_t attribute);
void framebuffer_set_cursor(uint8_t r, uint8_t c);
void framebuffer_scroll_up(void);

void get_keyboard_buffer(char *buf);
void keyboard_state_activate(void);

#endif
