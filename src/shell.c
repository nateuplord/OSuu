#include "header/shell/shell.h"

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