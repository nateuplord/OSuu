#include "shell.h"
#include "clock_display.h"
#include "fs_types.h"
#include "kernel_api.h"
#include <stdbool.h>
#include <stdint.h>

struct ShellFilePath {
    uint32_t parent_inode;
    uint32_t inode;
    uint32_t size;
    bool is_directory;
    char name[UINT8_MAX + 1];
};

History shell_history_backup;
History shell_history;
int history_idx;
int cursor_pos;
static int history_view_idx = -1;
static bool history_draft_saved;
static uint32_t current_directory_inode = 1;
static uint8_t shell_printf_row = 0;
static uint8_t shell_printf_col = 0;
static uint8_t shell_printf_fg = 0x0F;
static uint8_t shell_printf_bg = 0;
static bool shell_printf_bold = false;
static uint32_t shell_input_display_length = 0;
static uint8_t shell_file_buffer[USER_FILE_BUFFER_SIZE];
static char shell_arg_storage[SHELL_MAX_ARGS - 1][SHELL_BUFFER_SIZE];
static char shell_find_path[SHELL_BUFFER_SIZE];
static struct BlockBuffer shell_find_directory_buffer;
static struct BlockBuffer shell_rm_directory_buffers[SHELL_PATH_DEPTH_MAX];
static char shell_rm_child_names[SHELL_PATH_DEPTH_MAX][UINT8_MAX + 1];

const char *builtin_cmd[] = {"sudo", "cd",    "ls",    "mkdir",    "cat",
                             "grep", "cp",    "mv",    "rm",       "find",
                             "echo", "pwd",   "exit",  "badapple", "exec",
                             "ps",   "kill",  "clock", "beep",     "clear",
                             "help", "pacman"};
int (*builtin_func[])(char **) = {
    &shell_sudo,
    &shell_cd,
    &shell_ls,
    &shell_mkdir,
    &shell_cat,
    &shell_grep,
    &shell_cp,
    &shell_mv,
    &shell_rm,
    &shell_find,
    &shell_echo,
    &shell_pwd,
    &shell_exit,
    &shell_badapple,
    &shell_exec,
    &shell_ps,
    &shell_kill,
    &shell_clock,
    &shell_beep,
    &shell_clear,
    &shell_help,
    &shell_pacman
};

int streq(
    const char str1[],
    const char str2[]
) {
    int i = 0;
    while (str1[i] != '\0' && str1[i] == str2[i])
        i++;
    return str1[i] == str2[i];
}

int str_len(const char *str) {
    int len = 0;
    while (str[len] != '\0')
        len++;
    return len;
}

void strcopy(
    const char src[],
    char dest[]
) {
    int len = str_len(src), i;
    for (i = 0; i < len; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

void strcopy_len(
    const char src[],
    char dest[],
    int len
) {
    int i = 0;
    for (i = 0; i < len; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

int str_split(
    const char *string,
    char **parts,
    char keychar,
    int max_parts
) {
    if (string == NULL || parts == NULL || max_parts <= 0)
        return -1;
    int i, amount = 0, start = 0, len = 0;
    for (i = 0; string[i] != '\0' && amount < max_parts; i++) {
        if (string[i] == keychar) {
            strcopy_len(&(string[start]), parts[amount], len);
            amount++;
            start = i + 1;
            len = 0;
        } else {
            len++;
        }
    }
    if (amount < max_parts) {
        strcopy_len(&(string[start]), parts[amount], len);
        amount++;
        parts[amount] = NULL;
        return amount;
    } else {
        return -1;
    }
}

int resolve_path_string(
    const char *path,
    char layers[][SHELL_PATH_SEGMENT_MAX]
) {
    if (path == NULL || layers == NULL)
        return -1;

    char *layer_ptrs[SHELL_PATH_DEPTH_MAX];
    for (int i = 0; i < SHELL_PATH_DEPTH_MAX; i++) {
        layer_ptrs[i] = layers[i];
    }

    if (path[0] == '/') {
        layers[0][0] = '/';
        layers[0][1] = '\0';
        int result =
            str_split(path, &layer_ptrs[1], '/', SHELL_PATH_DEPTH_MAX - 1);
        if (result == -1) {
            return -1;
        } else {
            return 1 + result;
        }
    } else {
        return str_split(path, layer_ptrs, '/', SHELL_PATH_DEPTH_MAX);
    }
}

int dir_file_split(
    const char *path,
    char *dir,
    char *name
) {
    if (path == NULL || dir == NULL || name == NULL)
        return -1;
    char layers[SHELL_PATH_DEPTH_MAX][SHELL_PATH_SEGMENT_MAX];
    int depth = resolve_path_string(path, layers);
    if (depth == -1)
        return -1;
    strcopy(layers[depth - 1], name);
    strcopy_len(path, dir, str_len(path) - str_len(name));
    return 0;
}

static bool shell_directory_entries_valid(uint32_t inode) {
    struct EXT2Inode directory;
    struct BlockBuffer directory_buf;
    get_node(&directory, inode);
    if ((directory.i_mode & EXT2_S_IFDIR) == 0)
        return true;

    read_blocks(&directory_buf, directory.i_block[0], 1);
    uint32_t offset = get_dir_first_child_offset(directory_buf.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory_buf.buf, offset);
        if (entry->inode == 0)
            return true;
        if (entry->rec_len == 0 || entry->rec_len > BLOCK_SIZE - offset ||
            sizeof(*entry) + entry->name_len > entry->rec_len) {
            return false;
        }
        offset += entry->rec_len;
    }
    return true;
}

static bool shell_repair_empty_directory(uint32_t inode) {
    struct EXT2Inode directory;
    struct BlockBuffer directory_buf;
    get_node(&directory, inode);
    if ((directory.i_mode & EXT2_S_IFDIR) == 0)
        return false;

    read_blocks(&directory_buf, directory.i_block[0], 1);
    struct EXT2DirectoryEntry *first =
        get_directory_entry(directory_buf.buf, 0);
    if (first->name_len != 2 ||
        first->rec_len != BLOCK_SIZE - get_entry_record_len(1) ||
        memcmp(get_entry_name(first), "..", 2) != 0) {
        return false;
    }

    uint32_t parent_inode = first->inode;
    memset(&directory_buf, 0, sizeof(directory_buf));
    struct EXT2DirectoryEntry *dot = get_directory_entry(directory_buf.buf, 0);
    dot->inode = inode;
    dot->rec_len = get_entry_record_len(1);
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    memcpy(get_entry_name(dot), ".", 1);

    struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);
    dotdot->inode = parent_inode;
    dotdot->rec_len = BLOCK_SIZE - dot->rec_len;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    memcpy(get_entry_name(dotdot), "..", 2);
    write_blocks(&directory_buf, directory.i_block[0], 1);
    return true;
}

static bool shell_initialize_directory(
    uint32_t inode,
    uint32_t parent_inode
) {
    struct EXT2Inode directory;
    struct BlockBuffer directory_buf;
    get_node(&directory, inode);
    if ((directory.i_mode & EXT2_S_IFDIR) == 0)
        return false;

    memset(&directory_buf, 0, sizeof(directory_buf));
    struct EXT2DirectoryEntry *dot = get_directory_entry(directory_buf.buf, 0);
    dot->inode = inode;
    dot->rec_len = get_entry_record_len(1);
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    memcpy(get_entry_name(dot), ".", 1);

    struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(dot);
    dotdot->inode = parent_inode;
    dotdot->rec_len = BLOCK_SIZE - dot->rec_len;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    memcpy(get_entry_name(dotdot), "..", 2);

    write_blocks(&directory_buf, directory.i_block[0], 1);
    directory.i_size = BLOCK_SIZE;
    directory.i_blocks = 1;
    sync_node(&directory, inode);
    return true;
}

static uint32_t shell_find_child_inode(
    uint32_t parent_inode,
    const char *name
) {
    struct EXT2Inode parent;
    struct BlockBuffer directory_buf;
    get_node(&parent, parent_inode);
    read_blocks(&directory_buf, parent.i_block[0], 1);

    uint32_t offset = 0;
    uint32_t name_len = (uint32_t) str_len(name);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory_buf.buf, offset);
        if (entry->inode == 0)
            return 0;
        if (entry->rec_len == 0 || entry->rec_len > BLOCK_SIZE - offset ||
            sizeof(*entry) + entry->name_len > entry->rec_len) {
            return 0;
        }
        if (entry->name_len == name_len &&
            memcmp(get_entry_name(entry), name, name_len) == 0) {
            return entry->inode;
        }
        offset += entry->rec_len;
    }
    return 0;
}

