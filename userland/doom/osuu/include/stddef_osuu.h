#ifndef _OSUU_LIBC_COMMON_H
#define _OSUU_LIBC_COMMON_H
// Shared bits of OSuu's minimal C library for Doom. Only gcc's freestanding
// headers (stddef/stdint/stdarg/stdbool/limits) are available - the build
// uses -nostdinc so no host (glibc) header can leak into the binary.
#include <stddef.h>
#include <stdint.h>
#endif
