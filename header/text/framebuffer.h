#ifndef _FRAMEBUFFER_H
#define _FRAMEBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Terminal framebuffer
 *
 * @details
 * Starting at `FRAMEBUFFER_MEMORY_OFFSET`,
 * - Even number memory: Character, 8-bit
 * - Odd number memory: Character color lower 4-bit, Background color upper 4-bit
 *
 * @note Resolution is 80x25
 */
#define FRAMEBUFFER_MEMORY_OFFSET ((uint8_t *) 0xC00B8000)
#define CURSOR_PORT_CMD 0x03D4
#define CURSOR_PORT_DATA 0x03D5

#define FRAMEBUFFER_WIDTH 80
#define FRAMEBUFFER_HEIGHT 25

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

#endif