int8_t resolve_path(
    uint32_t start_inode,
    const char *path,
    uint32_t *resolved_inode
) {
    uint32_t current_inode = start_inode;
    if (path == NULL || resolved_inode == NULL)
        return -1;
    if (path[0] == '/')
        current_inode = 1;
    while (1) {
        while (*path == '/')
            path++;
        if (*path == '\0') {
            *resolved_inode = current_inode;
            return 0;
        }
        uint32_t name_len = 0;
        while (path[name_len] != '\0' && path[name_len] != '/')
            name_len++;
        if (name_len > UINT8_MAX)
            return -1;
        if (name_len == 1 && path[0] == '.') {
            // current directory
        } else if (name_len == 2 && path[0] == '.' && path[1] == '.') {
            // parent directory
            struct EXT2Inode current_directory;
            struct BlockBuffer directory_buf;
            get_node(&current_directory, current_inode);
            read_blocks(&directory_buf, current_directory.i_block[0], 1);
            struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(
                get_directory_entry(directory_buf.buf, 0)
            );
            current_inode = dotdot->inode;
        } else {
            if (!shell_directory_entries_valid(current_inode))
                return 2;
            char name[UINT8_MAX + 1];
            strcopy_len(path, name, name_len);
            struct BlockBuffer directory_buf;
            struct EXT2DriverRequest request = {
                .buf = &directory_buf,
                .name = name,
                .name_len = (uint8_t) name_len,
                .parent_inode = current_inode
            };
            int8_t result = read_directory(&request);
            if (result != 0)
                return result;
            struct EXT2DirectoryEntry *dot =
                get_directory_entry(directory_buf.buf, 0);
            current_inode = dot->inode;
        }
        path += name_len;
    }
}

static uint8_t shell_printf_attribute(void) {
    return (uint8_t) ((shell_printf_bg << 4) | (shell_printf_fg & 0x0F));
}

static void shell_printf_write(
    const char *message,
    uint32_t length
) {
    if (length == 0)
        return;
    framebuffer_puts(message, length, shell_printf_attribute());
    for (uint32_t i = 0; i < length; i++) {
        if (message[i] == '\n') {
            if (shell_printf_row + 1 < FRAMEBUFFER_HEIGHT) {
                shell_printf_row++;
            }
            shell_printf_col = 0;
        } else {
            shell_printf_col++;
            if (shell_printf_col >= FRAMEBUFFER_WIDTH) {
                shell_printf_col = 0;
                if (shell_printf_row + 1 < FRAMEBUFFER_HEIGHT) {
                    shell_printf_row++;
                }
            }
        }
    }
}

