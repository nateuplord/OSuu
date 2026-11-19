#include "misc/badapple.h"
#include "driver/pit.h"
#include "fs/ext2.h"
#include "text/framebuffer.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GRID_COLS 8
#define GRID_ROWS 6
#define TILES_PER_FRAME (GRID_COLS * GRID_ROWS) // 48 tiles
#define BYTES_PER_FRAME (TILES_PER_FRAME * 2)   // 96 bytes

#define VIDEO_FPS 30
#define PIT_HZ 100
#define ROOT_INODE 1

#define COLOR_BLACK 0x00
#define COLOR_WHITE 0x0F

// Palette stays in RAM (~355 KB), frames stream through a 32 KB window
#define PALETTE_BUF_SIZE (384 * 1024)
#define CHUNK_FRAMES 340
// 32,640 bytes of real data, rounded up to the next 512-byte block boundary -
// read() copies in whole BLOCK_SIZE chunks based on the file's block count,
// so a non-block-aligned buffer here overflows by the rounding difference.
#define CHUNK_BUF_SIZE 32768

static uint8_t palette_buf[PALETTE_BUF_SIZE];
static uint8_t chunk_buf[CHUNK_BUF_SIZE];

static void blit_tile(
    const uint8_t *tile_bitmap,
    uint16_t base_x,
    uint16_t base_y
) {
    for (uint16_t ty = 0; ty < 8; ty++) {
        uint8_t tile_byte = tile_bitmap[ty];
        for (uint16_t tx = 0; tx < 8; tx++) {
            uint8_t bit = (tile_byte >> tx) & 1;
            putpixel(base_x + tx, base_y + ty, bit ? COLOR_WHITE : COLOR_BLACK);
        }
    }
}

/**
 * Formats "f0.bin", "f1.bin", ..., "f19.bin" without needing sprintf.
 */
static uint8_t format_chunk_name(
    uint32_t chunk_idx,
    char *out_name
) {
    uint8_t pos = 0;
    out_name[pos++] = 'f';

    if (chunk_idx >= 10) {
        out_name[pos++] = '0' + (chunk_idx / 10);
    }
    out_name[pos++] = '0' + (chunk_idx % 10);

    out_name[pos++] = '.';
    out_name[pos++] = 'b';
    out_name[pos++] = 'i';
    out_name[pos++] = 'n';
    out_name[pos] = '\0';
    return pos;
}

/**
 * Counts valid frames in the current chunk (handles the shorter final chunk).
 */
static uint32_t count_valid_frames(
    const uint8_t *buf,
    uint32_t max_frames
) {
    for (int32_t f = (int32_t) max_frames - 1; f >= 0; f--) {
        const uint8_t *ptr = buf + ((size_t) f * BYTES_PER_FRAME);
        for (size_t b = 0; b < BYTES_PER_FRAME; b++) {
            if (ptr[b] != 0) {
                return (uint32_t) (f + 1);
            }
        }
    }
    return 0;
}

void play_bad_apple(
    uint16_t offset_x,
    uint16_t offset_y
) {
    // 1. Load palette.bin once into RAM
    struct EXT2DriverRequest palette_req = {
        .buf = palette_buf,
        .name = "palette.bin",
        .name_len = 11,
        .parent_inode = ROOT_INODE,
        .buffer_size = PALETTE_BUF_SIZE,
        .is_directory = false,
    };
    if (read(palette_req) != 0) {
        framebuffer_write(0, 0, 'E', 0x0C, 0x00);
        framebuffer_write(0, 1, 'P', 0x0C, 0x00);
        return;
    }

    framebuffer_clear();
    uint32_t start_tick = pit_get_ticks();
    uint32_t global_frame_idx = 0;
    char chunk_name[12];

    // 2. Stream f0.bin, f1.bin, ... until read() returns 3 (Not found)
    for (uint32_t chunk_idx = 0;; chunk_idx++) {
        // Zero buffer so the final partial chunk has clean trailing zeros
        for (size_t z = 0; z < CHUNK_BUF_SIZE; z++) {
            chunk_buf[z] = 0;
        }

        uint8_t name_len = format_chunk_name(chunk_idx, chunk_name);
        struct EXT2DriverRequest chunk_req = {
            .buf = chunk_buf,
            .name = chunk_name,
            .name_len = name_len,
            .parent_inode = ROOT_INODE,
            .buffer_size = CHUNK_BUF_SIZE,
            .is_directory = false,
        };

        int8_t res = read(chunk_req);
        if (res == 3) {
            // Reached end of chunk sequence (no more fN.bin files)
            break;
        }
        if (res != 0) {
            framebuffer_write(0, 0, 'E', 0x0C, 0x00);
            framebuffer_write(0, 1, 'C', 0x0C, 0x00);
            return;
        }

        uint32_t frames_in_chunk = count_valid_frames(chunk_buf, CHUNK_FRAMES);

        // 3. Play all frames in this 32 KB chunk
        for (uint32_t i = 0; i < frames_in_chunk; i++) {
            const uint8_t *frame_ptr =
                chunk_buf + ((size_t) i * BYTES_PER_FRAME);

            for (uint16_t row = 0; row < GRID_ROWS; row++) {
                for (uint16_t col = 0; col < GRID_COLS; col++) {
                    size_t offset = (row * GRID_COLS + col) * 2;
                    uint16_t tile_id = (uint16_t) frame_ptr[offset] |
                                       ((uint16_t) frame_ptr[offset + 1] << 8);

                    const uint8_t *tile_bitmap =
                        &palette_buf[(size_t) tile_id * 8];
                    blit_tile(
                        tile_bitmap, offset_x + (col * 8), offset_y + (row * 8)
                    );
                }
            }

            global_frame_idx++;
            uint32_t next_frame_tick =
                start_tick + (global_frame_idx * PIT_HZ) / VIDEO_FPS;
            while (pit_get_ticks() < next_frame_tick) {
                __asm__ volatile("hlt");
            }
        }
    }
}

