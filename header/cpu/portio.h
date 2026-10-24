#ifndef _PORTIO_H
#define _PORTIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Sends a given data to the given I/O port
 *
 * @param port
 * The I/O port to send the data to
 *
 * @param data
 * The data to send to the I/O port
 */
void out(
    uint16_t port,
    uint8_t data
);

/**
 * Reads data from the given I/O port
 *
 * @param port
 * The I/O port to request the data
 *
 * @return
 * Recieved data from the corresponding I/O port
 */
uint8_t in(uint16_t port);

/**
 * Write a 16-bit value to an I/O port.
 *
 * @details
 * Wraps the x86 `out` instruction (`outw`). Uses the I/O address space,
 * which is separate from regular memory.
 *
 * @param port
 * I/O port number (`0x0000`-`0xFFFF`)
 *
 * @param data
 * 16-bit value to write
 */
void out16(
    uint16_t port,
    uint16_t data
);

/**
 * Read a 16-bit value from an I/O port.
 * @details Wraps the x86 `in` instruction (`inw`).
 *
 * @param port
 * I/O port number (`0x0000`-`0xFFFF`)
 *
 * @return
 * The 16-bit value read from the port
 */
uint16_t in16(uint16_t port);

#endif