static uint32_t shell_parse_sgr(
    const char *message,
    uint32_t length,
    uint32_t start
) {
    uint16_t parameters[16];
    uint32_t parameter_count = 0;
    uint32_t value = 0;
    bool has_digit = false;
    uint32_t i = start + 2;

    if (start + 1 >= length || message[start] != '\x1B' ||
        message[start + 1] != '[') {
        return 0;
    }

    for (; i < length; i++) {
        if (i - start > 64)
            return 0;
        char c = message[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + (uint32_t) (c - '0');
            if (value > 999)
                return 0;
            has_digit = true;
        } else if (c == ';' || c == 'm') {
            if (parameter_count >= 16)
                return 0;
            parameters[parameter_count++] = has_digit ? (uint16_t) value : 0;
            value = 0;
            has_digit = false;
            if (c == 'm')
                break;
        } else {
            return 0;
        }
    }
    if (i >= length || message[i] != 'm')
        return 0;

    static const uint8_t ansi_to_vga[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    for (uint32_t p = 0; p < parameter_count; p++) {
        uint16_t code = parameters[p];
        if (code == 0) {
            shell_printf_fg = 0x0F;
            shell_printf_bg = 0;
            shell_printf_bold = false;
        } else if (code == 1) {
            shell_printf_bold = true;
            shell_printf_fg |= 0x08;
        } else if (code == 22) {
            shell_printf_bold = false;
        } else if (code >= 30 && code <= 37) {
            shell_printf_fg = (uint8_t) (ansi_to_vga[code - 30] +
                                         (shell_printf_bold ? 8 : 0));
        } else if (code >= 90 && code <= 97) {
            shell_printf_fg = (uint8_t) (ansi_to_vga[code - 90] + 8);
        } else if (code >= 40 && code <= 47) {
            shell_printf_bg = ansi_to_vga[code - 40];
        } else if (code >= 100 && code <= 107) {
            shell_printf_bg = (uint8_t) (ansi_to_vga[code - 100] + 8);
        } else if (code == 39) {
            shell_printf_fg = 0x0F;
        } else if (code == 49) {
            shell_printf_bg = 0;
        }
    }
    return i - start + 1;
}

static void shell_printf_newline(void) {
    framebuffer_putchar('\n', shell_printf_attribute());
    if (shell_printf_row + 1 < FRAMEBUFFER_HEIGHT) {
        shell_printf_row++;
    }
    shell_printf_col = 0;
}

static void shell_printf_n(
    const char *message,
    uint32_t length
) {
    uint32_t i = 0;
    while (i < length) {
        if (message[i] == '\n') {
            shell_printf_newline();
            i++;
        } else if (message[i] == '\r') {
            // Carriage return: back to column 0 of the current row, so the
            // next write overdraws the line (progress bars, spinners...).
            shell_printf_col = 0;
            framebuffer_set_cursor(shell_printf_row, 0);
            i++;
        } else {
            uint32_t consumed = 0;
            if (message[i] == '\x1B' && i + 1 < length &&
                message[i + 1] == '[') {
                consumed = shell_parse_sgr(message, length, i);
            }
            if (consumed != 0) {
                i += consumed;
            } else {
                uint32_t text_start = i;
                while (
                    i < length && message[i] != '\n' && message[i] != '\r' &&
                    message[i] != '\x1B'
                ) {
                    i++;
                }
                if (i == text_start)
                    i++;
                shell_printf_write(message + text_start, i - text_start);
            }
        }
    }
}

static void shell_printf(const char *message) {
    shell_printf_n(message, (uint32_t) str_len(message));
}

static void shell_print_uint_field(
    uint32_t value,
    uint8_t width
) {
    char digits[11];
    uint32_t length = 0;
    if (value == 0) {
        digits[length++] = '0';
    }
    while (value != 0) {
        digits[length++] = (char) ('0' + value % 10);
        value /= 10;
    }

    char reversed[11];
    for (uint32_t i = 0; i < length; i++) {
        reversed[i] = digits[length - 1 - i];
    }

    for (uint32_t i = length; i < width; i++) {
        shell_printf_write(" ", 1);
    }
    shell_printf_n(reversed, length);
}

static void shell_print_uint(uint32_t value) {
    shell_print_uint_field(value, 0);
}

static bool shell_parse_uint(
    const char *text,
    uint32_t *value
) {
    if (text == NULL || text[0] == '\0' || value == NULL)
        return false;

    uint32_t result = 0;
    for (uint32_t i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9')
            return false;
        uint32_t digit = (uint32_t) (text[i] - '0');
        if (result > (UINT32_MAX - digit) / 10)
            return false;
        result = result * 10 + digit;
    }

    *value = result;
    return true;
}

static const char *shell_process_state_name(uint8_t state) {
    switch (state) {
        case PROCESS_STATE_INACTIVE:
            return "INACTIVE";
        case PROCESS_STATE_READY:
            return "READY";
        case PROCESS_STATE_RUNNING:
            return "RUNNING";
        case PROCESS_STATE_BLOCKED:
            return "BLOCKED";
        default:
            return "UNKNOWN";
    }
}

static void shell_print_error(
    const char *prefix,
    const char *name,
    const char *suffix
) {
    char message[SHELL_BUFFER_SIZE + 64];
    uint32_t i = 0;
    for (uint32_t j = 0; prefix[j] != '\0'; j++) {
        message[i] = prefix[j];
        i++;
    }
    for (uint32_t j = 0; name[j] != '\0'; j++) {
        message[i] = name[j];
        i++;
    }
    for (uint32_t j = 0; suffix[j] != '\0'; j++) {
        message[i] = suffix[j];
        i++;
    }
    message[i] = '\0';
    shell_printf(message);
}

static void shell_get_current_path(char path[SHELL_BUFFER_SIZE]) {
    char reverse_path[SHELL_BUFFER_SIZE];
    uint32_t reverse_len = 0;
    uint32_t inode = current_directory_inode;
    bool valid = true;

    for (uint32_t depth = 0; inode != 1; depth++) {
        if (depth >= SHELL_BUFFER_SIZE / 2) {
            valid = false;
            break;
        }

        struct EXT2Inode directory;
        struct BlockBuffer directory_buf;
        get_node(&directory, inode);
        if ((directory.i_mode & EXT2_S_IFDIR) == 0) {
            valid = false;
            break;
        }
        read_blocks(&directory_buf, directory.i_block[0], 1);
        struct EXT2DirectoryEntry *dotdot =
            get_next_directory_entry(get_directory_entry(directory_buf.buf, 0));
        uint32_t parent_inode = dotdot->inode;
        if (parent_inode == 0 || parent_inode == inode) {
            valid = false;
            break;
        }

        struct EXT2Inode parent;
        get_node(&parent, parent_inode);
        read_blocks(&directory_buf, parent.i_block[0], 1);
        uint32_t offset = get_dir_first_child_offset(directory_buf.buf);
        bool found_name = false;
        while (offset < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry =
                get_directory_entry(directory_buf.buf, offset);
            if (entry->inode == 0 || entry->rec_len == 0 ||
                entry->rec_len > BLOCK_SIZE - offset ||
                sizeof(*entry) + entry->name_len > entry->rec_len) {
                break;
            }
            if (entry->inode == inode) {
                if (reverse_len + entry->name_len + 1 >= SHELL_BUFFER_SIZE) {
                    valid = false;
                    break;
                }
                memcpy(
                    reverse_path + reverse_len, get_entry_name(entry),
                    entry->name_len
                );
                reverse_len += entry->name_len;
                reverse_path[reverse_len++] = '/';
                found_name = true;
                break;
            }
            offset += entry->rec_len;
        }
        if (!valid || !found_name) {
            valid = false;
            break;
        }
        inode = parent_inode;
    }

    if (!valid) {
        strcopy("~", path);
        return;
    }

    uint32_t path_len = 0;
    // path[path_len++] = '~';
    path[path_len++] = '/';
    uint32_t end = reverse_len;
    while (end > 0) {
        while (end > 0 && reverse_path[end - 1] == '/')
            end--;
        uint32_t component_end = end;
        while (end > 0 && reverse_path[end - 1] != '/')
            end--;
        if (path_len > 1)
            path[path_len++] = '/';
        for (uint32_t i = end; i < component_end; i++) {
            path[path_len++] = reverse_path[i];
        }
    }
    path[path_len] = '\0';
}

static void shell_print_prompt(void) {
    char path[SHELL_BUFFER_SIZE];
    shell_get_current_path(path);
    // shell_printf consumes SGR escapes without advancing the cursor, so this
    // keeps the same visible prompt length used by shell_refresh_input_line().
    shell_printf("\x1B[32mUser@OSuu:\x1B[0m");
    shell_printf("\x1B[34m");
    shell_printf(path);
    shell_printf("\x1B[31m$ \x1B[0m");
    if (shell_printf_col >= FRAMEBUFFER_WIDTH)
        shell_printf_newline();
    uint8_t row;
    uint8_t col;
    shell_get_cursor_position(&row, &col);
    framebuffer_set_cursor(row, col);
}

static void shell_refresh_input_line(void) {
    Line *line = &shell_history.buffer[history_idx];
    char path[SHELL_BUFFER_SIZE];
    shell_get_current_path(path);

    uint32_t line_length = (uint32_t) str_len(line->buffer);
    uint32_t prompt_length =
        str_len("User@OSuu:") + str_len(path) + str_len("$ ");
    uint32_t start_row = shell_printf_row;
    uint32_t start_col = shell_printf_col;
    uint32_t screen_size = FRAMEBUFFER_HEIGHT * FRAMEBUFFER_WIDTH;

    // shell_printf_row/col point just past the prompt, where the input starts.
    // Recover the prompt's real start before redrawing it.
    uint32_t input_start_cell = start_row * FRAMEBUFFER_WIDTH + start_col;
    uint32_t prompt_start_cell = input_start_cell >= prompt_length
                                   ? input_start_cell - prompt_length
                                   : 0;
    uint32_t max_length = line_length > shell_input_display_length
                            ? line_length
                            : shell_input_display_length;
    uint32_t last_cell = input_start_cell + max_length;
    if (last_cell >= screen_size)
        last_cell = screen_size - 1;

    // Clear every row the prompt or either version of the input occupied. A
    // long line wraps, so clearing just one row leaves stale characters behind.
    uint32_t first_row = prompt_start_cell / FRAMEBUFFER_WIDTH;
    uint32_t last_row = last_cell / FRAMEBUFFER_WIDTH;
    for (uint32_t r = first_row; r <= last_row && r < FRAMEBUFFER_HEIGHT; ++r) {
        for (uint32_t c = 0; c < FRAMEBUFFER_WIDTH; ++c) {
            framebuffer_write((uint8_t) r, (uint8_t) c, ' ', 0xF, 0);
        }
    }

    // Redraw the prompt from its original start cell. shell_print_prompt()
    // prints through framebuffer_puts(), which uses the kernel's auto-advance
    // cursor, so sync that cursor to the prompt start first.
    shell_printf_row = (uint8_t) first_row;
    shell_printf_col = (uint8_t) (prompt_start_cell % FRAMEBUFFER_WIDTH);
    framebuffer_set_cursor(shell_printf_row, shell_printf_col);
    shell_print_prompt();
    input_start_cell = shell_printf_row * FRAMEBUFFER_WIDTH + shell_printf_col;
    for (
        uint32_t i = 0; i < line_length && input_start_cell + i < screen_size;
        ++i
    ) {
        uint32_t cell = input_start_cell + i;
        framebuffer_write(
            (uint8_t) (cell / FRAMEBUFFER_WIDTH),
            (uint8_t) (cell % FRAMEBUFFER_WIDTH), line->buffer[i], 0xF, 0
        );
    }
    shell_input_display_length = line_length;

    uint8_t cursor_row;
    uint8_t cursor_col;
    shell_get_cursor_position(&cursor_row, &cursor_col);
    framebuffer_set_cursor(cursor_row, cursor_col);
}

void shell_get_cursor_position(
    uint8_t *row,
    uint8_t *col
) {
    uint32_t cursor = shell_printf_row * FRAMEBUFFER_WIDTH + shell_printf_col +
                      (uint32_t) cursor_pos;
    if (cursor >= FRAMEBUFFER_HEIGHT * FRAMEBUFFER_WIDTH) {
        cursor = FRAMEBUFFER_HEIGHT * FRAMEBUFFER_WIDTH - 1;
    }
    if (row != NULL)
        *row = (uint8_t) (cursor / FRAMEBUFFER_WIDTH);
    if (col != NULL)
        *col = (uint8_t) (cursor % FRAMEBUFFER_WIDTH);
}

void shell_cursor_left(
    uint8_t *row,
    uint8_t *col
) {
    if (cursor_pos > 0)
        cursor_pos--;
    shell_get_cursor_position(row, col);
}

void shell_cursor_right(
    uint8_t *row,
    uint8_t *col
) {
    Line *line = &shell_history.buffer[history_idx];
    if (cursor_pos < str_len(line->buffer))
        cursor_pos++;
    shell_get_cursor_position(row, col);
}

static void shell_show_history_line(
    const char *line,
    uint32_t previous_length,
    uint8_t *row,
    uint8_t *col
) {
    uint32_t line_length = (uint32_t) str_len(line);
    uint32_t display_length =
        previous_length > line_length ? previous_length : line_length;
    uint32_t start = shell_printf_row * FRAMEBUFFER_WIDTH + shell_printf_col;
    uint32_t screen_size = FRAMEBUFFER_HEIGHT * FRAMEBUFFER_WIDTH;

    shell_input_display_length = display_length;

    for (uint32_t i = 0; i < display_length && start + i < screen_size; i++) {
        char character = i < line_length ? line[i] : ' ';
        uint32_t position = start + i;
        framebuffer_write(
            (uint8_t) (position / FRAMEBUFFER_WIDTH),
            (uint8_t) (position % FRAMEBUFFER_WIDTH), character, 0xF, 0
        );
    }

    uint32_t cursor = start + (uint32_t) cursor_pos;
    if (cursor >= screen_size)
        cursor = screen_size - 1;
    *row = (uint8_t) (cursor / FRAMEBUFFER_WIDTH);
    *col = (uint8_t) (cursor % FRAMEBUFFER_WIDTH);
}

void shell_history_up(
    uint8_t *row,
    uint8_t *col
) {
    if (row == NULL || col == NULL || shell_history.length < 2)
        return;
    uint16_t last_index = shell_history.length - 1;
    Line *current_line = &shell_history.buffer[last_index];
    uint32_t previous_length = (uint32_t) str_len(current_line->buffer);

    if (history_view_idx < 0) {
        strcopy(current_line->buffer, shell_history_backup.buffer[0].buffer);
        shell_history_backup.buffer[0].length = current_line->length;
        history_draft_saved = true;
        history_view_idx = (int) last_index - 1;
    } else if (history_view_idx > 0) {
        history_view_idx--;
    }

    Line *history_line = &shell_history.buffer[history_view_idx];
    strcopy(history_line->buffer, current_line->buffer);
    current_line->length = (uint16_t) str_len(current_line->buffer);
    cursor_pos = (int) current_line->length;
    shell_show_history_line(current_line->buffer, previous_length, row, col);
}

void shell_history_down(
    uint8_t *row,
    uint8_t *col
) {
    if (row == NULL || col == NULL || history_view_idx < 0)
        return;
    uint16_t last_index = shell_history.length - 1;
    Line *current_line = &shell_history.buffer[last_index];
    uint32_t previous_length = (uint32_t) str_len(current_line->buffer);

    if (history_view_idx < (int) last_index - 1) {
        history_view_idx++;
        Line *history_line = &shell_history.buffer[history_view_idx];
        strcopy(history_line->buffer, current_line->buffer);
    } else {
        history_view_idx = -1;
        if (history_draft_saved) {
            strcopy(
                shell_history_backup.buffer[0].buffer, current_line->buffer
            );
            current_line->length = (uint16_t) str_len(current_line->buffer);
        }
        history_draft_saved = false;
    }

    current_line->length = (uint16_t) str_len(current_line->buffer);
    cursor_pos = (int) current_line->length;
    shell_show_history_line(current_line->buffer, previous_length, row, col);
}

static int resolve_parent_and_name(
    const char *path,
    uint32_t *parent_inode,
    char name[UINT8_MAX + 1]
) {
    if (path == NULL || parent_inode == NULL || name == NULL)
        return -1;

    uint32_t path_len = (uint32_t) str_len(path);
    if (path_len == 0 || path_len >= SHELL_BUFFER_SIZE ||
        path[path_len - 1] == '/') {
        return -1;
    }

    uint32_t name_start = path_len;
    while (name_start > 0 && path[name_start - 1] != '/')
        name_start--;
    uint32_t name_len = path_len - name_start;
    if (name_len == 0 || name_len > UINT8_MAX)
        return -1;
    strcopy_len(path + name_start, name, (int) name_len);

    char parent_path[SHELL_BUFFER_SIZE];
    if (name_start == 0) {
        parent_path[0] = '\0';
    } else if (name_start == 1) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        strcopy_len(path, parent_path, (int) (name_start - 1));
    }

    return resolve_path(current_directory_inode, parent_path, parent_inode);
}

