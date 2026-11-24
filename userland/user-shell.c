#include "kernel_api.h"
#include "shell.h"
#include <stdbool.h>
#include <stdint.h>

static uint8_t row = 0;
static uint8_t col = 0;

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
                // Command output and the next prompt are emitted by the shell
                // itself; do not track a second, competing row here.
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
                // add_char() redraws the whole input line (including wrapped
                // rows) and positions the cursor; don't overwrite it here.
                add_char(c);
                shell_get_cursor_position(&row, &col);
                break;
        }
    }

    return 0;
}
