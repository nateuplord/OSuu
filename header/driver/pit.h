#ifndef _PIT_H
#define _PIT_H

#include <stdint.h>

/// PIT channel 0 data port (wired to IRQ0)
#define PIT_CHANNEL0_PORT 0x40

/// PIT mode/command register
#define PIT_COMMAND_PORT 0x43

/// PIT's fixed input oscillator frequency, in Hz
#define PIT_NATURAL_FREQUENCY_HZ 1193182u

/**
  * Command byte: channel 0, lobyte/hibyte access, mode 3 (square wave), binary.
  * @see https://wiki.osdev.org/Programmable_Interval_Timer#I.2FO_Ports
  */
#define PIT_COMMAND_SQUARE_WAVE 0x36

/* Driver Interfaces */

/**
  * Program the PIT to fire IRQ0 at the given frequency and unmask it at the PIC.
  *
  * @param frequency_hz
  * Desired tick rate. Must be at least ~19 Hz (PIT_NATURAL_FREQUENCY_HZ / 65536)
  * for the divisor to fit in 16 bits.
  */
void pit_init(uint32_t frequency_hz);

/// Tick rate the kernel programs the PIT to (kernel.c). 1000 must be
/// divisible by it, so ticks convert to milliseconds exactly.
#define PIT_TICK_RATE_HZ 100u

/**
  * Milliseconds since pit_init(), at PIT_TICK_RATE_HZ resolution (10 ms).
  * Backing implementation for SYSCALL_TIME_TICKS_MS.
  */
uint32_t pit_get_ms(void);

/**
  * Number of PIT ticks since pit_init() was called.
  */
uint32_t pit_get_ticks(void);

/* PIT Interrupt Service Routine */

/**
  * Handles the PIT interrupt. Just advances the tick counter 
  * deliberately does no other work here (see keyboard_isr for the same split 
  * between "ISR does the minimum" and "consumers read state outside of it").
  */
void pit_isr(void);

#endif
