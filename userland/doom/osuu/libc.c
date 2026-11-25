// OSuu minimal C library for Doom: strings, ctype, number parsing, malloc
// and the odds and ends doomgeneric's core links against. Formatted I/O is in
// printf.c, FILE streams in file.c. Built -mgeneral-regs-only like the rest
// of Doom, so nothing here may do real floating-point maths.
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kernel_api.h"

int errno = 0;

/* ---------------------------------------------------------------- string */

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    // Word copies when both sides are aligned: Doom memcpys whole screen rows.
    if ((((uintptr_t) d | (uintptr_t) s) & 3) == 0) {
        while (n >= 4) {
            *(uint32_t *) d = *(const uint32_t *) s;
            d += 4;
            s += 4;
            n -= 4;
        }
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s || d >= s + n)
        return memcpy(dst, src, n);
    while (n--)
        d[n] = s[n]; // overlapping, dst after src: copy backwards
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    while (n--)
        *d++ = (uint8_t) c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return x[i] - y[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0'; // strncpy pads with NULs (and doesn't terminate if full)
    return dst;
}

char *strcat(char *dst, const char *src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst + strlen(dst);
    while (n-- && *src)
        *d++ = *src++;
    *d = '\0';
    return dst;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char) *a - (unsigned char) *b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b || !*a)
            return (unsigned char) *a - (unsigned char) *b;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char) c)
            return (char *) s;
        if (!*s)
            return NULL;
    }
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char) c)
            last = s;
        if (!*s)
            return (char *) last;
    }
}

char *strstr(const char *haystack, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0)
        return (char *) haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle && strncmp(haystack, needle, n) == 0)
            return (char *) haystack;
    }
    return NULL;
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy)
        memcpy(copy, s, n);
    return copy;
}

char *strerror(int errnum) {
    switch (errnum) {
        case ENOENT: return "No such file or directory";
        case ENOMEM: return "Out of memory";
        case ENOSYS: return "Not supported on OSuu";
        default: return "Error";
    }
}

int strcasecmp(const char *a, const char *b) {
    while (*a && tolower(*a) == tolower(*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char) *a) - tolower((unsigned char) *b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        int x = tolower((unsigned char) *a), y = tolower((unsigned char) *b);
        if (x != y || !x)
            return x - y;
    }
    return 0;
}

/* ----------------------------------------------------------------- ctype */

int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
           c == '\f';
}
int isprint(int c) { return c >= 0x20 && c < 0x7F; }
int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }
int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }

/* ------------------------------------------------------- number parsing */

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

unsigned long strtoul(const char *s, char **end, int base) {
    while (isspace((unsigned char) *s))
        s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0 && s[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }

    unsigned long value = 0;
    for (;; s++) {
        int digit;
        if (isdigit((unsigned char) *s))
            digit = *s - '0';
        else if (isalpha((unsigned char) *s))
            digit = tolower((unsigned char) *s) - 'a' + 10;
        else
            break;
        if (digit >= base)
            break;
        value = value * (unsigned long) base + (unsigned long) digit;
    }
    if (end)
        *end = (char *) s;
    return value;
}

long strtol(const char *s, char **end, int base) {
    while (isspace((unsigned char) *s))
        s++;
    int negative = 0;
    if (*s == '-' || *s == '+')
        negative = *s++ == '-';
    unsigned long magnitude = strtoul(s, end, base);
    return negative ? -(long) magnitude : (long) magnitude;
}

int atoi(const char *s) { return (int) strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

// Only reachable from m_config.c's float settings, which load from a config
// file OSuu never has. Returning a constant keeps this soft-float-free.
double atof(const char *s) {
    (void) s;
    return 0.0;
}

// Clears the sign bit with integer ops only (no FPU, no soft-float helper).
double fabs(double x) {
    union {
        double d;
        uint64_t u;
    } v = {.d = x};
    v.u &= ~(1ULL << 63);
    return v.d;
}

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *)) {
    // Insertion sort: Doom only sorts a handful of things, and it's tiny.
    uint8_t *a = base;
    uint8_t tmp[256];
    if (size > sizeof(tmp))
        return;
    for (size_t i = 1; i < count; i++) {
        memcpy(tmp, a + i * size, size);
        size_t j = i;
        while (j > 0 && compare(a + (j - 1) * size, tmp) > 0) {
            memcpy(a + j * size, a + (j - 1) * size, size);
            j--;
        }
        memcpy(a + j * size, tmp, size);
    }
}

