#include "shell.h"
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
static uint8_t shell_file_buffer[USER_FILE_BUFFER_SIZE];
static char shell_arg_storage[SHELL_MAX_ARGS - 1][SHELL_BUFFER_SIZE];
static char shell_find_path[SHELL_BUFFER_SIZE];
static struct BlockBuffer shell_find_directory_buffer;

const char *builtin_cmd[] = {
    "cd",
    "ls",
    "mkdir",
    "cat",
    "cp",
    "mv",
    "find",
    "echo",
    "pwd",
    "exit",
    "badapple"
};

int (*builtin_func[]) (char **) = {
    &shell_cd,
    &shell_ls,
    &shell_mkdir,
    &shell_cat,
    &shell_cp,
    &shell_mv,
    &shell_find,
    &shell_echo,
    &shell_pwd,
    &shell_exit,
    &shell_badapple
};

int streq(const char str1[], const char str2[]) {
    int i = 0;
    while (str1[i] != '\0' && str1[i] == str2[i]) i++;
    return str1[i] == str2[i];
}

int str_len(const char *str) {
    int len = 0;
    while (str[len] != '\0') len++;
    return len;
}

void strcopy(const char src[], char dest[]) {
    int len = str_len(src), i;
    for (i = 0; i < len; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

void strcopy_len(const char src[], char dest[], int len) {
    int i = 0;
    for (i=0; i<len; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

int str_split(const char *string, char **parts, char keychar, int max_parts) {
    if (string == NULL  || parts == NULL || max_parts <= 0) return -1;
    int i, amount = 0, start = 0, len = 0;
    for (i=0; string[i] != '\0' && amount < max_parts; i++) {
        if (string[i] == keychar) {
            strcopy_len(&(string[start]), parts[amount], len);
            amount++;
            start = i+1;
            len = 0;
        }
        else {
            len++;
        }
    }
    if (amount < max_parts) {
        strcopy_len(&(string[start]), parts[amount], len);
        amount++;
        parts[amount] = NULL;
        return amount;
    }
    else {
        return -1;
    }
}

int resolve_path_string(const char *path, char layers[][SHELL_PATH_SEGMENT_MAX]) {
    if (path == NULL || layers == NULL) return -1;

    char *layer_ptrs[SHELL_PATH_DEPTH_MAX];
    for (int i = 0; i < SHELL_PATH_DEPTH_MAX; i++) {
        layer_ptrs[i] = layers[i];
    }

    if (path[0] == '/') {
        layers[0][0] = '/';
        layers[0][1] = '\0';
        int result = str_split(path, &layer_ptrs[1], '/', SHELL_PATH_DEPTH_MAX-1);
        if (result == -1) {
            return -1;
        }
        else {
            return 1 + result;
        }
    }
    else {
        return str_split(path, layer_ptrs, '/', SHELL_PATH_DEPTH_MAX);
    }
}

int dir_file_split(const char *path, char *dir, char *name) {
    if (path == NULL || dir == NULL || name == NULL) return -1;
    char layers[SHELL_PATH_DEPTH_MAX][SHELL_PATH_SEGMENT_MAX];
    int depth = resolve_path_string(path, layers);
    if (depth == -1) return -1;
    strcopy(layers[depth-1], name);
    strcopy_len(path, dir, str_len(path)-str_len(name));
    return 0;
}

static bool shell_directory_entries_valid(uint32_t inode) {
    struct EXT2Inode directory;
    struct BlockBuffer directory_buf;
    get_node(&directory, inode);
    if ((directory.i_mode & EXT2_S_IFDIR) == 0) return true;

    read_blocks(&directory_buf, directory.i_block[0], 1);
    uint32_t offset = get_dir_first_child_offset(directory_buf.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory_buf.buf, offset);
        if (entry->inode == 0) return true;
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
    if ((directory.i_mode & EXT2_S_IFDIR) == 0) return false;

    read_blocks(&directory_buf, directory.i_block[0], 1);
    struct EXT2DirectoryEntry *first =
        get_directory_entry(directory_buf.buf, 0);
    if (first->name_len != 2 || first->rec_len != BLOCK_SIZE - get_entry_record_len(1) ||
        memcmp(get_entry_name(first), "..", 2) != 0) {
        return false;
    }

    uint32_t parent_inode = first->inode;
    memset(&directory_buf, 0, sizeof(directory_buf));
    struct EXT2DirectoryEntry *dot =
        get_directory_entry(directory_buf.buf, 0);
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

static bool shell_initialize_directory(uint32_t inode, uint32_t parent_inode) {
    struct EXT2Inode directory;
    struct BlockBuffer directory_buf;
    get_node(&directory, inode);
    if ((directory.i_mode & EXT2_S_IFDIR) == 0) return false;

    memset(&directory_buf, 0, sizeof(directory_buf));
    struct EXT2DirectoryEntry *dot =
        get_directory_entry(directory_buf.buf, 0);
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

static uint32_t shell_find_child_inode(uint32_t parent_inode, const char *name) {
    struct EXT2Inode parent;
    struct BlockBuffer directory_buf;
    get_node(&parent, parent_inode);
    read_blocks(&directory_buf, parent.i_block[0], 1);

    uint32_t offset = 0;
    uint32_t name_len = (uint32_t)str_len(name);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory_buf.buf, offset);
        if (entry->inode == 0) return 0;
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

int8_t resolve_path(uint32_t start_inode, const char *path, uint32_t *resolved_inode) {
    uint32_t current_inode = start_inode;
    if (path == NULL || resolved_inode == NULL) return -1;
    if (path[0] == '/') current_inode = 1;
    while (1) {
        while (*path == '/') path++;
        if (*path == '\0') {
            *resolved_inode = current_inode;
            return 0;
        }
        uint32_t name_len = 0;
        while (path[name_len] != '\0' && path[name_len] != '/') name_len++;
        if (name_len > UINT8_MAX) return -1;
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
            if (!shell_directory_entries_valid(current_inode)) return 2;
            char name[UINT8_MAX + 1];
            strcopy_len(path, name, name_len);
            struct BlockBuffer directory_buf;
            struct EXT2DriverRequest request = {
                .buf = &directory_buf,
                .name = name,
                .name_len = (uint8_t)name_len,
                .parent_inode = current_inode
            };
            int8_t result = read_directory(&request);
            if (result != 0) return result;
            struct EXT2DirectoryEntry *dot = get_directory_entry(directory_buf.buf, 0);
            current_inode = dot->inode;
        }
        path += name_len;
    }
}

static void shell_printf_newline(void) {
    if (shell_printf_row + 1 < FRAMEBUFFER_HEIGHT) {
        shell_printf_row++;
    } else {
        framebuffer_scroll_up();
    }
    shell_printf_col = 0;
}

static void shell_printf_n(const char *message, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        if (message[i] == '\n') {
            shell_printf_newline();
        } else {
            if (shell_printf_col >= FRAMEBUFFER_WIDTH) {
                shell_printf_newline();
            }
            framebuffer_write(
                shell_printf_row,
                shell_printf_col,
                message[i],
                0xF,
                0
            );
            shell_printf_col++;
        }
    }
}

static void shell_printf(const char *message) {
    shell_printf_n(message, (uint32_t)str_len(message));
}

static void shell_print_error(const char *prefix, const char *name, const char *suffix) {
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
        struct EXT2DirectoryEntry *dotdot = get_next_directory_entry(
            get_directory_entry(directory_buf.buf, 0)
        );
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
                memcpy(reverse_path + reverse_len, get_entry_name(entry), entry->name_len);
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
        while (end > 0 && reverse_path[end - 1] == '/') end--;
        uint32_t component_end = end;
        while (end > 0 && reverse_path[end - 1] != '/') end--;
        if (path_len > 1) path[path_len++] = '/';
        for (uint32_t i = end; i < component_end; i++) {
            path[path_len++] = reverse_path[i];
        }
    }
    path[path_len] = '\0';
}

static void shell_print_prompt(void) {
    char path[SHELL_BUFFER_SIZE];
    shell_get_current_path(path);
    shell_printf("User@OSuu:");
    shell_printf(path);
    shell_printf("$ ");
    if (shell_printf_col >= FRAMEBUFFER_WIDTH) shell_printf_newline();
    uint8_t row;
    uint8_t col;
    shell_get_cursor_position(&row, &col);
    framebuffer_set_cursor(row, col);
}

static void shell_refresh_input_line(void) {
    Line *line = &shell_history.buffer[history_idx];
    uint8_t input_row = shell_printf_row;
    uint32_t line_length = (uint32_t)str_len(line->buffer);

    for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++) {
        framebuffer_write(input_row, col, ' ', 0xF, 0);
    }

    shell_printf_col = 0;
    shell_print_prompt();
    uint8_t input_start_col = shell_printf_col;
    for (uint32_t i = 0;
         i < line_length && input_start_col + i < FRAMEBUFFER_WIDTH;
         i++) {
        framebuffer_write(input_row, input_start_col + i, line->buffer[i], 0xF, 0);
    }

    uint32_t cursor_col = input_start_col + (uint32_t)cursor_pos;
    if (cursor_col >= FRAMEBUFFER_WIDTH) cursor_col = FRAMEBUFFER_WIDTH - 1;
    shell_printf_col = input_start_col;
    framebuffer_set_cursor(input_row, (uint8_t)cursor_col);
}

void shell_get_cursor_position(uint8_t *row, uint8_t *col) {
    uint32_t cursor = shell_printf_row * FRAMEBUFFER_WIDTH +
        shell_printf_col + (uint32_t)cursor_pos;
    if (cursor >= FRAMEBUFFER_HEIGHT * FRAMEBUFFER_WIDTH) {
        cursor = FRAMEBUFFER_HEIGHT * FRAMEBUFFER_WIDTH - 1;
    }
    if (row != NULL) *row = (uint8_t)(cursor / FRAMEBUFFER_WIDTH);
    if (col != NULL) *col = (uint8_t)(cursor % FRAMEBUFFER_WIDTH);
}

void shell_cursor_left(uint8_t *row, uint8_t *col) {
    if (cursor_pos > 0) cursor_pos--;
    shell_get_cursor_position(row, col);
}

void shell_cursor_right(uint8_t *row, uint8_t *col) {
    Line *line = &shell_history.buffer[history_idx];
    if (cursor_pos < str_len(line->buffer)) cursor_pos++;
    shell_get_cursor_position(row, col);
}

static void shell_show_history_line(
    const char *line,
    uint32_t previous_length,
    uint8_t *row,
    uint8_t *col
) {
    uint32_t line_length = (uint32_t)str_len(line);
    uint32_t display_length = previous_length > line_length
        ? previous_length : line_length;
    uint32_t start = shell_printf_row * FRAMEBUFFER_WIDTH + shell_printf_col;
    uint32_t screen_size = FRAMEBUFFER_HEIGHT * FRAMEBUFFER_WIDTH;

    for (uint32_t i = 0; i < display_length && start + i < screen_size; i++) {
        char character = i < line_length ? line[i] : ' ';
        uint32_t position = start + i;
        framebuffer_write(
            (uint8_t)(position / FRAMEBUFFER_WIDTH),
            (uint8_t)(position % FRAMEBUFFER_WIDTH),
            character,
            0xF,
            0
        );
    }

    uint32_t cursor = start + (uint32_t)cursor_pos;
    if (cursor >= screen_size) cursor = screen_size - 1;
    *row = (uint8_t)(cursor / FRAMEBUFFER_WIDTH);
    *col = (uint8_t)(cursor % FRAMEBUFFER_WIDTH);
}

void shell_history_up(uint8_t *row, uint8_t *col) {
    if (row == NULL || col == NULL || shell_history.length < 2) return;
    uint16_t last_index = shell_history.length - 1;
    Line *current_line = &shell_history.buffer[last_index];
    uint32_t previous_length = (uint32_t)str_len(current_line->buffer);

    if (history_view_idx < 0) {
        strcopy(current_line->buffer, shell_history_backup.buffer[0].buffer);
        shell_history_backup.buffer[0].length = current_line->length;
        history_draft_saved = true;
        history_view_idx = (int)last_index - 1;
    } else if (history_view_idx > 0) {
        history_view_idx--;
    }

    Line *history_line = &shell_history.buffer[history_view_idx];
    strcopy(history_line->buffer, current_line->buffer);
    current_line->length = (uint16_t)str_len(current_line->buffer);
    cursor_pos = (int)current_line->length;
    shell_show_history_line(current_line->buffer, previous_length, row, col);
}

void shell_history_down(uint8_t *row, uint8_t *col) {
    if (row == NULL || col == NULL || history_view_idx < 0) return;
    uint16_t last_index = shell_history.length - 1;
    Line *current_line = &shell_history.buffer[last_index];
    uint32_t previous_length = (uint32_t)str_len(current_line->buffer);

    if (history_view_idx < (int)last_index - 1) {
        history_view_idx++;
        Line *history_line = &shell_history.buffer[history_view_idx];
        strcopy(history_line->buffer, current_line->buffer);
    } else {
        history_view_idx = -1;
        if (history_draft_saved) {
            strcopy(shell_history_backup.buffer[0].buffer, current_line->buffer);
            current_line->length = (uint16_t)str_len(current_line->buffer);
        }
        history_draft_saved = false;
    }

    current_line->length = (uint16_t)str_len(current_line->buffer);
    cursor_pos = (int)current_line->length;
    shell_show_history_line(current_line->buffer, previous_length, row, col);
}

static int resolve_parent_and_name(
    const char *path,
    uint32_t *parent_inode,
    char name[UINT8_MAX + 1]
) {
    if (path == NULL || parent_inode == NULL || name == NULL) return -1;

    uint32_t path_len = (uint32_t)str_len(path);
    if (path_len == 0 || path_len >= SHELL_BUFFER_SIZE || path[path_len - 1] == '/') {
        return -1;
    }

    uint32_t name_start = path_len;
    while (name_start > 0 && path[name_start - 1] != '/') name_start--;
    uint32_t name_len = path_len - name_start;
    if (name_len == 0 || name_len > UINT8_MAX) return -1;
    strcopy_len(path + name_start, name, (int)name_len);

    char parent_path[SHELL_BUFFER_SIZE];
    if (name_start == 0) {
        parent_path[0] = '\0';
    } else if (name_start == 1) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        strcopy_len(path, parent_path, (int)(name_start - 1));
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
    if ((parent_node.i_mode & EXT2_S_IFDIR) == 0) return -1;

    struct BlockBuffer directory_buf;
    read_blocks(&directory_buf, parent_node.i_block[0], 1);

    uint32_t name_len = (uint32_t)str_len(name);
    uint32_t offset = get_dir_first_child_offset(directory_buf.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory_buf.buf, offset);
        if (entry->inode == 0) return 0;
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

static int resolve_shell_file(const char *path, struct ShellFilePath *file) {
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
    if (result != 1) return result;

    uint32_t path_len = (uint32_t)str_len(path);
    if (path_len > 0 && path[path_len - 1] == '/') return 1;

    result = resolve_parent_and_name(path, &file->parent_inode, file->name);
    if (result != 0) return result;

    bool entry_is_directory;
    int found = find_entry(file->parent_inode, file->name, &file->inode, &entry_is_directory);
    if (found == 0) return 2;
    if (found < 0) return -1;

    struct EXT2Inode node;
    get_node(&node, file->inode);
    file->is_directory = entry_is_directory ||
        (node.i_mode & EXT2_S_IFDIR) != 0;
    file->size = node.i_size;
    // Bounded by shell_file_buffer's actual size (USER_FILE_BUFFER_SIZE),
    // not the real 4 MiB disk - the ported shell's buffer is deliberately
    // much smaller than the kernel version's DISK_SPACE-sized one.
    if (file->size > USER_FILE_BUFFER_SIZE) return -1;
    return 0;
}

static int read_shell_file(const struct ShellFilePath *file) {
    struct EXT2DriverRequest request = {
        .buf = shell_file_buffer,
        .name = (char *)file->name,
        .name_len = (uint8_t)str_len(file->name),
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
        if ((node.i_mode & EXT2_S_IFDIR) == 0) return 1;
        *parent_inode = directory_inode;
        strcopy(source_name, name);
    } else {
        uint32_t path_len = (uint32_t)str_len(path);
        if (result == 1 && path_len > 0 && path[path_len - 1] == '/') return 1;
        result = resolve_parent_and_name(path, parent_inode, name);
        if (result != 0) return result;
    }

    uint32_t existing_inode;
    bool existing_is_directory;
    int found = find_entry(*parent_inode, name, &existing_inode, &existing_is_directory);
    if (found < 0) return -1;
    *already_exists = found == 1;
    return 0;
}

static void print_path_error(const char *command, const char *path, int result) {
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

static void print_read_error(const char *command, const char *path, int result) {
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
    shell_history.buffer[0].length = 0;
    shell_history.buffer[0].buffer[0] = '\0';
    shell_print_prompt();
}

void add_char(char c) {
    Line *l = &shell_history.buffer[history_idx];
    int end = 0, i;
    while (l->buffer[end] != '\0' && end < SHELL_BUFFER_SIZE - 1) end++;
    if (end >= SHELL_BUFFER_SIZE - 1) {
        shell_refresh_input_line();
        return;
    }
    if (cursor_pos < 0 || cursor_pos > end) cursor_pos = end;
    for (i=end; i>=cursor_pos; i--) {
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
    while (l->buffer[end] != '\0' && end < SHELL_BUFFER_SIZE) end++;
    if (cursor_pos <= 0 || cursor_pos > end) {
        shell_refresh_input_line();
        return;
    }
    cursor_pos--;
    for (i=cursor_pos; l->buffer[i] != '\0'; i++)
        l->buffer[i] = l->buffer[i + 1];
    l->length--;
    history_view_idx = -1;
    history_draft_saved = false;
    shell_refresh_input_line();
}

int shell_split_line(char *line, char **args) {
    if (line == NULL || args == NULL) return -1;
    for (int i = 0; i < SHELL_MAX_ARGS - 1; i++) {
        args[i] = shell_arg_storage[i];
    }
    args[SHELL_MAX_ARGS - 1] = NULL;
    return str_split(line, args, ' ', SHELL_MAX_ARGS - 1);
}

int shell_execute(char **args) {
    int N = (int)(sizeof(builtin_cmd) / sizeof(builtin_cmd[0]));
    if (args == NULL || args[0] == NULL) { return 1; };
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
    if (!shell_execute(args)) return 0;

    if (line->buffer[0] != '\0' && shell_history.length < SHELL_BUFFER_SIZE) {
        shell_history.length++;
    }
    history_idx = (int)shell_history.length - 1;
    Line *current_line = &shell_history.buffer[history_idx];
    current_line->length = 0;
    current_line->buffer[0] = '\0';
    cursor_pos = 0;
    history_view_idx = -1;
    history_draft_saved = false;
    shell_print_prompt();
    return 1;
}

int shell_cd(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("cd: cd [path]\nChange the current directory.\n");
        return 1;
    }
    if (args == NULL) return 1;
    if (args[1] == NULL) {
        current_directory_inode = 1;
        return 1;
    }
    uint32_t resolved_inode;
    int8_t result = resolve_path(current_directory_inode, args[1], &resolved_inode);
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
    if (args == NULL) return 1;
    if (args[1] != NULL) {
        uint32_t ls_inode;
        int8_t result = resolve_path(current_directory_inode, args[1], &ls_inode);
        if (result == 1) {
            // path is a file, split path to directory and file
            char dir[SHELL_BUFFER_SIZE];
            char name[UINT8_MAX + 1];
            if (dir_file_split(args[1], dir, name) != 0) {
                shell_print_error("ls: cannot access '", args[1], "': No such file or directory");
                shell_printf("\n");
                return 1;
            }
            uint32_t dir_inode;
            result = resolve_path(current_directory_inode, dir, &dir_inode);
            if (result == 1 || result == 3) {
                shell_print_error("ls: cannot access '", args[1], "': Not a directory");
                shell_printf("\n");
                return 1;
            } else if (result == 2) {
                shell_print_error("ls: cannot access '", args[1], "': No such file or directory");
                shell_printf("\n");
                return 1;
            } else if (result != 0) {
                shell_print_error("ls: cannot access '", args[1], "': Unknown error");
                shell_printf("\n");
                return 1;
            }
            struct EXT2DriverRequest request = {
                .buf = &directory_buf,
                .name = name,
                .name_len = (uint8_t)str_len(name),
                .parent_inode = dir_inode
            };
            result = read_directory(&request);
            if (result == 1) {
                shell_printf(name);
                shell_printf("\n");
                return 1;
            }
            if (result != 0) {
                shell_print_error("ls: cannot access '", args[1], "': No such file or directory");
                shell_printf("\n");
                return 1;
            }
            ls_inode = get_directory_entry(directory_buf.buf, 0)->inode;
        } else if (result != 0) {
            if (result == 1 || result == 3) {
                shell_print_error("ls: cannot access '", args[1], "': Not a directory");
                shell_printf("\n");
            } else {
                shell_print_error("ls: cannot access '", args[1], "': No such file or directory");
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
        struct EXT2DirectoryEntry *entry = get_directory_entry(directory_buf.buf, offset);
        if (entry->inode == 0) break;
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
    if (args == NULL || args[1] == NULL) return 1;
    if (!shell_directory_entries_valid(current_directory_inode)) {
        shell_repair_empty_directory(current_directory_inode);
    }
    char dir_path[SHELL_BUFFER_SIZE];
    char name[UINT8_MAX + 1];
    if (dir_file_split(args[1], dir_path, name) != 0) {
        shell_print_error("mkdir: cannot create directory '", args[1], "': Invalid path");
        shell_printf("\n");
        return 1;
    }
    uint32_t parent_inode;
    int8_t result = resolve_path(current_directory_inode, dir_path, &parent_inode);
    if (result == 1 || result == 3) {
        shell_print_error("mkdir: cannot create directory '", args[1], "': Not a directory");
        shell_printf("\n");
        return 1;
    }
    if (result != 0) {
        shell_print_error("mkdir: cannot create directory '", args[1], "': No such directory");
        shell_printf("\n");
        return 1;
    }
    if (!shell_directory_entries_valid(parent_inode) &&
        (!shell_repair_empty_directory(parent_inode) ||
         !shell_directory_entries_valid(parent_inode))) {
        shell_print_error("mkdir: cannot create directory '", args[1], "': Unable to access directory");
        shell_printf("\n");
        return 1;
    }
    struct EXT2DriverRequest request = {
        .buf = NULL,
        .name = name,
        .name_len = (uint8_t)str_len(name),
        .parent_inode = parent_inode,
        .buffer_size = 0,
        .is_directory = true
    };
    struct BlockBuffer existing_directory;
    struct EXT2DriverRequest lookup_request = request;
    lookup_request.buf = &existing_directory;
    int8_t lookup_result = read_directory(&lookup_request);
    if (lookup_result == 0 || lookup_result == 1) {
        shell_print_error("mkdir: cannot create directory '", args[1], "': File exists");
        shell_printf("\n");
        return 1;
    }
    if (lookup_result != 2) {
        shell_print_error("mkdir: cannot create directory '", args[1], "': No such directory");
        shell_printf("\n");
        return 1;
    }
    result = write(&request);
    if (result == 1) {
        shell_print_error("mkdir: cannot create directory '", args[1], "': File exists");
    } else if (result != 0) {
        shell_print_error("mkdir: cannot create directory '", args[1], "': Unable to create directory");
    } else {
        uint32_t child_inode = shell_find_child_inode(parent_inode, name);
        if (child_inode == 0 ||
            !shell_initialize_directory(child_inode, parent_inode)) {
            shell_print_error("mkdir: cannot initialize directory '", args[1], "': Unable to initialize directory");
            shell_printf("\n");
            return 1;
        }
    }
    if (result != 0) shell_printf("\n");
    return 1;
}

int shell_cat(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("cat: cat [file]\nDisplay a file's contents.\n");
        return 1;
    }
    if (args == NULL || args[1] == NULL) return 1;
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
    shell_printf_n((const char *)shell_file_buffer, file.size);
    return 1;
}

int shell_cp(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("cp: cp [source] [destination]\nCopy a file.\n");
        return 1;
    }
    if (args == NULL || args[1] == NULL || args[2] == NULL) return 1;
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
        .name_len = (uint8_t)str_len(destination_name),
        .parent_inode = destination_parent,
        .buffer_size = source.size,
        .is_directory = false,
    };
    result = write(&request);
    if (result == 1) {
        shell_print_error("cp: cannot create '", args[2], "': File exists");
    } else if (result != 0) {
        shell_print_error("cp: cannot create '", args[2], "': Unable to write file");
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
    if (args == NULL || args[1] == NULL || args[2] == NULL) return 1;

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
        .name_len = (uint8_t)str_len(destination_name),
        .parent_inode = destination_parent,
        .buffer_size = source.size,
        .is_directory = false,
    };
    result = write(&destination_request);
    if (result != 0) {
        if (result == 1) {
            shell_print_error("mv: cannot create '", args[2], "': File exists");
        } else {
            shell_print_error("mv: cannot create '", args[2], "': Unable to write file");
        }
        shell_printf("\n");
        return 1;
    }

    struct EXT2DriverRequest source_request = {
        .buf = NULL,
        .name = source.name,
        .name_len = (uint8_t)str_len(source.name),
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
        shell_print_error("mv: cannot remove '", args[1], "': Unable to remove file");
        shell_printf("\n");
    }
    return 1;
}

int shell_find(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("Usage: find [path]\nList files and directories recursively.\n");
        return 1;
    }
    bool root_call = args == NULL || args[1] == NULL;
    uint32_t inode = current_directory_inode;
    if (root_call) {
        shell_find_path[0] = '.';
        shell_find_path[1] = '\0';
    } else {
        int result = resolve_path(
            current_directory_inode,
            args[1],
            &inode
        );
        if (result != 0) return 1;
        strcopy(args[1], shell_find_path);
    }

    shell_printf(shell_find_path);
    shell_printf("\n");
    uint32_t path_len = (uint32_t)str_len(shell_find_path);
    struct EXT2Inode directory;
    get_node(&directory, inode);
    if ((directory.i_mode & EXT2_S_IFDIR) == 0) return 1;

    read_blocks(
        &shell_find_directory_buffer,
        directory.i_block[0],
        1
    );
    uint32_t offset = get_dir_first_child_offset(
        shell_find_directory_buffer.buf
    );
    while (offset < BLOCK_SIZE) {
        read_blocks(
            &shell_find_directory_buffer,
            directory.i_block[0],
            1
        );
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(shell_find_directory_buffer.buf, offset);
        if (entry->inode == 0 || entry->rec_len == 0 ||
            entry->rec_len > BLOCK_SIZE - offset ||
            sizeof(*entry) + entry->name_len > entry->rec_len) {
            return 1;
        }

        uint32_t next_offset = offset + entry->rec_len;
        bool add_separator = path_len == 0 || shell_find_path[path_len - 1] != '/';
        uint32_t separator_len = add_separator ? 1 : 0;
        uint32_t child_path_len = path_len + separator_len + entry->name_len;
        if (child_path_len < SHELL_BUFFER_SIZE) {
            if (add_separator) shell_find_path[path_len] = '/';
            memcpy(
                shell_find_path + path_len + separator_len,
                get_entry_name(entry),
                entry->name_len
            );
            shell_find_path[child_path_len] = '\0';

            if (entry->file_type == EXT2_FT_DIR) {
                char *child_args[] = {
                    "find",
                    shell_find_path,
                    NULL
                };
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
    if (args == NULL || args[0] == NULL  || args[1] == NULL) return 1;
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
    (void)args;
    return 0;
}

int shell_badapple(char **args) {
    if (args != NULL && args[1] != NULL && streq(args[1], "--help")) {
        shell_printf("Usage: badapple\nPlay the Bad Apple animation.\n");
        return 1;
    }
    play_bad_apple(240, 180); // centers the 160x120 video in the 640x480 screen

    // play_bad_apple only touches its own centered 160x120 patch, leaving
    // whatever shell text was already on screen around it - clear the whole
    // screen and reset to the top-left so the next prompt starts clean.
    framebuffer_clear();
    shell_printf_row = 0;
    shell_printf_col = 0;
    return 1;
}

