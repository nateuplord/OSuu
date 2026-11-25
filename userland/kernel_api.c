#include "kernel_api.h"
#include "syscall.h"
#include <stddef.h>

int8_t read(struct EXT2DriverRequest request) {
    int8_t result;
    syscall(SYSCALL_FS_READ, (uint32_t) &request, (uint32_t) &result, 0);
    return result;
}

int8_t read_directory(struct EXT2DriverRequest *request) {
    int8_t result;
    syscall(
        SYSCALL_FS_READ_DIRECTORY, (uint32_t) request, (uint32_t) &result, 0
    );
    return result;
}

int8_t write(struct EXT2DriverRequest *request) {
    int8_t result;
    syscall(SYSCALL_FS_WRITE, (uint32_t) request, (uint32_t) &result, 0);
    return result;
}

int8_t delete(struct EXT2DriverRequest request) {
    int8_t result;
    syscall(SYSCALL_FS_DELETE, (uint32_t) &request, (uint32_t) &result, 0);
    return result;
}

void get_node(
    struct EXT2Inode *node,
    uint32_t inode
) {
    syscall(SYSCALL_FS_GET_NODE, (uint32_t) node, inode, 0);
}

void sync_node(
    struct EXT2Inode *node,
    uint32_t inode
) {
    syscall(SYSCALL_FS_SYNC_NODE, (uint32_t) node, inode, 0);
}

void read_blocks(
    void *ptr,
    uint32_t logical_block_address,
    uint8_t block_count
) {
    syscall(
        SYSCALL_FS_READ_BLOCKS, (uint32_t) ptr, logical_block_address,
        block_count
    );
}

void write_blocks(
    const void *ptr,
    uint32_t logical_block_address,
    uint8_t block_count
) {
    syscall(
        SYSCALL_FS_WRITE_BLOCKS, (uint32_t) ptr, logical_block_address,
        block_count
    );
}

void framebuffer_write(
    uint8_t row,
    uint8_t col,
    char c,
    uint8_t fg,
    uint8_t bg
) {
    // The syscall always writes white-on-black - matches every real call
    // site in shell.c, so fg/bg are accepted but ignored here.
    (void) fg;
    (void) bg;
    syscall(SYSCALL_TEXT_WRITE_AT, row, col, (uint8_t) c);
}

void framebuffer_putchar(
    char c,
    uint8_t attribute
) {
    syscall(SYSCALL_TEXT_PUTCHAR, (uint8_t) c, attribute, 0);
}

void framebuffer_puts(
    const char *buf,
    uint32_t count,
    uint8_t attribute
) {
    syscall(SYSCALL_TEXT_PUTS, (uint32_t) buf, count, attribute);
}

void framebuffer_set_cursor(
    uint8_t r,
    uint8_t c
) {
    syscall(SYSCALL_TEXT_SET_CURSOR, r, c, 0);
}

void framebuffer_scroll_up(void) {
    syscall(SYSCALL_TEXT_SCROLL_UP, 0, 0, 0);
}

void framebuffer_clear(void) {
    syscall(SYSCALL_TEXT_CLEAR, 0, 0, 0);
}

void gui_fill_rect(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    uint8_t color
) {
    struct GuiRect rect = {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .color = color,
    };
    syscall(SYSCALL_GUI_FILL_RECT, (uint32_t) &rect, 0, 0);
}

void play_bad_apple(
    uint16_t offset_x,
    uint16_t offset_y
) {
    syscall(SYSCALL_PLAY_BADAPPLE, offset_x, offset_y, 0);
}

void get_keyboard_buffer(char *buf) {
    syscall(SYSCALL_KEYBOARD_GETCHAR, (uint32_t) buf, 0, 0);
}

void poll_keyboard_buffer(char *buf) {
    syscall(SYSCALL_KEYBOARD_POLL, (uint32_t) buf, 0, 0);
}

void keyboard_state_activate(void) {
    syscall(SYSCALL_KEYBOARD_ACTIVATE, 0, 0, 0);
}

__attribute__((noreturn)) void exit_process(void) {
    syscall(SYSCALL_PROCESS_EXIT, 0, 0, 0);
    __builtin_unreachable();
}

__attribute__((noreturn)) void exit_all_processes(void) {
    syscall(SYSCALL_PROCESS_EXIT_ALL, 0, 0, 0);
    __builtin_unreachable();
}

uint32_t getpid(void) {
    uint32_t pid = 0;
    syscall(SYSCALL_PROCESS_GETPID, (uint32_t) &pid, 0, 0);
    return pid;
}

void yield(void) {
    syscall(SYSCALL_PROCESS_YIELD, 0, 0, 0);
}