/* ---------------------------------------------------------------- malloc */

// A bump allocator over a fixed 32 MiB region, made demand-pageable with
// map_region() on first use - pages only cost RAM once touched. Doom's own
// zone allocator (z_zone.c) does the real bookkeeping inside one big early
// malloc (I_ZoneBase, 6 MiB); everything else that comes through here is a
// few long-lived buffers, so free() only reclaims the most recent block.
#define HEAP_BASE 0x30000000u
#define HEAP_SIZE (32u * 1024u * 1024u)
#define HEAP_ALIGN 16u

struct BlockHeader {
    uint32_t size; // usable bytes, rounded up to HEAP_ALIGN
    uint32_t magic;
    uint32_t _pad[2]; // keeps the payload HEAP_ALIGN-aligned
};
#define BLOCK_MAGIC 0x0A110CA7u

static uint8_t *heap_top = NULL; // NULL until the region is mapped
static uint8_t *last_block = NULL;

static int heap_init(void) {
    if (heap_top)
        return 1;
    if (!map_region((void *) HEAP_BASE, HEAP_SIZE))
        return 0;
    heap_top = (uint8_t *) HEAP_BASE;
    return 1;
}

static struct BlockHeader *header_of(void *ptr) {
    return (struct BlockHeader *) ptr - 1;
}

void *malloc(size_t size) {
    if (!heap_init()) {
        errno = ENOMEM;
        return NULL;
    }
    uint32_t rounded = (uint32_t) ((size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1));
    uint32_t needed = rounded + (uint32_t) sizeof(struct BlockHeader);
    if (size > HEAP_SIZE ||
        (uint32_t) (heap_top - (uint8_t *) HEAP_BASE) + needed > HEAP_SIZE) {
        errno = ENOMEM;
        return NULL;
    }

    struct BlockHeader *h = (struct BlockHeader *) heap_top;
    h->size = rounded;
    h->magic = BLOCK_MAGIC;
    last_block = heap_top;
    heap_top += needed;
    return h + 1;
}

void free(void *ptr) {
    if (!ptr)
        return;
    struct BlockHeader *h = header_of(ptr);
    if (h->magic != BLOCK_MAGIC)
        return;
    // Only the newest block can be given back without a free list.
    if ((uint8_t *) h == last_block) {
        heap_top = last_block;
        last_block = NULL;
    }
    h->magic = 0;
}

void *calloc(size_t count, size_t size) {
    if (size && count > HEAP_SIZE / size)
        return NULL;
    void *p = malloc(count * size);
    if (p)
        memset(p, 0, count * size); // a freed-and-reused tail may be dirty
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    struct BlockHeader *h = header_of(ptr);
    if (size <= h->size)
        return ptr;

    // Newest block: grow in place.
    uint32_t rounded = (uint32_t) ((size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1));
    if ((uint8_t *) h == last_block &&
        (uint32_t) ((uint8_t *) ptr - (uint8_t *) HEAP_BASE) + rounded <= HEAP_SIZE) {
        heap_top = (uint8_t *) ptr + rounded;
        h->size = rounded;
        return ptr;
    }

    void *grown = malloc(size);
    if (grown)
        memcpy(grown, ptr, h->size);
    return grown;
}

/* --------------------------------------------------- environment / misc */

char *getenv(const char *name) {
    (void) name;
    return NULL; // no environment on OSuu
}

int system(const char *command) {
    (void) command;
    errno = ENOSYS;
    return -1;
}

int atexit(void (*fn)(void)) {
    (void) fn; // Doom uses its own I_AtExit list instead
    return 0;
}

int isatty(int fd) {
    (void) fd;
    return 0;
}

int mkdir(const char *path, mode_t mode) {
    (void) path;
    (void) mode;
    errno = ENOSYS; // no writes from Doom: no saves, no config
    return -1;
}

int remove(const char *path) {
    (void) path;
    errno = ENOSYS;
    return -1;
}

int rename(const char *from, const char *to) {
    (void) from;
    (void) to;
    errno = ENOSYS;
    return -1;
}

// exit()/abort() live in doomgeneric_osuu.c: leaving has to hand the screen
// and keyboard back and return to the shell, which is platform business.
void osuu_assert_fail(const char *expr, const char *file, int line) {
    fprintf(stderr, "assert failed: %s (%s:%d)\n", expr, file, line);
    abort();
}
