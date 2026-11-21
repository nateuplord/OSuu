#ifndef _FRAMEBUFFER_H
#define _FRAMEBUFFER_H

#include "boot/vbe.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Terminal framebuffer
 *
 * @details
 * Text is rendered as 8x8 glyphs onto the linear graphics framebuffer set up
 * by vbe_set_mode() (640x480, 8bpp indexed), giving a character grid of
 * FRAMEBUFFER_WIDTH x FRAMEBUFFER_HEIGHT cells (640/8 x 480/8).
 */
#define FRAMEBUFFER_WIDTH 80
#define FRAMEBUFFER_HEIGHT 60

/**
 * Initialize the framebuffer driver with the linear graphics framebuffer
 * info returned by vbe_set_mode().
 *
 * @details
 * Must be called once, before any other framebuffer_* function, and only
 * after a successful vbe_set_mode() call.
 *
 * @param info
 * Mode info filled in by vbe_set_mode()
 */
void framebuffer_init(struct VbeModeInfo *info);

/**
 * Set a single pixel's color in the linear graphics framebuffer.
 *
 * @details
 * Writes directly into framebuffer memory at `y * pitch + x`, one byte
 * per pixel (8bpp indexed color). This is the lowest-level drawing
 * primitive; every other drawing function (framebuffer_write,
 * framebuffer_clear, ...) is built on top of it.
 *
 * @param x
 * Horizontal pixel position, zero-indexed
 *
 * @param y
 * Vertical pixel position, zero-indexed
 *
 * @param color_index
 * Palette index (0-255) to write at that pixel
 */
void putpixel(
    uint16_t x,
    uint16_t y,
    uint8_t color_index
);

/**
 * Set framebuffer character and color with corresponding parameter values.
 * @note
 * More details:
 * [BIOS color attributes](https://en.wikipedia.org/wiki/BIOS_color_attributes) in Wikipedia
 *
 * @param row
 * Vertical location, uses zero-indexing
 *
 * @param col
 * Horizontal location, uses zero-indexing
 *
 * @param c
 * ASCII character
 *
 * @param fg
 * Foreground / Character color
 *
 * @param bg
 * Background color
 */
void framebuffer_write(
    uint8_t row,
    uint8_t col,
    char c,
    uint8_t fg,
    uint8_t bg
);

/**
 * Set cursor to specified location.
 * @details Row and column starts from 0
 *
 * @param r
 * Current cursor vertical position (row)
 *
 * @param c
 * Current cursor horizontal position (column)
 */
void framebuffer_set_cursor(
    uint8_t r,
    uint8_t c
);

/**
 * Set all cell in framebuffer character to:
 * - `0x00` (empty character), and
 * - `0x07` (gray character & black background)
 *
 * @note It's allowed to use different color palette for this
 */
void framebuffer_clear(void);

/**
 * Scroll the whole screen up by one character row (8px) and clear the
 * newly-revealed bottom row.
 */
void framebuffer_scroll_up(void);

/**
 * Write a single character at the current cursor position and advance it,
 * wrapping to a new line and scrolling as needed. Backing implementation for
 * the `putchar` syscall (service 5).
 *
 * @param c
 * Character to write
 *
 * @param color
 * BIOS color attribute: low nibble is foreground, high nibble is background.
 */
void putchar(char c, uint8_t color);

/**
 * Write `count` characters starting at the current cursor position, same
 * wrapping/scrolling rules as putchar. Backing implementation for the `puts`
 * syscall (service 6).
 *
 * @param buf
 * Character buffer to write (not null-terminated; exactly `count` bytes read)
 *
 * @param count
 * Number of characters to write
 *
 * @param color
 * BIOS color attribute: low nibble is foreground, high nibble is background.
 */
void puts(const char *buf, uint32_t count, uint8_t color);

#endif
