#include "shell/shell.h"

const char *builtin_cmd[] = {
    "cd",
    "ls",
    "mkdir",
    "cat",
    "cp",
    "mv",
    "find",
    "exit"
};

int (*builtin_func[]) (char **) = {
    &shell_cd,
    &shell_ls,
    &shell_mkdir,
    &shell_cat,
    &shell_cp,
    &shell_mv,
    &shell_find,
    &shell_exit
};

// return 1 = keep running, 0 = exit shell
int shell_cd(char **args)    { (void)args; return 1; }
int shell_ls(char **args)    { (void)args; return 1; }
int shell_mkdir(char **args) { (void)args; return 1; }
int shell_cat(char **args)   { (void)args; return 1; }
int shell_cp(char **args)    { (void)args; return 1; }
int shell_mv(char **args)    { (void)args; return 1; }
int shell_find(char **args)  { (void)args; return 1; }
int shell_exit(char **args)  { (void)args; return 0; }