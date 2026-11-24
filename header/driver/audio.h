#ifndef _DRIVER_AUDIO_H
#define _DRIVER_AUDIO_H

#include <stdint.h>

/// Mixer capacity. Extra notes replace the voice with the least time left.
#define AUDIO_MAX_VOICES 8u

/// SB16 playback format: 8-bit unsigned mono.
#define AUDIO_SAMPLE_RATE_HZ 22050u

/// Samples per DMA block; two blocks are kept in flight.
#define AUDIO_BLOCK_SAMPLES 2048u

/**
 * Initialize the polyphonic mixer. Uses the SB16 when present and falls back
 * to the monophonic PC speaker otherwise.
 */
void audio_init(void);

/** Add a note to the mix. Existing voices keep sounding. */
void audio_tone(
    uint32_t frequency_hz,
    uint32_t duration_ms
);

/** Silence every voice immediately. */
void audio_stop(void);

/** Render one DMA block of unsigned 8-bit PCM samples. */
void audio_render_block(
    uint8_t *buffer,
    uint32_t sample_count
);

#endif