static int find_entry(
    uint32_t parent_inode,
    const char *name,
    uint32_t *inode,
    bool *is_directory
) {
    struct EXT2Inode parent_node;
    get_node(&parent_node, parent_inode);
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0)
        return -1;

    struct BlockBuffer directory_buf;
    read_blocks(&directory_buf, parent_node.i_block[0], 1);

    uint32_t name_len = (uint32_t) str_len(name);
    uint32_t offset = get_dir_first_child_offset(directory_buf.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory_buf.buf, offset);
        if (entry->inode == 0)
            return 0;
        if (entry->rec_len == 0 || entry->rec_len > BLOCK_SIZE - offset ||
            sizeof(*entry) + entry->name_len > entry->rec_len) {
            return -1;
        }

        if (entry->name_len == name_len &&
            memcmp(get_entry_name(entry), name, name_len) == 0) {
            *inode = entry->inode;
            *is_directory = entry->file_type == EXT2_FT_DIR;
            return 1;
        }
        offset += entry->rec_len;
    }
    return 0;
}

static int resolve_shell_file(
    const char *path,
    struct ShellFilePath *file
) {
    uint32_t resolved_inode;
    int result = resolve_path(current_directory_inode, path, &resolved_inode);
    if (result == 0) {
        struct EXT2Inode node;
        get_node(&node, resolved_inode);
        file->inode = resolved_inode;
        file->size = node.i_size;
        file->is_directory = true;
        return 0;
    }
    if (result != 1)
        return result;

    uint32_t path_len = (uint32_t) str_len(path);
    if (path_len > 0 && path[path_len - 1] == '/')
        return 1;

    result = resolve_parent_and_name(path, &file->parent_inode, file->name);
    if (result != 0)
        return result;

    bool entry_is_directory;
    int found = find_entry(
        file->parent_inode, file->name, &file->inode, &entry_is_directory
    );
    if (found == 0)
        return 2;
    if (found < 0)
        return -1;

    struct EXT2Inode node;
    get_node(&node, file->inode);
    file->is_directory =
        entry_is_directory || (node.i_mode & EXT2_S_IFDIR) != 0;
    file->size = node.i_size;
    // Bounded by shell_file_buffer's actual size (USER_FILE_BUFFER_SIZE),
    // not the real 4 MiB disk - the ported shell's buffer is deliberately
    // much smaller than the kernel version's DISK_SPACE-sized one.
    if (file->size > USER_FILE_BUFFER_SIZE)
        return -1;
    return 0;
}

static int read_shell_file(const struct ShellFilePath *file) {
    struct EXT2DriverRequest request = {
        .buf = shell_file_buffer,
        .name = (char *) file->name,
        .name_len = (uint8_t) str_len(file->name),
        .parent_inode = file->parent_inode,
        .buffer_size = USER_FILE_BUFFER_SIZE,
        .is_directory = false
    };
    return read(request);
}

static int resolve_copy_destination(
    const char *path,
    const char *source_name,
    uint32_t *parent_inode,
    char name[UINT8_MAX + 1],
    bool *already_exists
) {
    uint32_t directory_inode;
    int result = resolve_path(current_directory_inode, path, &directory_inode);
    if (result == 0) {
        struct EXT2Inode node;
        get_node(&node, directory_inode);
        if ((node.i_mode & EXT2_S_IFDIR) == 0)
            return 1;
        *parent_inode = directory_inode;
        strcopy(source_name, name);
    } else {
        uint32_t path_len = (uint32_t) str_len(path);
        if (result == 1 && path_len > 0 && path[path_len - 1] == '/')
            return 1;
        result = resolve_parent_and_name(path, parent_inode, name);
        if (result != 0)
            return result;
    }

    uint32_t existing_inode;
    bool existing_is_directory;
    int found = find_entry(
        *parent_inode, name, &existing_inode, &existing_is_directory
    );
    if (found < 0)
        return -1;
    *already_exists = found == 1;
    return 0;
}

static void print_path_error(
    const char *command,
    const char *path,
    int result
);