int32_t exec_process(struct EXT2DriverRequest request) {
    int32_t result = 0;
    syscall(SYSCALL_PROCESS_EXEC, (uint32_t) &request, (uint32_t) &result, 0);
    return result;
}

uint32_t process_list(
    struct ProcessInfo *buffer,
    uint32_t capacity
) {
    uint32_t count = 0;
    syscall(
        SYSCALL_PROCESS_LIST, (uint32_t) &count, (uint32_t) buffer, capacity
    );
    return count;
}

int32_t kill_process(uint32_t pid) {
    int32_t result = 1;
    syscall(SYSCALL_PROCESS_KILL, pid, (uint32_t) &result, 0);
    return result;
}

int32_t spawn_process(
    const char *name,
    uint8_t priority,
    uint32_t *pid
) {
    if (pid != NULL) {
        *pid = 0;
    }
    if (name == NULL) {
        return 1;
    }

    uint32_t name_len = 0;
    while (name[name_len] != '\0') {
        ++name_len;
        if (name_len > UINT8_MAX) {
            return 1;
        }
    }
    if (name_len == 0) {
        return 1;
    }

    struct EXT2DriverRequest request = {
        .buf = NULL,
        .name = (char *) name,
        .name_len = (uint8_t) name_len,
        .parent_inode = 1,
        .buffer_size = 0x100000,
        .is_directory = false,
    };
    struct ProcessCreateResult result = {0};
    syscall(
        SYSCALL_PROCESS_CREATE, (uint32_t) &request, priority,
        (uint32_t) &result
    );

    if (pid != NULL) {
        *pid = result.pid;
    }
    return result.status;
}

bool cmos_read_datetime(struct CmosDateTime *out) {
    bool ok = false;
    syscall(SYSCALL_CMOS_READ, (uint32_t) out, (uint32_t) &ok, 0);
    return ok;
}

uint32_t cmos_get_periodic_ticks(void) {
    uint32_t ticks = 0;
    syscall(SYSCALL_CMOS_TICKS, (uint32_t) &ticks, 0, 0);
    return ticks;
}

uint32_t pit_get_ticks(void) {
    uint32_t ticks = 0;
    syscall(SYSCALL_PIT_TICKS, (uint32_t) &ticks, 0, 0);
    return ticks;
}

uint32_t get_ticks_ms(void) {
    return pit_get_ticks() * (1000u / PIT_HZ);
}

void sleep_ms(uint32_t ms) {
    uint32_t start = get_ticks_ms();
    while (get_ticks_ms() - start < ms) {
        yield();
    }
}

bool raw_keyboard_enable(bool on) {
    int32_t ok = 0;
    syscall(SYSCALL_KEYBOARD_SET_RAW, on ? 1 : 0, (uint32_t) &ok, 0);
    return ok != 0;
}

bool map_region(void *start, uint32_t size) {
    int32_t ok = 0;
    syscall(
        SYSCALL_MEMORY_MAP_REGION, (uint32_t) start, size, (uint32_t) &ok
    );
    return ok != 0;
}

bool video_set_palette(const uint8_t *rgb) {
    int32_t ok = 0;
    syscall(SYSCALL_VIDEO_SET_PALETTE, (uint32_t) rgb, (uint32_t) &ok, 0);
    return ok != 0;
}

void video_blit(const uint8_t *buf, uint32_t width, uint32_t height) {
    syscall(SYSCALL_VIDEO_BLIT, (uint32_t) buf, width, height);
}

bool raw_keyboard_poll(struct KeyEvent *out) {
    int32_t ok = 0;
    syscall(SYSCALL_KEYBOARD_GET_EVENT, (uint32_t) out, (uint32_t) &ok, 0);
    return ok != 0;
}

__attribute__((noreturn)) void return_to_shell(void) {
    static const char shell_name[] = "shell";
    struct BlockBuffer directory;
    struct EXT2Inode root_node;
    get_node(&root_node, 1);
    read_blocks(directory.buf, root_node.i_block[0], 1);

    // exec needs the image size up front, so find "shell" in the root dir.
    uint32_t shell_inode = 0;
    uint32_t offset = get_dir_first_child_offset(directory.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory.buf, offset);
        if (entry->inode == 0 || entry->rec_len == 0 ||
            entry->rec_len > BLOCK_SIZE - offset) {
            break;
        }
        const char *name = get_entry_name(entry);
        if (entry->name_len == 5 && name[0] == 's' && name[1] == 'h' &&
            name[2] == 'e' && name[3] == 'l' && name[4] == 'l') {
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
        .name = (char *) shell_name,
        .name_len = 5,
        .parent_inode = 1,
        .buffer_size = shell_node.i_size,
        .is_directory = false,
    };

    framebuffer_clear();
    exec_process(exec_request);
    exit_process(); // exec failed - nothing to go back to
}
