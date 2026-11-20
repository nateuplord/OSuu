#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>
#include <stdint.h>
#include <linux/limits.h>
#include "stdlib/string.h"

#define SHELL_BUFFER_SIZE 1024
#define SHELL_MAX_ARGS 64

// Command line
typedef struct {
    char buffer[SHELL_BUFFER_SIZE];
    uint16_t length;
} Line;

// Command history
typedef struct {
    Line buffer[SHELL_BUFFER_SIZE];
    uint16_t length;
} History;

// Helper Functions
int streq(const char str1[], const char str2[]);
int str_len(const char *str);
void strcopy(const char src[], char dest[]);
void strcopy_len(const char src[], char dest[], int len);
// Splits the string by the keychar
int str_split(const char *string, char **parts, char keychar, int max_parts);
// Outputs every layer of the path in layers and outputs the depth
int resolve_path_string(const char *path, char layers[][MAX_INPUT]);
// Splits the file's name from its directory
int dir_file_split(const char *path, char *dir, char *name);
// Finds the inode of the path
int8_t resolve_path(uint32_t start_inode, const char *path, uint32_t *resolved_inode);

// Shell Technical Functions
void start_shell(void);
void shell_get_cursor_position(uint8_t *row, uint8_t *col);
void shell_cursor_left(uint8_t *row, uint8_t *col);
void shell_cursor_right(uint8_t *row, uint8_t *col);
void shell_history_up(uint8_t *row, uint8_t *col);
void shell_history_down(uint8_t *row, uint8_t *col);
// Adds a character to the line
void add_char(char c);
// Removes a character from the line
void remove_char(void);
// Executes the function
int shell_execute(char **args);
// Splits the line into arguments
int shell_split_line(char *line, char **args);
// Calls shell_execute then adds a new empty line
int enter_command(void);

// Built-in Commands
int shell_cd(char **args);
int shell_ls(char **args);
int shell_mkdir(char **args);
int shell_cat(char **args);
int shell_cp(char **args);
int shell_mv(char **args);
int shell_find(char **args);
int shell_echo(char **args);
int shell_pwd(char **args);
int shell_exit(char **args);

#endif
