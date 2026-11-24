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

void play_bad_apple(
    uint16_t offset_x,
    uint16_t offset_y
) {
    syscall(SYSCALL_PLAY_BADAPPLE, offset_x, offset_y, 0);
}

void get_keyboard_buffer(char *buf) {
    syscall(SYSCALL_KEYBOARD_GETCHAR, (uint32_t) buf, 0, 0);
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
