#include "driver/keyboard.h"
#include "cpu/interrupt.h"
#include "cpu/portio.h"
#include "process/scheduler.h"
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

/* --- Raw mode --- */

/// pid owning raw mode, 0 = raw mode off
static uint32_t raw_owner_pid = 0;

/// Single-producer (ISR) / single-consumer (owner's syscall) ring. head is
/// only written by the ISR, tail only by keyboard_pop_event(); with one slot
/// kept empty, head == tail means empty.
static struct KeyEvent event_ring[KEYBOARD_EVENT_RING_SIZE];
static volatile uint32_t event_head = 0;
static volatile uint32_t event_tail = 0;

/// Bytes still to swallow from a Pause/Break (0xE1 ...) sequence - it is the
/// only multi-byte make code and would otherwise decode as a bogus Ctrl+NumLock.
static uint8_t pause_bytes_left = 0;

static void push_event(uint8_t raw_byte, bool extended) {
    uint32_t next = (event_head + 1) % KEYBOARD_EVENT_RING_SIZE;
    if (next == event_tail) {
        return; // full - drop rather than overwrite unread events
    }
    event_ring[event_head] = (struct KeyEvent) {
        .scancode = raw_byte & 0x7F,
        .extended = extended ? 1 : 0,
        .pressed = (raw_byte & 0x80) ? 0 : 1,
    };
    event_head = next;
}

bool keyboard_set_raw(bool raw, uint32_t pid) {
    if (raw) {
        if (raw_owner_pid != 0 && raw_owner_pid != pid) {
            return false; // someone else already has it
        }
        event_head = event_tail = 0;
        keyboard_state.read_extended_mode = false;
        pause_bytes_left = 0;
        raw_owner_pid = pid;
        return true;
    }

    if (raw_owner_pid != pid) {
        return false;
    }
    raw_owner_pid = 0;
    keyboard_state.shift_pressed = false;
    keyboard_state.ctrl_pressed = false;
    keyboard_state.read_extended_mode = false;
    keyboard_state.keyboard_buffer = 0;
    return true;
}

bool keyboard_pop_event(struct KeyEvent *out, uint32_t pid) {
    if (raw_owner_pid == 0 || raw_owner_pid != pid || event_tail == event_head) {
        return false;
    }
    *out = event_ring[event_tail];
    event_tail = (event_tail + 1) % KEYBOARD_EVENT_RING_SIZE;
    return true;
}

void keyboard_release_raw_owner(uint32_t pid) {
    if (raw_owner_pid != 0 && raw_owner_pid == pid) {
        keyboard_set_raw(false, pid);
    }
}

/// Raw-mode half of the ISR: queue the byte as an event and skip the ASCII path.
static void keyboard_isr_raw(uint8_t scancode) {
    if (pause_bytes_left > 0) {
        pause_bytes_left--;
        return;
    }
    if (scancode == 0xE1) {
        pause_bytes_left = 5; // E1 1D 45 E1 9D C5
        return;
    }
    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        return;
    }

    bool extended = keyboard_state.read_extended_mode;
    keyboard_state.read_extended_mode = false;

    // 0xE0 0x2A / 0xE0 0xAA are "fake shift" bytes some keyboards wrap around
    // arrow/Home/End keys - not real keys, don't report them.
    if (extended && (scancode & 0x7F) == EXT_SCANCODE_SHIFT_LEFT) {
        return;
    }

    push_event(scancode, extended);
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (raw_owner_pid != 0) {
        keyboard_isr_raw(scancode);
        pic_ack(IRQ_KEYBOARD);
        return;
    }

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

            if (keyboard_state.keyboard_buffer != 0) {
                scheduler_wake_waiting(SCHEDULER_WAIT_KEYBOARD);
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
            scheduler_wake_waiting(SCHEDULER_WAIT_KEYBOARD);
        }
    }
    pic_ack(IRQ_KEYBOARD);
}
