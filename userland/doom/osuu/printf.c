// OSuu minimal C library for Doom: printf family and a small sscanf.
//
// Covers what doomgeneric's core actually uses: %d %i %u %x %X %o %c %s %p %%
// with flags (- 0 + space), width and precision (both may be *), and the
// h/hh/l/ll/z length modifiers. No %f: floats are patched out of the build.
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- vsnprintf */

struct Out {
    char *buf;
    size_t size;  // capacity including the terminating NUL (0 = count only)
    size_t len;   // characters produced so far (may exceed size)
};

static void out_char(struct Out *o, char c) {
    if (o->len + 1 < o->size)
        o->buf[o->len] = c;
    o->len++;
}

static void out_repeat(struct Out *o, char c, int n) {
    while (n-- > 0)
        out_char(o, c);
}

static void out_number(
    struct Out *o, uint64_t value, int negative, unsigned base, int upper,
    int width, int precision, int left, char pad, char sign_char
) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24];
    int n = 0;
    // Precision 0 with value 0 prints no digits at all (C standard).
    if (!(value == 0 && precision == 0)) {
        do {
            tmp[n++] = digits[value % base];
            value /= base;
        } while (value);
    }

    int zeros = precision > n ? precision - n : 0;
    char sign = negative ? '-' : sign_char;
    int body = n + zeros + (sign ? 1 : 0);
    // An explicit precision disables the 0 flag.
    if (precision >= 0)
        pad = ' ';

    if (!left && pad == ' ')
        out_repeat(o, ' ', width - body);
    if (sign)
        out_char(o, sign);
    if (!left && pad == '0')
        out_repeat(o, '0', width - body);
    out_repeat(o, '0', zeros);
    while (n)
        out_char(o, tmp[--n]);
    if (left)
        out_repeat(o, ' ', width - body);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct Out o = {.buf = buf, .size = size, .len = 0};

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out_char(&o, *fmt);
            continue;
        }
        fmt++;

        int left = 0;
        char pad = ' ', sign_char = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') pad = '0';
            else if (*fmt == '+') sign_char = '+';
            else if (*fmt == ' ') { if (!sign_char) sign_char = ' '; }
            else if (*fmt == '#') { /* alternate form: not needed */ }
            else break;
        }

        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            fmt++;
        } else {
            while (isdigit((unsigned char) *fmt))
                width = width * 10 + (*fmt++ - '0');
        }

        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            if (*fmt == '*') {
                precision = va_arg(ap, int);
                fmt++;
            } else {
                while (isdigit((unsigned char) *fmt))
                    precision = precision * 10 + (*fmt++ - '0');
            }
        }

        int longs = 0; // 0 = int, 1 = long, 2 = long long
        for (;; fmt++) {
            if (*fmt == 'l') longs++;
            else if (*fmt == 'h' || *fmt == 'z' || *fmt == 't' || *fmt == 'j') { }
            else break;
        }

        switch (*fmt) {
            case 'd':
            case 'i': {
                int64_t v = longs >= 2 ? va_arg(ap, long long)
                          : longs == 1 ? va_arg(ap, long)
                                       : va_arg(ap, int);
                uint64_t mag = v < 0 ? (uint64_t) (-(v + 1)) + 1 : (uint64_t) v;
                out_number(&o, mag, v < 0, 10, 0, width, precision, left, pad,
                           sign_char);
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'o': {
                uint64_t v = longs >= 2 ? va_arg(ap, unsigned long long)
                           : longs == 1 ? va_arg(ap, unsigned long)
                                        : va_arg(ap, unsigned int);
                unsigned base = *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16;
                out_number(&o, v, 0, base, *fmt == 'X', width, precision, left,
                           pad, 0);
                break;
            }
            case 'p': {
                uintptr_t v = (uintptr_t) va_arg(ap, void *);
                out_char(&o, '0');
                out_char(&o, 'x');
                out_number(&o, v, 0, 16, 0, width > 2 ? width - 2 : 0, -1, left,
                           '0', 0);
                break;
            }
            case 'c': {
                char c = (char) va_arg(ap, int);
                if (!left) out_repeat(&o, ' ', width - 1);
                out_char(&o, c);
                if (left) out_repeat(&o, ' ', width - 1);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                int n = 0;
                // Precision bounds how much is read: %.8s on an unterminated
                // 8-byte lump name must not run past it.
                while (s[n] && (precision < 0 || n < precision))
                    n++;
                if (!left) out_repeat(&o, ' ', width - n);
                for (int i = 0; i < n; i++)
                    out_char(&o, s[i]);
                if (left) out_repeat(&o, ' ', width - n);
                break;
            }
            case '%':
                out_char(&o, '%');
                break;
            case '\0':
                fmt--; // lone '%' at the end: stop at the terminator
                break;
            default:
                // Unknown conversion (e.g. %f): show it rather than guessing.
                out_char(&o, '%');
                out_char(&o, *fmt);
                break;
        }
    }

    if (o.size)
        o.buf[o.len < o.size ? o.len : o.size - 1] = '\0';
    return (int) o.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, (size_t) -1 / 2, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    char line[512];
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    size_t len = n < 0 ? 0 : (size_t) n;
    if (len >= sizeof(line))
        len = sizeof(line) - 1; // long lines are truncated, not dropped
    fwrite(line, 1, len, f);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int putchar(int c) {
    char ch = (char) c;
    fwrite(&ch, 1, 1, stdout);
    return (unsigned char) c;
}

int puts(const char *s) {
    fwrite(s, 1, strlen(s), stdout);
    putchar('\n');
    return 0;
}

/* ---------------------------------------------------------------- sscanf */

// Enough for Doom's number parsing (m_misc.c M_StrToInt, m_config.c):
// " 0x%x", " 0X%x", " 0%o", " %d", "%x", "%i". Whitespace in the format
// skips any input whitespace, other characters must match exactly, and
// %d %i %u %x %o store into an int*.
int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int assigned = 0;
    const char *s = str;

    for (; *fmt; fmt++) {
        if (isspace((unsigned char) *fmt)) {
            while (isspace((unsigned char) *s))
                s++;
            continue;
        }
        if (*fmt != '%') {
            if (*s != *fmt)
                break;
            s++;
            continue;
        }

        fmt++;
        int base;
        switch (*fmt) {
            case 'd': base = 10; break;
            case 'u': base = 10; break;
            case 'x':
            case 'X': base = 16; break;
            case 'o': base = 8; break;
            case 'i': base = 0; break; // prefix decides: 0x.. hex, 0.. octal
            default: goto done;        // unsupported conversion
        }

        while (isspace((unsigned char) *s))
            s++;
        int negative = 0;
        if (*s == '-' || *s == '+')
            negative = *s++ == '-';

        const char *start = s;
        long value = (long) strtoul(s, (char **) &s, base);
        if (s == start)
            break; // no digits: matching failure
        *va_arg(ap, int *) = (int) (negative ? -value : value);
        assigned++;
    }

done:
    va_end(ap);
    // EOF only if input ran out before anything could be converted.
    if (assigned == 0 && *str == '\0')
        return EOF;
    return assigned;
}

int fscanf(FILE *f, const char *fmt, ...) {
    // Only m_config.c reads config files with this, and fopen never returns
    // a config file on OSuu, so it can't be reached with a real stream.
    (void) f;
    (void) fmt;
    return EOF;
}
