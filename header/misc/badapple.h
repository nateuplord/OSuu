#ifndef _BAD_APPLE_H
#define _BAD_APPLE_H

#include <stdint.h>

/**
 * Loads palette.bin and frames.bin from EXT2 root (inode 1) into static .bss
 * buffers and plays the 160x120 animation at 30 FPS using PIT pacing.
 */
void play_bad_apple(
    uint16_t offset_x,
    uint16_t offset_y
);

#endif
