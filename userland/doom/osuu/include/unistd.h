#ifndef _OSUU_UNISTD_H
#define _OSUU_UNISTD_H
// Deliberately no read()/write()/close(): userland's kernel_api.o already
// defines read/write/delete as OSuu's *ext2* syscalls with different
// signatures, and Doom never calls the POSIX ones.
int isatty(int fd);
#endif
