#ifndef _OSUU_STDIO_H
#define _OSUU_STDIO_H
#include "stddef_osuu.h"
#include <stdarg.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define FILENAME_MAX 256

/// Opaque. Read-only files are whole-file images loaded into memory; the
/// console streams (stdout/stderr) print to the screen.
typedef struct OsuuFile FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *buf, size_t size, size_t count, FILE *f);
size_t fwrite(const void *buf, size_t size, size_t count, FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
int fflush(FILE *f);
int fgetc(FILE *f);
char *fgets(char *buf, int size, FILE *f);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int fileno(FILE *f);

int putchar(int c);
int puts(const char *s);

int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int sscanf(const char *str, const char *fmt, ...);
int fscanf(FILE *f, const char *fmt, ...);

int remove(const char *path);
int rename(const char *from, const char *to);
#endif