static int shell_rm_entry(
    uint32_t parent_inode,
    const char *name,
    uint32_t depth
) {
    if (depth >= SHELL_PATH_DEPTH_MAX)
        return -1;

    uint32_t target_inode;
    bool is_directory;
    int found = find_entry(parent_inode, name, &target_inode, &is_directory);
    if (found == 0)
        return 1;
    if (found != 1)
        return -1;
    if (is_directory &&
        (target_inode == 1 || target_inode == current_directory_inode)) {
        return -1;
    }

    if (is_directory) {
        struct EXT2Inode directory;
        get_node(&directory, target_inode);
        if ((directory.i_mode & EXT2_S_IFDIR) == 0)
            return -1;

        while (true) {
            read_blocks(
                &shell_rm_directory_buffers[depth], directory.i_block[0], 1
            );
            uint32_t offset = get_dir_first_child_offset(
                shell_rm_directory_buffers[depth].buf
            );
            bool has_child = false;
            while (offset < BLOCK_SIZE) {
                struct EXT2DirectoryEntry *entry = get_directory_entry(
                    shell_rm_directory_buffers[depth].buf, offset
                );
                if (entry->inode == 0)
                    break;
                if (entry->rec_len == 0 ||
                    entry->rec_len > BLOCK_SIZE - offset ||
                    sizeof(*entry) + entry->name_len > entry->rec_len ||
                    entry->name_len > UINT8_MAX) {
                    return -1;
                }
                char *child_name = shell_rm_child_names[depth];
                strcopy_len(get_entry_name(entry), child_name, entry->name_len);
                offset += entry->rec_len;
                if (streq(child_name, ".") || streq(child_name, "..")) {
                    continue;
                }
                if (depth + 1 >= SHELL_PATH_DEPTH_MAX ||
                    shell_rm_entry(target_inode, child_name, depth + 1) != 0) {
                    return -1;
                }
                has_child = true;
                break;
            }
            if (!has_child)
                break;
        }
    }

    struct EXT2DriverRequest request = {
        .buf = NULL,
        .name = (char *) name,
        .name_len = (uint8_t) str_len(name),
        .parent_inode = parent_inode,
        .buffer_size = 0,
        .is_directory = is_directory
    };
    return delete(request);
}

int shell_rm(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("rm: rm [OPTION] PATH\nRemove a file or directory.\n");
        return 1;
    }
    bool force_recursive = false;
    char *path;
    if (args == NULL || args[1] == NULL) {
        shell_printf("rm: missing operand\n");
        return 1;
    }
    if (streq(args[1], "-rf")) {
        force_recursive = true;
    }
    if (force_recursive) {
        if (args[2] == NULL)
            return 1;
        path = args[2];
    } else {
        path = args[1];
    }
    if (path == NULL) {
        shell_printf("rm: missing operand\n");
        return 1;
    }

    uint32_t parent_inode;
    char name[UINT8_MAX + 1];
    int result = resolve_parent_and_name(path, &parent_inode, name);
    if (result != 0) {
        if (force_recursive && result == 2)
            return 1;
        print_path_error("rm: ", path, result);
        return 1;
    }
    if (streq(name, ".") || streq(name, "..") ||
        !shell_directory_entries_valid(parent_inode)) {
        shell_print_error("rm: cannot remove '", path, "': Invalid path");
        shell_printf("\n");
        return 1;
    }

    uint32_t target_inode;
    bool is_directory;
    int found = find_entry(parent_inode, name, &target_inode, &is_directory);
    if (found == 0) {
        shell_print_error(
            "rm: cannot remove '", path, "': No such file or directory"
        );
        shell_printf("\n");
        return 1;
    }
    if (found < 0) {
        shell_print_error(
            "rm: cannot remove '", path, "': Unable to access directory"
        );
        shell_printf("\n");
        return 1;
    }
    if (is_directory && !force_recursive) {
        shell_print_error("rm: cannot remove '", path, "': Is a directory");
        shell_printf("\n");
        return 1;
    }
    if (is_directory &&
        (target_inode == 1 || target_inode == current_directory_inode)) {
        shell_print_error(
            "rm: cannot remove '", path, "': Operation not permitted"
        );
        shell_printf("\n");
        return 1;
    }

    if (force_recursive) {
        result = shell_rm_entry(parent_inode, name, 0);
    } else {
        struct EXT2DriverRequest request = {
            .buf = NULL,
            .name = name,
            .name_len = (uint8_t) str_len(name),
            .parent_inode = parent_inode,
            .buffer_size = 0,
            .is_directory = is_directory
        };
        result = delete(request);
    }
    if (result == 3) {
        shell_print_error("rm: cannot remove '", path, "': Not a directory");
    } else if (result != 0) {
        shell_print_error("rm: cannot remove '", path, "': Unable to remove");
    } else {
        return 1;
    }
    shell_printf("\n");
    return 1;
}

static void print_path_error(
    const char *command,
    const char *path,
    int result
) {
    const char *reason;
    if (result == 1 || result == 3) {
        reason = ": Not a directory";
    } else if (result == 2) {
        reason = ": No such file or directory";
    } else {
        reason = ": Invalid path";
    }
    shell_print_error(command, path, reason);
    shell_printf("\n");
}

static void print_read_error(
    const char *command,
    const char *path,
    int result
) {
    const char *reason;
    if (result == 1) {
        reason = ": Is a directory";
    } else if (result == 2) {
        reason = ": File is too large";
    } else if (result == 3) {
        reason = ": No such file or directory";
    } else if (result == 4) {
        reason = ": Not a directory";
    } else {
        reason = ": Unable to read file";
    }
    shell_print_error(command, path, reason);
    shell_printf("\n");
}

void start_shell() {
    shell_history.length = 1;
    history_idx = 0;
    cursor_pos = 0;
    history_view_idx = -1;
    history_draft_saved = false;
    shell_printf_row = 0;
    shell_printf_col = 0;
    shell_printf_fg = 0x0F;
    shell_printf_bg = 0;
    shell_input_display_length = 0;
    shell_history.buffer[0].length = 0;
    shell_history.buffer[0].buffer[0] = '\0';
    shell_print_prompt();
}

void add_char(char c) {
    Line *l = &shell_history.buffer[history_idx];
    int end = 0, i;
    while (l->buffer[end] != '\0' && end < SHELL_BUFFER_SIZE - 1)
        end++;
    if (end >= SHELL_BUFFER_SIZE - 1) {
        shell_refresh_input_line();
        return;
    }
    if (cursor_pos < 0 || cursor_pos > end)
        cursor_pos = end;
    for (i = end; i >= cursor_pos; i--) {
        l->buffer[i + 1] = l->buffer[i];
    }
    l->buffer[cursor_pos] = c;
    l->length++;
    cursor_pos++;
    history_view_idx = -1;
    history_draft_saved = false;
    shell_refresh_input_line();
}

void remove_char() {
    Line *l = &shell_history.buffer[history_idx];
    int end = 0, i;
    while (l->buffer[end] != '\0' && end < SHELL_BUFFER_SIZE)
        end++;
    if (cursor_pos <= 0 || cursor_pos > end) {
        shell_refresh_input_line();
        return;
    }
    cursor_pos--;
    for (i = cursor_pos; l->buffer[i] != '\0'; i++)
        l->buffer[i] = l->buffer[i + 1];
    l->length--;
    history_view_idx = -1;
    history_draft_saved = false;
    shell_refresh_input_line();
}

int shell_split_line(
    char *line,
    char **args
) {
    if (line == NULL || args == NULL)
        return -1;
    for (int i = 0; i < SHELL_MAX_ARGS - 1; i++) {
        args[i] = shell_arg_storage[i];
    }
    args[SHELL_MAX_ARGS - 1] = NULL;
    return str_split(line, args, ' ', SHELL_MAX_ARGS - 1);
}

int shell_execute(char **args) {
    int N = (int) (sizeof(builtin_cmd) / sizeof(builtin_cmd[0]));
    if (args == NULL || args[0] == NULL) {
        return 1;
    };
    shell_printf_newline();
    for (int i = 0; i < N; i++) {
        if (streq(args[0], builtin_cmd[i])) {
            return builtin_func[i](args);
        }
    }
    return 1;
}

int enter_command() {
    Line *line = &shell_history.buffer[shell_history.length - 1];
    char *args[SHELL_MAX_ARGS];
    int N = shell_split_line(line->buffer, args);
    (void) N;
    // if (shell_printf_col > 0) shell_printf_newline();
    // char str[2]; str[0] = '0'+N; str[1] = '\0';
    // shell_printf(str);
    if (!shell_execute(args))
        return 0;

    if (line->buffer[0] != '\0' && shell_history.length < SHELL_BUFFER_SIZE) {
        shell_history.length++;
    }
    history_idx = (int) shell_history.length - 1;
    Line *current_line = &shell_history.buffer[history_idx];
    current_line->length = 0;
    current_line->buffer[0] = '\0';
    cursor_pos = 0;
    shell_input_display_length = 0;
    history_view_idx = -1;
    history_draft_saved = false;
    shell_print_prompt();
    return 1;
}

