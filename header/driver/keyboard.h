#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/key_event.h"
// #include "cpu/interrupt.h"

#define EXT_SCANCODE_UP 0x48
#define EXT_SCANCODE_DOWN 0x50
#define EXT_SCANCODE_LEFT 0x4B
#define EXT_SCANCODE_RIGHT 0x4D

#define KEYBOARD_KEY_UP    0x1C
#define KEYBOARD_KEY_DOWN  0x1D
#define KEYBOARD_KEY_LEFT  0x1E
#define KEYBOARD_KEY_RIGHT 0x1F

#define EXT_SCANCODE_SHIFT_LEFT 0x2A
#define EXT_SCANCODE_SHIFT_RIGHT 0x36
#define EXT_SCANCODE_CTRL_LEFT 0x1D

#define KEYBOARD_DATA_PORT 0x60
#define EXTENDED_SCANCODE_BYTE 0xE0

/**
 * Scancode values - ASCII printables mapping
 *
 * @note
 * How to use this array: `ascii_char = k[scancode]`.
 * By default, QEMU using scancode set 1 (from empirical testing)
 */
extern const char keyboard_scancode_1_to_ascii_map[256];

/// Shifted version of `keyboard_scancode_1_to_ascii_map`
extern const char keyboard_scancode_1_to_ascii_shift_map[256];

/**
 * All driver states
 */
struct KeyboardDriverState {
    /**
     * Extended mode toggle
     *
     * @note
     * Optional, can be used for signaling when next read
     * is extended scancode (ex. arrow keys)
     */
    bool read_extended_mode;

    /// Indicate whether keyboard ISR is activated or not
    bool keyboard_input_on;

    /// Indicate whether `Shift` key is pressed
    bool shift_pressed;

    /// Indicate whether `Ctrl` key is pressed
    bool ctrl_pressed;

    /// Stores keyboard input values in ASCII
    char keyboard_buffer;
} __attribute((packed));

/* Driver Interfaces */

/**
 * Activate keyboard ISR / start listen keyboard & save to buffer
 */
void keyboard_state_activate(void);

/**
 * Deactivate keyboard ISR / stop listening keyboard interrupt
 */
void keyboard_state_deactivate(void);

/**
 * Get keyboard buffer value and flush the buffer
 *
 * @param buf
 * Pointer to character buffer
 */
void get_keyboard_buffer(char *buf);

/* Raw mode - press/release events instead of ASCII */

/// Number of queued KeyEvents; events arriving while it's full are dropped.
#define KEYBOARD_EVENT_RING_SIZE 64

/**
 * Turn raw mode on or off on behalf of process `pid`.
 *
 * @details
 * While raw mode is on, every scancode is queued as a KeyEvent and the ASCII
 * buffer is left untouched, so processes blocked in GETCHAR (the shell) stay
 * asleep instead of receiving the raw-mode process's keystrokes. Only one
 * process may hold raw mode; turning it off is only honoured for that owner.
 * Turning it on clears any stale events; turning it off resets Shift/Ctrl
 * state so a modifier held at that moment doesn't stick.
 *
 * @return true if the change was applied
 */
bool keyboard_set_raw(bool raw, uint32_t pid);

/**
 * Pop the oldest queued event for raw-mode owner `pid`. Never blocks.
 *
 * @return true if an event was written to `out`, false if the queue was
 *         empty or `pid` doesn't own raw mode
 */
bool keyboard_pop_event(struct KeyEvent *out, uint32_t pid);

/**
 * Release raw mode if `pid` holds it. Called from process teardown so a
 * process that dies (or is killed) in raw mode doesn't leave the keyboard
 * dead for everyone else.
 */
void keyboard_release_raw_owner(uint32_t pid);

/* Keyboard Interrupt Service Routine */

/**
 * Handles keyboard interrupt & process scancodes into ASCII character.
 *
 * @note
 * Will start listening and processing keyboard scancode if
 * `keyboard_input_on` is `true`.
 */
void keyboard_isr(void);

#endif
