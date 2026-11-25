#ifndef _OSUU_STDLIB_H
#define _OSUU_STDLIB_H
#include "stddef_osuu.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

int abs(int x);
long labs(long x);
int atoi(const char *s);
long atol(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
double atof(const char *s);

char *getenv(const char *name);
int system(const char *command);

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));

__attribute__((noreturn)) void exit(int status);
__attribute__((noreturn)) void abort(void);
int atexit(void (*fn)(void));
#endif
