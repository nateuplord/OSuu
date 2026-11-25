// colortest - Phase 1 check for palette (32) + blit (33) + screen ownership.
//
// Draws a 640x400 frame (Doom's output size) of all 256 palette indices,
// loads a rainbow palette, then rotates the palette every frame so the bars
// scroll without redrawing a single pixel - which only works if SET_PALETTE
// really reprograms the DAC. Esc releases the screen (restoring the default
// palette) and returns to the shell, whose text colours must look normal.
#include "kernel_api.h"
#include <stdbool.h>
#include <stdint.h>

#define FRAME_W 640u
#define FRAME_H 400u
#define FRAME_BUF ((uint8_t *) 0x10000000u) // via map_region, like Doom's zone
#define SCANCODE_ESC 0x01
#define FRAME_MS 33u // ~30 fps

static uint8_t palette[768];
static uint8_t rotated[768];

// Integer hue wheel: 256 steps through red -> yellow -> green -> cyan ->
// blue -> magenta -> red, full saturation and value.
static void build_rainbow(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t h = i * 6;          // 0..1535, six 256-wide sectors
        uint8_t f = (uint8_t) (h & 0xFF);
        uint8_t r, g, b;
        switch (h >> 8) {
            case 0: r = 255;     g = f;       b = 0;       break;
            case 1: r = 255 - f; g = 255;     b = 0;       break;
            case 2: r = 0;       g = 255;     b = f;       break;
            case 3: r = 0;       g = 255 - f; b = 255;     break;
            case 4: r = f;       g = 0;       b = 255;     break;
            default: r = 255;    g = 0;       b = 255 - f; break;
        }
        palette[i * 3 + 0] = r;
        palette[i * 3 + 1] = g;
        palette[i * 3 + 2] = b;
    }
}

static void draw_frame(void) {
    for (uint32_t y = 0; y < FRAME_H; y++) {
        uint8_t *row = FRAME_BUF + y * FRAME_W;
        for (uint32_t x = 0; x < FRAME_W; x++) {
            // Top half: vertical bars through all 256 indices. Bottom half:
            // diagonal stripes, so a stuck/misaligned blit is obvious.
            row[x] = y < FRAME_H / 2 ? (uint8_t) (x * 256 / FRAME_W)
                                     : (uint8_t) (x + y);
        }
    }
}

static void fail(const char *msg) {
    uint32_t n = 0;
    while (msg[n] != '\0')
        n++;
    framebuffer_puts(msg, n, 0x0C);
    sleep_ms(3000);
    return_to_shell();
}

int main(void) {
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    if (!map_region(FRAME_BUF, FRAME_W * FRAME_H)) {
        fail("colortest: map_region failed\n");
    }
    if (!raw_keyboard_enable(true)) {
        fail("colortest: screen/keyboard already owned by another process\n");
    }

    build_rainbow();
    draw_frame();
    framebuffer_clear(); // drop any text; bars are centred with 40px borders
    if (!video_set_palette(palette)) {
        raw_keyboard_enable(false);
        fail("colortest: SET_PALETTE refused\n");
    }
    video_blit(FRAME_BUF, FRAME_W, FRAME_H);

    uint32_t shift = 0;
    while (true) {
        struct KeyEvent ev;
        while (raw_keyboard_poll(&ev)) {
            if (ev.pressed && !ev.extended && ev.scancode == SCANCODE_ESC) {
                raw_keyboard_enable(false); // releases screen + palette
                return_to_shell();
            }
        }

        // Rotate the palette by one entry: every bar shifts one colour.
        shift = (shift + 1) & 0xFF;
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t src = ((i + shift) & 0xFF) * 3;
            rotated[i * 3 + 0] = palette[src + 0];
            rotated[i * 3 + 1] = palette[src + 1];
            rotated[i * 3 + 2] = palette[src + 2];
        }
        video_set_palette(rotated);
        sleep_ms(FRAME_MS);
    }
}
