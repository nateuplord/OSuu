#ifndef _GUI_H
#define _GUI_H

#include <stdint.h>

/**
 * Axis-aligned rectangle passed by pointer to SYSCALL_GUI_FILL_RECT.
 * Coordinates are pixels in the linear 640x480 framebuffer.
 */
struct GuiRect {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t color;
} __attribute__((packed));

#endif
