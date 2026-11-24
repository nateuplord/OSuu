#ifndef _DRIVER_AUDIO_H
#define _DRIVER_AUDIO_H

#include <stdint.h>

/// Mixer capacity. Extra notes replace the voice with the least time left.
#define AUDIO_MAX_VOICES 8u

/// SB16 playback format: 8-bit unsigned mono.
#define AUDIO_SAMPLE_RATE_HZ 22050u

/// Samples per DMA block; two blocks are kept in flight. Smaller blocks reduce
/// note-start latency, but 128 samples underruns QEMU's SB16; 256 is stable.
#define AUDIO_BLOCK_SAMPLES 256u

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

/**
 * Fade out one pitch (or every pitch when frequency_hz is 0) without stopping
 * DMA playback. Used by the score format's silence records.
 */
void audio_release(uint32_t frequency_hz);

/** Silence every voice immediately and stop playback. */
void audio_stop(void);

/** Render one DMA block of unsigned 8-bit PCM samples. */
void audio_render_block(
    uint8_t *buffer,
    uint32_t sample_count
);

#endif
