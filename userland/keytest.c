// keytest - Phase 1 check for raw keyboard mode (syscalls 30-31).
//
// Prints every key press/release as it arrives, including modifiers and
// extended (0xE0) keys. Hold a key to see a single press (plus typematic
// repeats), let go to see the release. Esc quits back to the shell.
#include "kernel_api.h"
#include "string.h"
#include <stdbool.h>
#include <stdint.h>

#define COLOR_TEXT 0x07
#define COLOR_PRESS 0x0A   // light green
#define COLOR_RELEASE 0x0C // light red
#define SCANCODE_ESC 0x01

static void print(const char *s, uint8_t color) {
    uint32_t n = 0;
    while (s[n] != '\0')
        n++;
    framebuffer_puts(s, n, color);
}

static void print_hex8(uint8_t value, uint8_t color) {
    static const char digits[] = "0123456789ABCDEF";
    char out[4] = {'0', 'x', digits[value >> 4], digits[value & 0xF]};
    framebuffer_puts(out, 4, color);
}

// Same approach as snake: look up the shell's size in the root directory,
// then exec it, so quitting keytest drops back into a working shell.
static void return_to_shell(void) {
    char shell_name[] = "shell";
    struct BlockBuffer directory;
    struct EXT2Inode root_node;
    get_node(&root_node, 1);
    read_blocks(directory.buf, root_node.i_block[0], 1);

    uint32_t shell_inode = 0;
    uint32_t offset = get_dir_first_child_offset(directory.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory.buf, offset);
        if (entry->inode == 0 || entry->rec_len == 0 ||
            entry->rec_len > BLOCK_SIZE - offset) {
            break;
        }
        if (entry->name_len == 5 &&
            memcmp(get_entry_name(entry), shell_name, 5) == 0) {
            shell_inode = entry->inode;
            break;
        }
        offset += entry->rec_len;
    }
    if (shell_inode == 0) {
        exit_process();
    }

    struct EXT2Inode shell_node;
    get_node(&shell_node, shell_inode);

    struct EXT2DriverRequest exec_request = {
        .buf = NULL,
        .name = shell_name,
        .name_len = 5,
        .parent_inode = 1,
        .buffer_size = shell_node.i_size,
        .is_directory = false,
    };

    framebuffer_clear();
    exec_process(exec_request);
    exit_process();
}

int main(void) {
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    print("keytest - raw keyboard events. Esc to quit.\n\n", COLOR_TEXT);

    if (!raw_keyboard_enable(true)) {
        print("raw mode unavailable (another process holds it)\n", COLOR_RELEASE);
        sleep_ms(2000);
        return_to_shell();
    }

    while (true) {
        struct KeyEvent ev;
        if (!raw_keyboard_poll(&ev)) {
            yield();
            continue;
        }

        uint8_t color = ev.pressed ? COLOR_PRESS : COLOR_RELEASE;
        print(ev.pressed ? "press   " : "release ", color);
        print("sc=", COLOR_TEXT);
        print_hex8(ev.scancode, color);
        print(ev.extended ? "  ext\n" : "\n", COLOR_TEXT);

        if (ev.pressed && !ev.extended && ev.scancode == SCANCODE_ESC) {
            raw_keyboard_enable(false);
            return_to_shell();
        }
    }

    return 0;
}
