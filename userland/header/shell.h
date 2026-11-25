#ifndef _USER_SHELL_H
#define _USER_SHELL_H

#include "string.h"
#include <stddef.h>
#include <stdint.h>

/**
 * Ported from header/shell/shell.h. Sizes are much smaller than the kernel
 * version's: that one runs on a 2 MiB kernel stack with an effectively
 * unbounded .bss, while this one gets one demand-paged 4 KiB stack page to
 * start and must keep its own .bss small enough that the flat binary (which
 * physically contains .bss's zero bytes once ld -oformat=binary is done)
 * stays well under the 1 MiB program size limit.
 */
#define SHELL_BUFFER_SIZE 256
#define SHELL_MAX_ARGS 32

// Replaces PATH_MAX/MAX_INPUT from <linux/limits.h> (4096/255 - a single
// resolve_path_string() call would stack-allocate ~1 MiB with those).
#define SHELL_PATH_DEPTH_MAX 32
#define SHELL_PATH_SEGMENT_MAX 64

// Replaces DISK_SPACE (4 MiB) as the cap for shell_file_buffer specifically -
// DISK_SPACE itself (from fs_types.h) stays the real 4 MiB disk size.
#define USER_FILE_BUFFER_SIZE 65536u

typedef struct {
    char buffer[SHELL_BUFFER_SIZE];
    uint16_t length;
} Line;

typedef struct {
    Line buffer[SHELL_BUFFER_SIZE];
    uint16_t length;
} History;

int streq(
    const char str1[],
    const char str2[]
);
int str_len(const char *str);
void strcopy(
    const char src[],
    char dest[]
);
void strcopy_len(
    const char src[],
    char dest[],
    int len
);
int str_split(
    const char *string,
    char **parts,
    char keychar,
    int max_parts
);
int resolve_path_string(
    const char *path,
    char layers[][SHELL_PATH_SEGMENT_MAX]
);
int dir_file_split(
    const char *path,
    char *dir,
    char *name
);
int8_t resolve_path(
    uint32_t start_inode,
    const char *path,
    uint32_t *resolved_inode
);

void start_shell(void);
void shell_get_cursor_position(
    uint8_t *row,
    uint8_t *col
);
void shell_cursor_left(
    uint8_t *row,
    uint8_t *col
);
void shell_cursor_right(
    uint8_t *row,
    uint8_t *col
);
void shell_history_up(
    uint8_t *row,
    uint8_t *col
);
void shell_history_down(
    uint8_t *row,
    uint8_t *col
);
void add_char(char c);
void remove_char(void);
int shell_execute(char **args);
int shell_split_line(
    char *line,
    char **args
);
int enter_command(void);

int shell_sudo(char **args);
int shell_cd(char **args);
int shell_ls(char **args);
int shell_mkdir(char **args);
int shell_cat(char **args);
int shell_grep(char **args);
int shell_cp(char **args);
int shell_mv(char **args);
int shell_rm(char **args);
int shell_find(char **args);
int shell_echo(char **args);
int shell_pwd(char **args);
int shell_exit(char **args);
int shell_badapple(char **args);
int shell_exec(char **args);
int shell_ps(char **args);
int shell_kill(char **args);
int shell_clock(char **args);
int shell_beep(char **args);
int shell_clear(char **args);
int shell_help(char **args);
int shell_pacman(char **args);

#endif
