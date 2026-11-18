#ifndef _VBE_H
#define _VBE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Fields read out of the BIOS's VBE Mode Info Block after a successful
 * vbe_set_mode() call - everything needed to drive the linear framebuffer.
 */
struct VbeModeInfo {
    uint32_t framebuffer_addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
} __attribute__((packed));

/**
 * Drop to real mode, ask the VBE BIOS to set the given mode with a linear
 * framebuffer, read back its mode info, then return to protected mode.
 *
 * @param mode
 * VBE mode number (e.g. 0x101 for 640x480x8). The linear-framebuffer
 * request bit (0x4000) is added internally - pass the plain mode number.
 *
 * @param out
 * Filled in with the resulting framebuffer's address/pitch/dimensions/bpp
 * on success. Left untouched on failure.
 *
 * @return true on success, false if the BIOS rejected the mode
 */
bool vbe_set_mode(uint16_t mode, struct VbeModeInfo *out);

#endif
