#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
// #include "cpu/interrupt.h"

#define EXT_SCANCODE_UP             0x48
#define EXT_SCANCODE_DOWN           0x50
#define EXT_SCANCODE_LEFT           0x4B
#define EXT_SCANCODE_RIGHT          0x4D

#define EXT_SCANCODE_SHIFT_LEFT     0x2A
#define EXT_SCANCODE_SHIFT_RIGHT    0x36
#define EXT_SCANCODE_CTRL_LEFT      0x1D

#define KEYBOARD_DATA_PORT          0x60
#define EXTENDED_SCANCODE_BYTE      0xE0

/**
 * Scancode values - ASCII printables mapping
 *
 * @note 
 * How to use this array: `ascii_char = k[scancode]`.
 * By default, QEMU using scancode set 1 (from empirical testing)
 */
extern const char keyboard_scancode_1_to_ascii_map[256];

// Shifted version of `keyboard_scancode_1_to_ascii_map`
extern const char keyboard_scancode_1_to_ascii_shift_map[256];

// All driver states
struct KeyboardDriverState {
    bool read_extended_mode;    ///< Optional, can be used for signaling next read is extended scancode (ex. arrow keys)
    bool keyboard_input_on;     ///< Indicate whether keyboard ISR is activated or not
    bool shift_pressed;         ///< Indicate whether `Shift` key is pressed
    bool ctrl_pressed;          ///< Indicate whether `Ctrl` key is pressed
    char keyboard_buffer;       ///< Storing keyboard input values in ASCII
} __attribute((packed));

/* -- Driver Interfaces -- */

// Activate keyboard ISR / start listen keyboard & save to buffer
void keyboard_state_activate(void);

// Deactivate keyboard ISR / stop listening keyboard interrupt
void keyboard_state_deactivate(void);

/** 
 * Get keyboard buffer value and flush the buffer
 *
 * @param buf 
 * Pointer to character buffer
 */
void get_keyboard_buffer(char *buf);

/* -- Keyboard Interrupt Service Routine -- */

/**
 * Handles keyboard interrupt & process scancodes into ASCII character.
 * @note Will start listening and processing keyboard scancode if `keyboard_input_on` is `true`.
 */
void keyboard_isr(void);

#endif