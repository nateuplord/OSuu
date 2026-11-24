#include "driver/pcspk.h"
#include "cpu/interrupt.h"
#include "cpu/portio.h"
#include "driver/pit.h"
#include <stdint.h>

#define PC_SPEAKER_PIT_COMMAND_PORT 0x43u
#define PC_SPEAKER_PIT_DATA_PORT 0x42u
#define PC_SPEAKER_CONTROL_PORT 0x61u

/// Bits 0 and 1 gate PIT channel 2 and connect it to the speaker.
#define PC_SPEAKER_CONTROL_MASK 0x03u

/// channel 2, lobyte/hibyte, mode 3 (square wave), binary counter
#define PC_SPEAKER_PIT_MODE 0xB6u

#define MILLISECONDS_PER_SECOND 1000u
#define PIT_TICK_MS (MILLISECONDS_PER_SECOND / PIT_HZ)

/// PIT tick at which the current note should stop, or 0 when idle.
static volatile uint32_t pcspk_stop_tick = 0;

void pcspk_stop(void) {
    uint8_t control = in(PC_SPEAKER_CONTROL_PORT);
    out(PC_SPEAKER_CONTROL_PORT,
        (uint8_t) (control & ~PC_SPEAKER_CONTROL_MASK));
    io_wait();
    pcspk_stop_tick = 0;
}

void pcspk_init(void) {
    pcspk_stop_tick = 0;
    pcspk_stop();
}

void pcspk_tone(
    uint32_t frequency_hz,
    uint32_t duration_ms
) {
    if (frequency_hz == 0 || duration_ms == 0) {
        pcspk_stop();
        return;
    }

    uint32_t divisor = PC_SPEAKER_PIT_INPUT_HZ / frequency_hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFFu) {
        divisor = 0xFFFFu;
    }

    // Reprogram only channel 2; channel 0 remains the scheduler tick.
    out(PC_SPEAKER_PIT_COMMAND_PORT, PC_SPEAKER_PIT_MODE);
    io_wait();
    out(PC_SPEAKER_PIT_DATA_PORT, (uint8_t) (divisor & 0xFFu));
    io_wait();
    out(PC_SPEAKER_PIT_DATA_PORT, (uint8_t) (divisor >> 8));
    io_wait();

    uint8_t control = in(PC_SPEAKER_CONTROL_PORT);
    out(PC_SPEAKER_CONTROL_PORT, (uint8_t) (control | PC_SPEAKER_CONTROL_MASK));
    io_wait();

    uint32_t ticks = (duration_ms + PIT_TICK_MS - 1u) / PIT_TICK_MS;
    if (ticks == 0) {
        ticks = 1;
    }
    pcspk_stop_tick = pit_get_ticks() + ticks;
}

void pcspk_tick(void) {
    if (pcspk_stop_tick == 0) {
        return;
    }

    // Signed subtraction keeps this correct across the uint32 tick wrap.
    if ((int32_t) (pcspk_stop_tick - pit_get_ticks()) <= 0) {
        pcspk_stop();
    }
}
