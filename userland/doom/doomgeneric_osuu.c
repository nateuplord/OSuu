// doomgeneric platform layer for OSuu.
//
// Wires doomgeneric's DG_* hooks to OSuu syscalls:
//   DG_Init        raw_keyboard_enable(true)  - claims keyboard + screen
//   DG_DrawFrame   video_set_palette() when Doom changed it, video_blit()
//   DG_GetKey      raw_keyboard_poll(), set-1 scancodes -> doomkeys.h codes
//   DG_GetTicksMs  get_ticks_ms()   (PIT, 10 ms resolution)
//   DG_SleepMs     sleep_ms()       (yields while waiting)
// and owns process exit: exit() gives the screen/keyboard back (restoring
// the palette) and returns to the shell, showing the error if Doom failed.
//
// Built with -DCMAP256: Doom hands over 8-bit palette indices, which is
// exactly OSuu's 640x480 8bpp mode, so frames are copied as-is.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_video.h"

#include "kernel_api.h"

#define CONSOLE_COLOR 0x07
#define CONSOLE_ERROR_COLOR 0x0C
#define ERROR_SHOW_MS 6000u

/* ------------------------------------------------------------ console */

// Until the first frame, Doom's startup log (W_Init, R_Init, ...) prints
// straight to the screen. After that, text would be drawn over the game, so
// it's only kept here - the tail is shown if Doom exits with an error.
static int frames_started = 0;
static char console_log[1536];
static size_t console_log_len = 0;

static void log_append(const char *text, size_t len) {
    if (len >= sizeof(console_log)) {
        text += len - (sizeof(console_log) - 1);
        len = sizeof(console_log) - 1;
    }
    if (console_log_len + len >= sizeof(console_log)) {
        size_t drop = console_log_len + len - (sizeof(console_log) - 1);
        memmove(console_log, console_log + drop, console_log_len - drop);
        console_log_len -= drop;
    }
    memcpy(console_log + console_log_len, text, len);
    console_log_len += len;
}

void osuu_console_write(const char *text, size_t len, int is_stderr) {
    log_append(text, len);
    if (!frames_started) {
        framebuffer_puts(text, (uint32_t) len,
                         is_stderr ? CONSOLE_ERROR_COLOR : CONSOLE_COLOR);
    }
}

/* --------------------------------------------------------------- exit */

static int full_screen = 0;

__attribute__((noreturn)) void exit(int status) {
    if (full_screen) {
        raw_keyboard_enable(false); // releases the screen, restores palette
        full_screen = 0;
    }

    if (status != 0 && frames_started) {
        // The game was on screen, so the error never showed: show the
        // tail of the log (I_Error's message is the last thing in it).
        framebuffer_clear();
        framebuffer_set_cursor(0, 0);
        const char *head = "doom exited with an error:\n\n";
        framebuffer_puts(head, (uint32_t) strlen(head), CONSOLE_ERROR_COLOR);
        framebuffer_puts(console_log, (uint32_t) console_log_len, CONSOLE_COLOR);
        sleep_ms(ERROR_SHOW_MS);
    } else if (status != 0) {
        sleep_ms(ERROR_SHOW_MS); // startup error: already on screen
    }
    return_to_shell();
}

__attribute__((noreturn)) void abort(void) {
    exit(134);
}

/* ---------------------------------------------------------------- keys */

// Scancode set 1 (make codes) to doomkeys.h. Letters, digits and
// punctuation map to their lowercase ASCII, which is what Doom's menus and
// cheats expect. Modifiers map to Doom's default bindings:
// Ctrl = fire, Space = use, Shift = run, Alt = strafe.
static const uint8_t scancode_to_doom[0x59] = {
    [0x01] = KEY_ESCAPE,
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = KEY_MINUS, [0x0D] = KEY_EQUALS,
    [0x0E] = KEY_BACKSPACE, [0x0F] = KEY_TAB,
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = KEY_ENTER,
    [0x1D] = KEY_FIRE, // left Ctrl
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2A] = KEY_RSHIFT, // left Shift - run
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x33] = KEY_STRAFE_L, // ',' - vanilla's strafe left
    [0x34] = KEY_STRAFE_R, // '.' - vanilla's strafe right
    [0x35] = '/',
    [0x36] = KEY_RSHIFT, // right Shift
    [0x38] = KEY_RALT,   // left Alt - strafe modifier
    [0x39] = KEY_USE,    // Space
    [0x3A] = KEY_CAPSLOCK,
    [0x3B] = KEY_F1, [0x3C] = KEY_F2, [0x3D] = KEY_F3, [0x3E] = KEY_F4,
    [0x3F] = KEY_F5, [0x40] = KEY_F6, [0x41] = KEY_F7, [0x42] = KEY_F8,
    [0x43] = KEY_F9, [0x44] = KEY_F10,
    // Keypad with NumLock off doubles as arrows.
    [0x48] = KEY_UPARROW, [0x4B] = KEY_LEFTARROW,
    [0x4D] = KEY_RIGHTARROW, [0x50] = KEY_DOWNARROW,
    [0x57] = KEY_F11, [0x58] = KEY_F12,
};

static uint8_t translate_extended(uint8_t scancode) {
    switch (scancode) {
        case 0x48: return KEY_UPARROW;
        case 0x50: return KEY_DOWNARROW;
        case 0x4B: return KEY_LEFTARROW;
        case 0x4D: return KEY_RIGHTARROW;
        case 0x1D: return KEY_FIRE;   // right Ctrl
        case 0x38: return KEY_RALT;   // right Alt / AltGr
        case 0x1C: return KEY_ENTER;  // keypad Enter
        case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END;
        case 0x49: return KEY_PGUP;
        case 0x51: return KEY_PGDN;
        case 0x52: return KEY_INS;
        case 0x53: return KEY_DEL;
        default: return 0;
    }
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    struct KeyEvent ev;
    while (raw_keyboard_poll(&ev)) {
        uint8_t key = ev.extended ? translate_extended(ev.scancode)
                      : ev.scancode < sizeof(scancode_to_doom)
                          ? scancode_to_doom[ev.scancode]
                          : 0;
        if (key == 0)
            continue; // a key Doom has no use for
        *pressed = ev.pressed;
        *doomKey = key;
        return 1;
    }
    return 0;
}

/* ----------------------------------------------------------- DG hooks */

void DG_Init(void) {
    if (!raw_keyboard_enable(true)) {
        fprintf(stderr, "doom: screen/keyboard already owned by another program\n");
        exit(1);
    }
    full_screen = 1;
}

void DG_DrawFrame(void) {
    if (!frames_started) {
        frames_started = 1;
        framebuffer_clear(); // drop the startup log from the bands
    }

    if (palette_changed) {
        static uint8_t rgb[256 * 3];
        for (int i = 0; i < 256; i++) {
            rgb[i * 3 + 0] = (uint8_t) colors[i].r;
            rgb[i * 3 + 1] = (uint8_t) colors[i].g;
            rgb[i * 3 + 2] = (uint8_t) colors[i].b;
        }
        video_set_palette(rgb);
        palette_changed = false;
    }

    video_blit(DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

void DG_SleepMs(uint32_t ms) {
    sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    return get_ticks_ms();
}

void DG_SetWindowTitle(const char *title) {
    (void) title; // no windows on OSuu
}

int main(void) {
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);

    // The IWAD is looked up in the root directory (see osuu/file.c).
    static char *argv[] = {"doom", "-iwad", "doom1.wad", NULL};
    doomgeneric_Create(3, argv);

    for (;;) {
        doomgeneric_Tick();
    }
}
