#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>
#include <stdint.h>   // both are fine in freestanding mode
#include "stdlib/string.h"

#define SHELL_BUFFER_SIZE 1024
#define SHELL_MAX_ARGS    64
#define SHELL_DELIMITERS  " \t\r\n"

// Reads into a caller-provided buffer (no malloc)
void shell_read_line(char *buf, size_t size);

// Splits in place, returns argc, fills argv
int  shell_split_line(char *line, char **argv, int max_args);

int  shell_execute(char **args);

int  shell_cd(char **args);
int  shell_ls(char **args);
int  shell_mkdir(char **args);
int  shell_cat(char **args);
int  shell_cp(char **args);
int  shell_mv(char **args);
int  shell_find(char **args);
int  shell_exit(char **args);

#endif