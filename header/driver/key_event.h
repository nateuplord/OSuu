#ifndef _KEY_EVENT_H
#define _KEY_EVENT_H

#include <stdint.h>

/**
 * One raw key press or release, as queued by the keyboard ISR while raw mode
 * is on (SYSCALL_KEYBOARD_SET_RAW). Shared by the kernel and userland, so it
 * lives in its own header with no kernel-only dependencies.
 *
 * @details
 * Unlike the ASCII path (GETCHAR / KEYBOARD_POLL), raw mode reports every key
 * including modifiers, and reports releases too - which is what games that
 * need "is this key held" state (Doom, Snake-style movement) require.
 */
struct KeyEvent {
    /// Scancode set 1 make code, with the release bit (0x80) stripped
    uint8_t scancode;
    /// 1 if the key was sent with the 0xE0 prefix (arrows, right Ctrl/Alt, ...)
    uint8_t extended;
    /// 1 = pressed (make), 0 = released (break)
    uint8_t pressed;
    uint8_t _pad;
} __attribute__((packed));

#endif