int shell_sudo(char **args) {
    (void) args;
    return 1;
}

int shell_cd(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("cd: cd [path]\nChange the current directory.\n");
        return 1;
    }
    if (args == NULL)
        return 1;
    if (args[1] == NULL) {
        current_directory_inode = 1;
        return 1;
    }
    uint32_t resolved_inode;
    int8_t result =
        resolve_path(current_directory_inode, args[1], &resolved_inode);
    if (result == 0) {
        current_directory_inode = resolved_inode;
    } else if (result == 1 || result == 3) {
        shell_print_error("cd: ", args[1], ": Not a directory");
        shell_printf("\n");
    } else if (result == 2) {
        shell_print_error("cd: ", args[1], ": No such file or directory");
        shell_printf("\n");
    } else {
        shell_print_error("cd: ", args[1], ": Unknown error");
        shell_printf("\n");
    }
    return 1;
}

int shell_ls(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("ls: ls [path]\nList everything in a directory.\n");
        return 1;
    }
    struct EXT2Inode ls_directory;
    struct BlockBuffer directory_buf;
    if (args == NULL)
        return 1;
    if (args[1] != NULL) {
        uint32_t ls_inode;
        int8_t result =
            resolve_path(current_directory_inode, args[1], &ls_inode);
        if (result == 1) {
            // path is a file, split path to directory and file
            char dir[SHELL_BUFFER_SIZE];
            char name[UINT8_MAX + 1];
            if (dir_file_split(args[1], dir, name) != 0) {
                shell_print_error(
                    "ls: cannot access '", args[1],
                    "': No such file or directory"
                );
                shell_printf("\n");
                return 1;
            }
            uint32_t dir_inode;
            result = resolve_path(current_directory_inode, dir, &dir_inode);
            if (result == 1 || result == 3) {
                shell_print_error(
                    "ls: cannot access '", args[1], "': Not a directory"
                );
                shell_printf("\n");
                return 1;
            } else if (result == 2) {
                shell_print_error(
                    "ls: cannot access '", args[1],
                    "': No such file or directory"
                );
                shell_printf("\n");
                return 1;
            } else if (result != 0) {
                shell_print_error(
                    "ls: cannot access '", args[1], "': Unknown error"
                );
                shell_printf("\n");
                return 1;
            }
            struct EXT2DriverRequest request = {
                .buf = &directory_buf,
                .name = name,
                .name_len = (uint8_t) str_len(name),
                .parent_inode = dir_inode
            };
            result = read_directory(&request);
            if (result == 1) {
                shell_printf(name);
                shell_printf("\n");
                return 1;
            }
            if (result != 0) {
                shell_print_error(
                    "ls: cannot access '", args[1],
                    "': No such file or directory"
                );
                shell_printf("\n");
                return 1;
            }
            ls_inode = get_directory_entry(directory_buf.buf, 0)->inode;
        } else if (result != 0) {
            if (result == 1 || result == 3) {
                shell_print_error(
                    "ls: cannot access '", args[1], "': Not a directory"
                );
                shell_printf("\n");
            } else {
                shell_print_error(
                    "ls: cannot access '", args[1],
                    "': No such file or directory"
                );
                shell_printf("\n");
            }
            return 1;
        }
        get_node(&ls_directory, ls_inode);
    } else {
        get_node(&ls_directory, current_directory_inode);
    }
    read_blocks(&directory_buf, ls_directory.i_block[0], 1);
    uint32_t offset = get_dir_first_child_offset(directory_buf.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory_buf.buf, offset);
        if (entry->inode == 0)
            break;
        if (entry->rec_len == 0 || entry->rec_len > BLOCK_SIZE - offset ||
            entry->name_len > UINT8_MAX ||
            sizeof(*entry) + entry->name_len > entry->rec_len) {
            return 1;
        }
        char name[UINT8_MAX + 1];
        strcopy_len(get_entry_name(entry), name, entry->name_len);
        shell_printf(name);
        shell_printf("\n");
        offset += entry->rec_len;
    }
    return 1;
}

int shell_mkdir(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("mkdir: mkdir [directory]\nCreate a directory.\n");
        return 1;
    }
    if (args == NULL || args[1] == NULL)
        return 1;
    if (!shell_directory_entries_valid(current_directory_inode)) {
        shell_repair_empty_directory(current_directory_inode);
    }
    char dir_path[SHELL_BUFFER_SIZE];
    char name[UINT8_MAX + 1];
    if (dir_file_split(args[1], dir_path, name) != 0) {
        shell_print_error(
            "mkdir: cannot create directory '", args[1], "': Invalid path"
        );
        shell_printf("\n");
        return 1;
    }
    uint32_t parent_inode;
    int8_t result =
        resolve_path(current_directory_inode, dir_path, &parent_inode);
    if (result == 1 || result == 3) {
        shell_print_error(
            "mkdir: cannot create directory '", args[1], "': Not a directory"
        );
        shell_printf("\n");
        return 1;
    }
    if (result != 0) {
        shell_print_error(
            "mkdir: cannot create directory '", args[1], "': No such directory"
        );
        shell_printf("\n");
        return 1;
    }
    if (!shell_directory_entries_valid(parent_inode) &&
        (!shell_repair_empty_directory(parent_inode) ||
         !shell_directory_entries_valid(parent_inode))) {
        shell_print_error(
            "mkdir: cannot create directory '", args[1],
            "': Unable to access directory"
        );
        shell_printf("\n");
        return 1;
    }
    struct EXT2DriverRequest request = {
        .buf = NULL,
        .name = name,
        .name_len = (uint8_t) str_len(name),
        .parent_inode = parent_inode,
        .buffer_size = 0,
        .is_directory = true
    };
    struct BlockBuffer existing_directory;
    struct EXT2DriverRequest lookup_request = request;
    lookup_request.buf = &existing_directory;
    int8_t lookup_result = read_directory(&lookup_request);
    if (lookup_result == 0 || lookup_result == 1) {
        shell_print_error(
            "mkdir: cannot create directory '", args[1], "': File exists"
        );
        shell_printf("\n");
        return 1;
    }
    if (lookup_result != 2) {
        shell_print_error(
            "mkdir: cannot create directory '", args[1], "': No such directory"
        );
        shell_printf("\n");
        return 1;
    }
    result = write(&request);
    if (result == 1) {
        shell_print_error(
            "mkdir: cannot create directory '", args[1], "': File exists"
        );
    } else if (result != 0) {
        shell_print_error(
            "mkdir: cannot create directory '", args[1],
            "': Unable to create directory"
        );
    } else {
        uint32_t child_inode = shell_find_child_inode(parent_inode, name);
        if (child_inode == 0 ||
            !shell_initialize_directory(child_inode, parent_inode)) {
            shell_print_error(
                "mkdir: cannot initialize directory '", args[1],
                "': Unable to initialize directory"
            );
            shell_printf("\n");
            return 1;
        }
    }
    if (result != 0)
        shell_printf("\n");
    return 1;
}

int shell_cat(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("cat: cat [file]\nDisplay a file's contents.\n");
        return 1;
    }
    if (args == NULL || args[1] == NULL)
        return 1;
    struct ShellFilePath file;
    int result = resolve_shell_file(args[1], &file);
    if (result != 0) {
        print_path_error("cat: ", args[1], result);
        return 1;
    }
    if (file.is_directory) {
        shell_print_error("cat: ", args[1], ": Is a directory");
        shell_printf("\n");
        return 1;
    }
    result = read_shell_file(&file);
    if (result != 0) {
        print_read_error("cat: ", args[1], result);
        return 1;
    }
    shell_printf_n((const char *) shell_file_buffer, file.size);
    return 1;
}

