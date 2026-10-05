#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define SHELL_BUFFER_SIZE 1024
#define SHELL_MAX_ARGS 64
#define SHELL_DELIMITERS " \t\r\n\a"

char *shell_read_line(void);

char **shell_split_line(char *line);

int shell_execute(char **args);

int shell_launch(char **args);

int shell_cd(char **args);
int shell_ls(char **args);
int shell_mkdir(char **args);
int shell_cat(char **args);
int shell_cp(char **args);
int shell_mv(char **args);
int shell_find(char **args);
int shell_exit(char **args);

#endif