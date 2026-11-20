#include "driver/keyboard.h"
#include "cpu/interrupt.h"
#include "cpu/portio.h"
#include <stdint.h>

static struct KeyboardDriverState keyboard_state;

const char keyboard_scancode_1_to_ascii_map[256] = {
    0,   0x1B, '1',  '2', '3',  '4', '5', '6', '7', '8', '9', '0', '-',
    '=', '\b', '\t', 'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    '[', ']',  '\n', 0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    ';', '\'', '`',  0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',',
    '.', '/',  0,    '*', 0,    ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   '-', 0,   0,   0,
    '+', 0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,

    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,
};

const char keyboard_scancode_1_to_ascii_shift_map[256] = {
    0,   0x1B, '!',  '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',
    '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':', '\"', '~',  0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<',
    '>', '?',  0,    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   '-', 0,   0,   0,
    '+', 0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,

    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,
};

static void update_modifier_state(uint8_t scancode) {
    bool released = scancode & 0x80;
    uint8_t key = scancode & 0x7F;

    if (key == EXT_SCANCODE_SHIFT_LEFT || key == EXT_SCANCODE_SHIFT_RIGHT) {
        keyboard_state.shift_pressed = !released;
    }

    if (key == EXT_SCANCODE_CTRL_LEFT) {
        keyboard_state.ctrl_pressed = !released;
    }
}

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = 0;
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        pic_ack(IRQ_KEYBOARD);
        return;
    }

    if (keyboard_state.read_extended_mode) {
        keyboard_state.read_extended_mode = false;
        if (keyboard_state.keyboard_input_on && (scancode & 0x80) == 0) {
            switch (scancode) {
                case EXT_SCANCODE_UP:
                    keyboard_state.keyboard_buffer = KEYBOARD_KEY_UP;
                    break;
                case EXT_SCANCODE_DOWN:
                    keyboard_state.keyboard_buffer = KEYBOARD_KEY_DOWN;
                    break;
                case EXT_SCANCODE_LEFT:
                    keyboard_state.keyboard_buffer = KEYBOARD_KEY_LEFT;
                    break;
                case EXT_SCANCODE_RIGHT:
                    keyboard_state.keyboard_buffer = KEYBOARD_KEY_RIGHT;
                    break;
            }
        }
        pic_ack(IRQ_KEYBOARD);
        return;
    }

    update_modifier_state(scancode);

    if (keyboard_state.keyboard_input_on) {
        const char *map = keyboard_state.shift_pressed
                            ? keyboard_scancode_1_to_ascii_shift_map
                            : keyboard_scancode_1_to_ascii_map;

        char ascii_char = map[scancode];

        if (keyboard_state.ctrl_pressed) {
            if (ascii_char >= 'A' && ascii_char <= 'Z') {
                ascii_char += 'a' - 'A';
            }

            if (ascii_char >= 'a' && ascii_char <= 'z') {
                ascii_char = ascii_char - 'a' + 1;
            }
        }

        if (ascii_char) {
            keyboard_state.keyboard_buffer = ascii_char;
        }
    }
    pic_ack(IRQ_KEYBOARD);
}
