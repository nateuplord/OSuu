#ifndef _STRING_H
#define _STRING_H

#include <stdint.h>
#include <stddef.h>

/**
 * C standard `memset`
 * 
 * @note 
 * Run `man memset` or
 * visit the [Linux manual](https://man7.org/linux/man-pages/man3/memset.3.html) webpage for more details
 * 
 * @param s
 * Pointer to memory area to set
 * @param c
 * Constant byte value for filling memory area
 * @param n
 * Memory area size in byte 
 * 
 * @return
 * Pointer to `s`
*/
void* memset(void *s, int c, size_t n);

/**
 * C standard `memcpy`
 * 
 * @note 
 * Run `man memcpy` or
 * visit the [Linux manual](https://man7.org/linux/man-pages/man3/memcpy.3.html) webpage for more details
 * 
 * @param dest
 * Starting location for memory area to set
 * @param src 
 * Pointer to source memory
 * @param n
 * Memory area size in byte 
 * 
 * @return
 * Pointer to `dest`
*/
void* memcpy(
    void* restrict dest, 
    const void* restrict src, 
    size_t n
);

/**
 * C standard memcmp
 *
 * @details
 * Run `man memcmp` or
 * visit the [Linux manual](https://man7.org/linux/man-pages/man3/memcmp.3.html) webpage for more details
 * 
 * @param s1
 * Pointer to first memory area
 * @param s2
 * Pointer to second memory area
 * @param n
 * Memory area size in byte 
 * 
 * @return 
 * Integer as error code, zero for equality, non-zero for inequality
*/
int memcmp(
    const void *s1, 
    const void *s2, 
    size_t n
);

/**
 * C standard `memmove`
 * 
 * @note
 * Run `man memmove` or
 * visit the [Linux manual](https://man7.org/linux/man-pages/man3/memmove.3.html) webpage for more details
 * 
 * @param dest
 * Pointer to destination memory
 * @param src
 * Pointer to source memory
 * @param n
 * Memory area size in byte 
 * 
 * @return
 * Pointer to `dest`
*/
void *memmove(
    void *dest, 
    const void *src, 
    size_t n
);

#endif