int shell_grep(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf(
            "grep: grep [PATTERN] [FILE]...\nSearch files for matching lines.\n"
        );
        return 1;
    }
    if (args == NULL || args[1] == NULL) {
        shell_printf("grep: missing pattern\n");
        return 1;
    }
    const char *pattern = args[1];
    if (args[2] == NULL) {
        shell_printf("grep: missing file operand\n");
        return 1;
    }

    int first_file_index = 2;
    int file_count = 0;
    while (args[first_file_index + file_count] != NULL)
        file_count++;

    for (int file_index = 0; file_index < file_count; file_index++) {
        const char *path = args[first_file_index + file_index];
        struct ShellFilePath file;
        int result = resolve_shell_file(path, &file);
        if (result != 0) {
            print_path_error("grep: ", path, result);
            continue;
        }
        if (file.is_directory) {
            shell_print_error("grep: ", path, ": Is a directory");
            shell_printf("\n");
            continue;
        }
        result = read_shell_file(&file);
        if (result != 0) {
            print_read_error("grep: ", path, result);
            continue;
        }

        uint32_t pattern_length = (uint32_t) str_len(pattern);
        uint32_t line_start = 0;
        while (line_start < file.size) {
            uint32_t line_end = line_start;
            while (
                line_end < file.size && shell_file_buffer[line_end] != '\n'
            ) {
                line_end++;
            }
            uint32_t line_length = line_end - line_start;

            bool matched = pattern_length == 0;
            for (
                uint32_t start = 0;
                !matched && start + pattern_length <= line_length; start++
            ) {
                if (memcmp(
                        shell_file_buffer + line_start + start, pattern,
                        pattern_length
                    ) == 0) {
                    matched = true;
                }
            }
            if (matched) {
                if (file_count > 1) {
                    shell_printf(path);
                    shell_printf(":");
                }
                uint32_t output_offset = 0;
                if (pattern_length != 0) {
                    while (output_offset + pattern_length <= line_length) {
                        uint32_t match_offset = output_offset;
                        while (
                            match_offset + pattern_length <= line_length &&
                            memcmp(
                                shell_file_buffer + line_start + match_offset,
                                pattern, pattern_length
                            ) != 0
                        ) {
                            match_offset++;
                        }
                        if (match_offset + pattern_length > line_length)
                            break;

                        shell_printf_n(
                            (const char *) shell_file_buffer + line_start +
                                output_offset,
                            match_offset - output_offset
                        );
                        shell_printf("\x1B[31m");
                        shell_printf_n(
                            (const char *) shell_file_buffer + line_start +
                                match_offset,
                            pattern_length
                        );
                        shell_printf("\x1B[0m");
                        output_offset = match_offset + pattern_length;
                    }
                }
                shell_printf_n(
                    (const char *) shell_file_buffer + line_start +
                        output_offset,
                    line_length - output_offset
                );
                shell_printf("\n");
            }

            if (line_end == file.size)
                break;
            line_start = line_end + 1;
        }
    }
    return 1;
}

int shell_cp(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("cp: cp [source] [destination]\nCopy a file.\n");
        return 1;
    }
    if (args == NULL || args[1] == NULL || args[2] == NULL)
        return 1;
    struct ShellFilePath source;
    int result = resolve_shell_file(args[1], &source);
    if (result != 0) {
        print_path_error("cp: ", args[1], result);
        return 1;
    }
    if (source.is_directory) {
        shell_print_error("cp: cannot copy '", args[1], "': Is a directory");
        shell_printf("\n");
        return 1;
    }

    result = read_shell_file(&source);
    if (result != 0) {
        print_read_error("cp: cannot read '", args[1], result);
        return 1;
    }

    uint32_t destination_parent;
    char destination_name[UINT8_MAX + 1];
    bool destination_exists;
    result = resolve_copy_destination(
        args[2], source.name, &destination_parent, destination_name,
        &destination_exists
    );
    if (result != 0) {
        print_path_error("cp: ", args[2], result);
        return 1;
    }
    if (destination_exists) {
        shell_print_error("cp: cannot create '", args[2], "': File exists");
        shell_printf("\n");
        return 1;
    }

    struct EXT2DriverRequest request = {
        .buf = shell_file_buffer,
        .name = destination_name,
        .name_len = (uint8_t) str_len(destination_name),
        .parent_inode = destination_parent,
        .buffer_size = source.size,
        .is_directory = false,
    };
    result = write(&request);
    if (result == 1) {
        shell_print_error("cp: cannot create '", args[2], "': File exists");
    } else if (result != 0) {
        shell_print_error(
            "cp: cannot create '", args[2], "': Unable to write file"
        );
    }
    if (result != 0) {
        shell_printf("\n");
        return 1;
    }

    return 1;
}

int shell_mv(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("mv: mv [source] [destination]\nMove a file.\n");
        return 1;
    }
    if (args == NULL || args[1] == NULL || args[2] == NULL)
        return 1;

    struct ShellFilePath source;
    int result = resolve_shell_file(args[1], &source);
    if (result != 0) {
        print_path_error("mv: ", args[1], result);
        return 1;
    }
    if (source.is_directory) {
        shell_print_error("mv: cannot move '", args[1], "': Is a directory");
        shell_printf("\n");
        return 1;
    }

    result = read_shell_file(&source);
    if (result != 0) {
        print_read_error("mv: cannot read '", args[1], result);
        return 1;
    }

    uint32_t destination_parent;
    char destination_name[UINT8_MAX + 1];
    bool destination_exists;
    result = resolve_copy_destination(
        args[2], source.name, &destination_parent, destination_name,
        &destination_exists
    );
    if (result != 0) {
        print_path_error("mv: ", args[2], result);
        return 1;
    }
    if (destination_parent == source.parent_inode &&
        streq(destination_name, source.name)) {
        return 1;
    }
    if (destination_exists) {
        shell_print_error("mv: cannot create '", args[2], "': File exists");
        shell_printf("\n");
        return 1;
    }

    struct EXT2DriverRequest destination_request = {
        .buf = shell_file_buffer,
        .name = destination_name,
        .name_len = (uint8_t) str_len(destination_name),
        .parent_inode = destination_parent,
        .buffer_size = source.size,
        .is_directory = false,
    };
    result = write(&destination_request);
    if (result != 0) {
        if (result == 1) {
            shell_print_error("mv: cannot create '", args[2], "': File exists");
        } else {
            shell_print_error(
                "mv: cannot create '", args[2], "': Unable to write file"
            );
        }
        shell_printf("\n");
        return 1;
    }

    struct EXT2DriverRequest source_request = {
        .buf = NULL,
        .name = source.name,
        .name_len = (uint8_t) str_len(source.name),
        .parent_inode = source.parent_inode,
        .buffer_size = 0,
        .is_directory = false
    };
    result = delete(source_request);
    if (result != 0) {
        struct EXT2DriverRequest rollback_request = destination_request;
        rollback_request.buf = NULL;
        rollback_request.buffer_size = 0;
        delete(rollback_request);
        shell_print_error(
            "mv: cannot remove '", args[1], "': Unable to remove file"
        );
        shell_printf("\n");
    }
    return 1;
}

int shell_find(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf(
            "Usage: find [path]\nList files and directories recursively.\n"
        );
        return 1;
    }
    bool root_call = args == NULL || args[1] == NULL;
    uint32_t inode = current_directory_inode;
    if (root_call) {
        shell_find_path[0] = '.';
        shell_find_path[1] = '\0';
    } else {
        int result = resolve_path(current_directory_inode, args[1], &inode);
        if (result != 0)
            return 1;
        strcopy(args[1], shell_find_path);
    }

    shell_printf(shell_find_path);
    shell_printf("\n");
    uint32_t path_len = (uint32_t) str_len(shell_find_path);
    struct EXT2Inode directory;
    get_node(&directory, inode);
    if ((directory.i_mode & EXT2_S_IFDIR) == 0)
        return 1;

    read_blocks(&shell_find_directory_buffer, directory.i_block[0], 1);
    uint32_t offset =
        get_dir_first_child_offset(shell_find_directory_buffer.buf);
    while (offset < BLOCK_SIZE) {
        read_blocks(&shell_find_directory_buffer, directory.i_block[0], 1);
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(shell_find_directory_buffer.buf, offset);
        if (entry->inode == 0 || entry->rec_len == 0 ||
            entry->rec_len > BLOCK_SIZE - offset ||
            sizeof(*entry) + entry->name_len > entry->rec_len) {
            return 1;
        }

        uint32_t next_offset = offset + entry->rec_len;
        bool add_separator =
            path_len == 0 || shell_find_path[path_len - 1] != '/';
        uint32_t separator_len = add_separator ? 1 : 0;
        uint32_t child_path_len = path_len + separator_len + entry->name_len;
        if (child_path_len < SHELL_BUFFER_SIZE) {
            if (add_separator)
                shell_find_path[path_len] = '/';
            memcpy(
                shell_find_path + path_len + separator_len,
                get_entry_name(entry), entry->name_len
            );
            shell_find_path[child_path_len] = '\0';

            if (entry->file_type == EXT2_FT_DIR) {
                char *child_args[] = {"find", shell_find_path, NULL};
                shell_find(child_args);
            } else {
                shell_printf(shell_find_path);
                shell_printf("\n");
            }
            shell_find_path[path_len] = '\0';
        }
        offset = next_offset;
    }
    return 1;
}

