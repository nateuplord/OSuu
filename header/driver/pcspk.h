#ifndef _DRIVER_PCSPEAKER_H
#define _DRIVER_PCSPEAKER_H

#include <stdint.h>

/**
 * Simple PC-speaker driver.
 *
 * Uses PIT channel 2 for the square wave and port 0x61 to gate the speaker.
 * Channel 0 (the scheduler tick) is left alone. Notes are nonblocking: the
 * PIT ISR turns the speaker off after the requested duration.
 */

/// Input clock fed to the Intel 8253/8254 PIT channels.
#define PC_SPEAKER_PIT_INPUT_HZ 1193182u

/// Initialize hardware state (speaker off, no pending note).
void pcspk_init(void);

/// Play a square wave at `frequency_hz` for approximately `duration_ms`.
void pcspk_tone(
    uint32_t frequency_hz,
    uint32_t duration_ms
);

/// Stop the speaker immediately and cancel any pending note.
void pcspk_stop(void);

/// Called from the PIT ISR to stop notes whose duration has elapsed.
void pcspk_tick(void);

#endif
