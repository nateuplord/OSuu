#include "kernel_api.h"
#include "shell.h"
#include <stdbool.h>
#include <stdint.h>

static uint8_t row = 0;
static uint8_t col = 0;

static void new_line(void) {
    ++row;
    col = 0;
}

int main(void) {
    keyboard_state_activate();
    start_shell();
    shell_get_cursor_position(&row, &col);

    while (true) {
        char c;
        get_keyboard_buffer(&c);
        if (!c) {
            continue;
        }

        switch (c) {
            case '\n':
                new_line();
                enter_command();
                shell_get_cursor_position(&row, &col);
                break;
            case '\b':
                remove_char();
                shell_get_cursor_position(&row, &col);
                break;
            case KEYBOARD_KEY_LEFT:
                shell_cursor_left(&row, &col);
                break;
            case KEYBOARD_KEY_RIGHT:
                shell_cursor_right(&row, &col);
                break;
            case KEYBOARD_KEY_UP:
                shell_history_up(&row, &col);
                break;
            case KEYBOARD_KEY_DOWN:
                shell_history_down(&row, &col);
                break;
            default:
                framebuffer_write(row, col, c, 0xF, 0);
                ++col;

                if (col >= FRAMEBUFFER_WIDTH) {
                    new_line();
                }
                add_char(c);
                break;
        }
        framebuffer_set_cursor(row, col);
    }

    return 0;
}