int shell_echo(char **args) {
    if (args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("Usage: echo [text]\nDisplay the supplied text.\n");
        return 1;
    }
    if (args == NULL || args[0] == NULL || args[1] == NULL)
        return 1;
    shell_printf(args[1]);
    shell_printf_newline();
    return 1;
}

int shell_pwd(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("pwd: pwd\nPrint the current working directory.\n");
        return 1;
    }
    char path[SHELL_BUFFER_SIZE];
    shell_get_current_path(path);
    shell_printf(path);
    shell_printf_newline();
    return 1;
}

int shell_exit(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("Usage: exit\nExit the shell.\n");
        return 1;
    }
    (void) args;
    exit_all_processes();
}

int shell_badapple(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("Usage: badapple\nPlay the Bad Apple animation.\n");
        return 1;
    }
    play_bad_apple(240, 180); // centers the 160x120 video in the 640x480 screen

    // play_bad_apple clears the screen and draws its centered patch; clear
    // again and reset the cursor so the next prompt starts clean at top-left.
    framebuffer_clear();
    shell_printf_row = 0;
    shell_printf_col = 0;
    return 1;
}

int shell_exec(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf(
            "Usage: exec <program>\nReplace the shell with <program>.\n"
        );
        return 1;
    }
    if (args == NULL || args[1] == NULL) {
        shell_printf("exec: missing program\n");
        return 1;
    }

    struct ShellFilePath file;
    int result = resolve_shell_file(args[1], &file);
    if (result != 0) {
        print_path_error("exec: ", args[1], result);
        return 1;
    }
    if (file.is_directory) {
        shell_print_error("exec: ", args[1], ": Is a directory");
        shell_printf("\n");
        return 1;
    }

    struct EXT2DriverRequest request = {
        .buf = NULL,
        .name = (char *) file.name,
        .name_len = (uint8_t) str_len(file.name),
        .parent_inode = file.parent_inode,
        .buffer_size = file.size,
        .is_directory = false
    };

    int32_t status = exec_process(request);
    if (status != 0) {
        shell_print_error("exec: ", args[1], ": Unable to execute program");
        shell_printf("\n");
    }
    return 1;
}

int shell_ps(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("Usage: ps\nList running processes.\n");
        return 1;
    }

    struct ProcessInfo processes[PROCESS_COUNT_MAX];
    uint32_t count = process_list(processes, PROCESS_COUNT_MAX);

    shell_printf("PID       STATE     PRI  COMMAND\n");
    for (uint32_t i = 0; i < count; i++) {
        shell_print_uint_field(processes[i].pid, 8);
        shell_printf("  ");
        shell_printf(shell_process_state_name(processes[i].state));
        shell_print_uint_field(processes[i].priority, 10);
        shell_printf("  ");
        shell_printf(processes[i].name);
        shell_printf("\n");
    }
    return 1;
}

static bool shell_is_clock_pid(uint32_t pid) {
    struct ProcessInfo processes[PROCESS_COUNT_MAX];
    uint32_t count = process_list(processes, PROCESS_COUNT_MAX);
    for (uint32_t i = 0; i < count; i++) {
        if (processes[i].pid == pid && streq(processes[i].name, "clock")) {
            return true;
        }
    }
    return false;
}

static void shell_clear_clock_display(void) {
    for (
        uint8_t col = (uint8_t) CLOCK_DISPLAY_COLUMN;
        col < (uint8_t) (CLOCK_DISPLAY_COLUMN + CLOCK_DISPLAY_WIDTH); ++col
    ) {
        framebuffer_write((uint8_t) CLOCK_DISPLAY_ROW, col, ' ', 0xF, 0);
    }
}

int shell_kill(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("Usage: kill [-9] <pid>\nTerminate a process.\n");
        return 1;
    }
    if (args == NULL || args[1] == NULL) {
        shell_printf("kill: missing pid\n");
        return 1;
    }

    const char *pid_text = args[1];
    if (streq(pid_text, "-9")) {
        if (args[2] == NULL) {
            shell_printf("kill: missing pid\n");
            return 1;
        }
        pid_text = args[2];
    }

    uint32_t pid;
    if (!shell_parse_uint(pid_text, &pid)) {
        shell_print_error("kill: ", pid_text, ": invalid pid");
        shell_printf("\n");
        return 1;
    }

    bool was_clock = shell_is_clock_pid(pid);
    int32_t result = kill_process(pid);
    if (result != 0) {
        shell_printf("kill: ");
        shell_print_uint(pid);
        shell_printf(": No such process\n");
    } else if (was_clock) {
        shell_clear_clock_display();
    }
    return 1;
}

int shell_clock(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf(
            "Usage: clock\nStart a background clock in the bottom-right.\n"
        );
        return 1;
    }

    uint32_t pid = 0;
    int32_t status = spawn_process("clock", 0, &pid);
    if (status != 0) {
        shell_printf("clock: unable to start background process\n");
        return 1;
    }

    shell_printf("clock: started pid ");
    shell_print_uint(pid);
    shell_printf("\n");
    return 1;
}

int shell_beep(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf(
            "Usage: beep [frequency_hz] [duration_ms]\n"
            "Play a PC speaker tone (defaults: 880 Hz, 120 ms).\n"
        );
        return 1;
    }

    uint32_t frequency = 880u;
    uint32_t duration = 120u;

    if (args != NULL && args[1] != NULL) {
        if (!shell_parse_uint(args[1], &frequency)) {
            shell_printf("beep: invalid frequency\n");
            return 1;
        }
    }
    if (args != NULL && args[2] != NULL) {
        if (!shell_parse_uint(args[2], &duration)) {
            shell_printf("beep: invalid duration\n");
            return 1;
        }
    }

    audio_tone(frequency, duration);
    return 1;
}

int shell_clear(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("Usage: clears the framebuffer.\n");
        return 1;
    }

    framebuffer_clear();
    shell_printf_row = 0;
    shell_printf_col = 0;
    return 1;
}

int shell_help(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf(
            "Usage: displays a helpful command (list of commands available in "
            "this shell).\n"
        );
        return 1;
    }

    shell_printf(
        "File & Directory Management: pwd, cd, ls, mkdir, cp, mv, rm\n"
    );
    shell_printf("Text Processing & Search: cat, grep, find, echo\n");
    shell_printf("Process: sudo, ps, kill, exec\n");
    shell_printf("Terminal & Session Control: clear, exit, help\n");
    shell_printf("Utils, Misc: badapple, beep, clock\n");

    return 1;
}

int shell_pacman(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf(
            "Usage: pacman! a tribute to Arch Linux's pacman easter egg.\n"
        );
        return 1;
    }

    const uint32_t width = 40;
    char frame[96];

    for (uint32_t i = 0; i <= width; i++) {
        uint32_t n = 0;
        const char *head = "\r[";
        while (*head)
            frame[n++] = *head++;

        for (uint32_t j = 0; j < i; j++) // eaten trail
            frame[n++] = '-';

        const char *pac =
            (i % 2 == 0) ? "\x1B[1;33mC\x1B[0m" : "\x1B[1;33mc\x1B[0m";
        while (*pac)
            frame[n++] = *pac++;

        for (uint32_t j = i + 1; j <= width; j++) // dots still ahead
            frame[n++] = (j % 3 == 0) ? 'o' : ' ';

        frame[n++] = ']';
        shell_printf_n(frame, n);

        sleep_ms(80);
    }
    shell_printf("\n");

    return 1;
}
