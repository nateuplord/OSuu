#include "driver/pit.h"
#include "cpu/interrupt.h"
#include "cpu/portio.h"
#include "process/scheduler.h"
#include <stdint.h>

static volatile uint32_t pit_ticks = 0;

void pit_init(uint32_t frequency_hz) {
    uint32_t divisor = PIT_NATURAL_FREQUENCY_HZ / frequency_hz;

    out(PIT_COMMAND_PORT, PIT_COMMAND_SQUARE_WAVE);
    out(PIT_CHANNEL0_PORT, (uint8_t) (divisor & 0xFF));
    out(PIT_CHANNEL0_PORT, (uint8_t) ((divisor >> 8) & 0xFF));

    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
}

uint32_t pit_get_ticks(void) {
    return pit_ticks;
}

void pit_isr(void) {
    pic_ack(IRQ_TIMER);
    ++pit_ticks;
    scheduler_tick();
}
