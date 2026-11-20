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
