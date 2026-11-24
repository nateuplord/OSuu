#ifndef _DRIVER_SB16_H
#define _DRIVER_SB16_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Probe and initialize the emulated SB16 at port 0x220, IRQ 5, DMA 1.
 * `buffer_a` and `buffer_b` must be physically contiguous page-aligned blocks.
 */
bool sb16_init(
    uint8_t *buffer_a,
    uint8_t *buffer_b,
    uint16_t sample_count
);

/** Start (or restart) playback from buffer A. */
void sb16_start(void);

/** Mask DMA and turn the DAC speaker output off. */
void sb16_stop(void);

/** End-of-DMA interrupt: render and start the other buffer. */
void sb16_isr(void);

#endif
