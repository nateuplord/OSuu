#include "misc/badapple.h"
#include "driver/keyboard.h"
#include "driver/pit.h"
#include "fs/ext2.h"
#include "text/framebuffer.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GRID_COLS 20
#define GRID_ROWS 15
#define TILES_PER_FRAME (GRID_COLS * GRID_ROWS) // 300 tiles

#define VIDEO_FPS 30
#define PIT_HZ 100
#define ROOT_INODE 1

#define COLOR_BLACK 0x00
#define COLOR_WHITE 0x0F

// Both loaded wholesale into RAM once - small enough (~2.4 MB combined, real
// encoder output is 705,208 / 1,782,289 bytes) that the old chunked-streaming
// scheme (needed back when frames.bin held raw, uncompressed per-frame data)
// isn't worth the complexity anymore. Sized with headroom above the actual
// file sizes rounded up to the next 512-byte block boundary, since read()
// copies in whole blocks based on the file's block count.
#define PALETTE_BUF_SIZE (768 * 1024)
#define FRAME_RLE_BUF_SIZE (1920 * 1024)

static uint8_t palette_buf[PALETTE_BUF_SIZE];
static uint8_t frame_rle_buf[FRAME_RLE_BUF_SIZE];

/**
 * One grid cell's position within frame_rle_buf's temporal-RLE stream: a
 * sequence of (run_len: 1 byte, tile_id: 4 bytes LE) pairs covering every
 * frame at that screen position - see misc/uhhh.py's rle_encode_ids().
 */
struct CellCursor {
    uint32_t offset;     // byte offset of the NEXT run to read
    uint32_t tile_id;    // tile id for the currently active run
    uint8_t remaining;   // frames left in the currently active run (0 = exhausted)
};

static struct CellCursor cursors[TILES_PER_FRAME];

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

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

/** Pull the next run off a cell's stream once its current run is used up. */
static void advance_cursor(struct CellCursor *cur) {
    if (cur->remaining > 0) {
        return;
    }
    cur->remaining = frame_rle_buf[cur->offset];
    cur->tile_id = read_u32_le(&frame_rle_buf[cur->offset + 1]);
    cur->offset += 5;
}

void play_bad_apple(
    uint16_t offset_x,
    uint16_t offset_y
) {
    // Now reached via the int 0x30 syscall gate (a 32-bit interrupt gate,
    // which clears EFLAGS.IF on entry) instead of the old ring-0 main loop,
    // which ran with interrupts already enabled. The pacing loop below
    // depends on the PIT and keyboard ISRs actually firing to make
    // progress, so without this it deadlocks on the very first frame.
    __asm__ volatile("sti");

    // 1. Load palette.bin (the tile dictionary) once into RAM
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

    // 2. Load frames.bin (header + TILES_PER_FRAME temporal-RLE streams)
    struct EXT2DriverRequest frames_req = {
        .buf = frame_rle_buf,
        .name = "frames.bin",
        .name_len = 10,
        .parent_inode = ROOT_INODE,
        .buffer_size = FRAME_RLE_BUF_SIZE,
        .is_directory = false,
    };
    if (read(frames_req) != 0) {
        framebuffer_write(0, 0, 'E', 0x0C, 0x00);
        framebuffer_write(0, 1, 'F', 0x0C, 0x00);
        return;
    }

    uint32_t num_frames = read_u32_le(&frame_rle_buf[0]);
    uint32_t stream_pos = 4 + TILES_PER_FRAME * 4;
    for (uint32_t i = 0; i < TILES_PER_FRAME; i++) {
        uint32_t stream_len = read_u32_le(&frame_rle_buf[4 + i * 4]);
        cursors[i].offset = stream_pos;
        cursors[i].remaining = 0;
        cursors[i].tile_id = 0;
        stream_pos += stream_len;
    }

    framebuffer_clear();
    uint32_t start_tick = pit_get_ticks();
    bool interrupted = false;

    // 3. Decode and play, one frame at a time - all TILES_PER_FRAME cursors
    // advance together, each independently tracking its own run.
    for (uint32_t frame = 0; frame < num_frames && !interrupted; frame++) {
        for (uint16_t row = 0; row < GRID_ROWS; row++) {
            for (uint16_t col = 0; col < GRID_COLS; col++) {
                struct CellCursor *cur = &cursors[row * GRID_COLS + col];
                advance_cursor(cur);
                cur->remaining--;

                const uint8_t *tile_bitmap =
                    &palette_buf[(size_t) cur->tile_id * 8];
                blit_tile(
                    tile_bitmap, offset_x + (col * 8), offset_y + (row * 8)
                );
            }
        }

        uint32_t next_frame_tick =
            start_tick + ((frame + 1) * PIT_HZ) / VIDEO_FPS;
        while (pit_get_ticks() < next_frame_tick) {
            char c;
            get_keyboard_buffer(&c); // non-blocking; 0 if nothing pending
            if (c) {
                interrupted = true;
                break;
            }
            __asm__ volatile("hlt");
        }
    }
}
