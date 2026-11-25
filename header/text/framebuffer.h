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
 * Report which page directory entries framebuffer_init() mapped the linear
 * framebuffer's MMIO into, so a freshly created process page directory can
 * copy them too - otherwise the framebuffer is only reachable from whichever
 * directory was active when framebuffer_init() ran (the kernel's own), and
 * any other directory's first touch gets silently demand-paged to an
 * arbitrary RAM frame instead of real video memory.
 *
 * @param start_dir_index
 * Lowest page directory index the framebuffer occupies
 *
 * @param end_dir_index
 * Highest page directory index the framebuffer occupies (inclusive; equal
 * to start_dir_index unless the framebuffer straddles a 4 MiB boundary)
 */
void framebuffer_get_mmio_dir_range(
    uint32_t *start_dir_index,
    uint32_t *end_dir_index
);

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
 * Fill an axis-aligned rectangle in the linear framebuffer.
 *
 * @details Clips the rectangle to the visible framebuffer. Used by userland
 * GUI programs through SYSCALL_GUI_FILL_RECT so a whole cell can be drawn
 * with one syscall instead of one syscall per pixel.
 */
/// 256 palette entries x (R, G, B)
#define FRAMEBUFFER_PALETTE_BYTES 768

/**
 * Read the VGA DAC's current 256-colour palette and keep it as the default.
 * Called once from framebuffer_init(), before anything reprograms the DAC.
 */
void framebuffer_save_default_palette(void);

/**
 * Program all 256 palette entries.
 *
 * @param rgb FRAMEBUFFER_PALETTE_BYTES bytes: R, G, B per entry, 8 bits each
 *            (scaled down to the DAC's 6 bits). Backs SYSCALL_VIDEO_SET_PALETTE.
 */
void framebuffer_set_palette(const uint8_t *rgb);

/// Put back the palette saved by framebuffer_save_default_palette().
void framebuffer_restore_palette(void);

/**
 * Copy a `width` x `height` 8bpp image to the centre of the screen.
 * Backs SYSCALL_VIDEO_BLIT.
 *
 * @return false (nothing drawn) if either side is 0 or larger than the screen
 */
bool framebuffer_blit(const uint8_t *buf, uint32_t width, uint32_t height);

/**
 * Claim the screen for process `pid` (a full-screen program such as a game).
 * Only the owner may change the palette or blit. Claimed and released
 * together with raw keyboard mode (SYSCALL_KEYBOARD_SET_RAW).
 *
 * @return false if another process already owns the screen
 */
bool framebuffer_claim(uint32_t pid);

/**
 * Release the screen if `pid` owns it, restoring the default palette so text
 * is readable again. Also called from process teardown.
 */
void framebuffer_release(uint32_t pid);

/// pid owning the screen, or 0 if nobody does
uint32_t framebuffer_get_owner(void);

void framebuffer_fill_rect(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
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
 * Forget the previous caret and auto-advance position without drawing. Used
 * after a full-screen clear, where the old caret's pixels were already
 * overwritten and must not be erased a second time.
 */
void framebuffer_reset_cursor(void);

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
void putchar(
    char c,
    uint8_t color
);

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
void puts(
    const char *buf,
    uint32_t count,
    uint8_t color
);

#endif
