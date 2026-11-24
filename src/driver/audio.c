#include "driver/audio.h"
#include "driver/pcspk.h"
#include "driver/sb16.h"
#include "stdlib/string.h"
#include <stdbool.h>
#include <stdint.h>

#define AUDIO_SILENCE_BYTE 0x80u
#define AUDIO_VOICE_AMPLITUDE 48u
#define AUDIO_WAVE_TABLE_SIZE 256u
#define AUDIO_WAVE_TABLE_MASK 0xFFu
#define AUDIO_WAVE_TABLE_SHIFT 24u
#define AUDIO_WAVE_FRACTION_SHIFT 16u
#define AUDIO_WAVE_FRACTION_MASK 0xFFu
#define AUDIO_PHASE_SCALE 65536u
#define AUDIO_ATTACK_SAMPLES ((AUDIO_SAMPLE_RATE_HZ * 2u) / 1000u)
#define AUDIO_RELEASE_SAMPLES ((AUDIO_SAMPLE_RATE_HZ * 10u) / 1000u)
#define AUDIO_MAX_DURATION_MS 60000u

struct AudioVoice {
    uint32_t phase;
    uint32_t phase_step;
    uint32_t remaining_samples;
    uint32_t age_samples;
    uint32_t frequency_hz;
    bool active;
};

// One cycle of a softened 50% duty-cycle square wave, scaled to int8_t range.
// The slopes are a tanh-shaped transition over roughly 12 table steps, which
// keeps the odd harmonics but removes the piercing high-frequency edge energy.
static const int8_t audio_wave_table[AUDIO_WAVE_TABLE_SIZE] = {
    0,    25,   47,   67,   83,   96,   105,  112,  116,  120,  122,  123,
    125,  125,  126,  126,  126,  127,  127,  127,  127,  127,  127,  127,
    127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,
    127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,
    127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,
    127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,
    127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,
    127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,
    127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,  127,
    127,  127,  127,  127,  126,  126,  126,  125,  125,  123,  122,  120,
    116,  112,  105,  96,   83,   67,   47,   25,   0,    -25,  -47,  -67,
    -83,  -96,  -105, -112, -116, -120, -122, -123, -125, -125, -126, -126,
    -126, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127,
    -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127,
    -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127,
    -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127,
    -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127,
    -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127,
    -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127,
    -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127, -127,
    -126, -126, -126, -125, -125, -123, -122, -120, -116, -112, -105, -96,
    -83,  -67,  -47,  -25,
};

// Two page-aligned blocks: DMA channel 1 cannot cross a 64 KiB boundary.
static uint8_t audio_dma_buffers[2][AUDIO_BLOCK_SAMPLES]
    __attribute__((aligned(4096)));
static struct AudioVoice audio_voices[AUDIO_MAX_VOICES];
static uint32_t audio_fallback_frequency = 0;
static bool audio_sb16_available = false;
static bool audio_sb16_playing = false;

static uint32_t audio_read_eflags(void) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0"
                     : "=r"(flags));
    return flags;
}

static void audio_write_eflags(uint32_t flags) {
    __asm__ volatile("pushl %0; popfl"
                     :
                     : "r"(flags)
                     : "memory");
}

void audio_init(void) {
    pcspk_init();
    memset(audio_voices, 0, sizeof(audio_voices));
    memset(audio_dma_buffers, AUDIO_SILENCE_BYTE, sizeof(audio_dma_buffers));
    audio_fallback_frequency = 0;

    if (!sb16_init(
            audio_dma_buffers[0], audio_dma_buffers[1], AUDIO_BLOCK_SAMPLES
        )) {
        return;
    }

    audio_sb16_available = true;
    audio_render_block(audio_dma_buffers[0], AUDIO_BLOCK_SAMPLES);
    sb16_start();
    audio_sb16_playing = true;
}

void audio_render_block(
    uint8_t *buffer,
    uint32_t sample_count
) {
    for (uint32_t sample = 0; sample < sample_count; sample++) {
        int32_t mix = 0;
        uint32_t amplitude_sum = 0;

        for (uint32_t i = 0; i < AUDIO_MAX_VOICES; i++) {
            struct AudioVoice *voice = &audio_voices[i];
            if (!voice->active) {
                continue;
            }

            uint32_t amplitude = AUDIO_VOICE_AMPLITUDE;
            if (voice->age_samples < AUDIO_ATTACK_SAMPLES) {
                amplitude =
                    (amplitude * voice->age_samples) / AUDIO_ATTACK_SAMPLES;
            }
            if (voice->remaining_samples < AUDIO_RELEASE_SAMPLES) {
                amplitude = (amplitude * voice->remaining_samples) /
                            AUDIO_RELEASE_SAMPLES;
            }

            if (amplitude != 0) {
                uint32_t table_index = voice->phase >> AUDIO_WAVE_TABLE_SHIFT;
                uint32_t fraction =
                    (voice->phase >> AUDIO_WAVE_FRACTION_SHIFT) &
                    AUDIO_WAVE_FRACTION_MASK;
                int32_t current = audio_wave_table[table_index];
                int32_t next =
                    audio_wave_table[(table_index + 1) & AUDIO_WAVE_TABLE_MASK];
                // Linear interpolation removes the phase-quantization steps of
                // a plain 256-entry table lookup.
                int32_t wave =
                    current + ((next - current) * (int32_t) fraction) / 256;
                mix += (wave * (int32_t) amplitude) / 127;
                amplitude_sum += amplitude;
            }

            voice->phase += voice->phase_step;
            voice->age_samples++;
            voice->remaining_samples--;
            if (voice->remaining_samples == 0) {
                voice->active = false;
            }
        }

        // Normalize by the smoothed amplitude sum, not the voice count. A
        // starting or ending voice then cannot make every other voice jump.
        int32_t centered = amplitude_sum == 0
                             ? 0
                             : (mix * (int32_t) AUDIO_VOICE_AMPLITUDE) /
                                   (int32_t) amplitude_sum;
        if (centered < -128) {
            centered = -128;
        }
        if (centered > 127) {
            centered = 127;
        }
        buffer[sample] = (uint8_t) (centered + 128);
    }
}

void audio_tone(
    uint32_t frequency_hz,
    uint32_t duration_ms
) {
    if (frequency_hz == 0 || duration_ms == 0) {
        audio_stop();
        return;
    }

    uint32_t flags = audio_read_eflags();
    __asm__ volatile("cli");

    if (!audio_sb16_available) {
        pcspk_tone(frequency_hz, duration_ms);
        audio_fallback_frequency = frequency_hz;
        audio_write_eflags(flags);
        return;
    }

    uint32_t slot = AUDIO_MAX_VOICES;
    for (uint32_t i = 0; i < AUDIO_MAX_VOICES; i++) {
        if (!audio_voices[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == AUDIO_MAX_VOICES) {
        slot = 0;
        for (uint32_t i = 1; i < AUDIO_MAX_VOICES; i++) {
            if (audio_voices[i].remaining_samples <
                audio_voices[slot].remaining_samples) {
                slot = i;
            }
        }
    }

    if (duration_ms > AUDIO_MAX_DURATION_MS) {
        duration_ms = AUDIO_MAX_DURATION_MS;
    }
    if (frequency_hz > AUDIO_SAMPLE_RATE_HZ / 2u) {
        frequency_hz = AUDIO_SAMPLE_RATE_HZ / 2u;
    }

    struct AudioVoice *voice = &audio_voices[slot];
    voice->phase = 0;
    // Equivalent to frequency_hz * 2^32 / sample_rate without 64-bit division:
    // the intermediate quotient fits in 32 bits for all supported pitches.
    voice->phase_step =
        ((frequency_hz * AUDIO_PHASE_SCALE) / AUDIO_SAMPLE_RATE_HZ) << 16;
    voice->remaining_samples = (duration_ms * AUDIO_SAMPLE_RATE_HZ) / 1000u;
    if (voice->remaining_samples == 0) {
        voice->remaining_samples = 1;
    }
    voice->age_samples = 0;
    voice->frequency_hz = frequency_hz;
    voice->active = true;

    if (!audio_sb16_playing) {
        audio_render_block(audio_dma_buffers[0], AUDIO_BLOCK_SAMPLES);
        sb16_start();
        audio_sb16_playing = true;
    }

    audio_write_eflags(flags);
}

void audio_release(uint32_t frequency_hz) {
    uint32_t flags = audio_read_eflags();
    __asm__ volatile("cli");

    if (!audio_sb16_available) {
        if (frequency_hz == 0 || frequency_hz == audio_fallback_frequency) {
            pcspk_stop();
            audio_fallback_frequency = 0;
        }
    } else {
        for (uint32_t i = 0; i < AUDIO_MAX_VOICES; i++) {
            struct AudioVoice *voice = &audio_voices[i];
            if (!voice->active) {
                continue;
            }
            if (frequency_hz != 0 && voice->frequency_hz != frequency_hz) {
                continue;
            }
            if (voice->remaining_samples > AUDIO_RELEASE_SAMPLES) {
                voice->remaining_samples = AUDIO_RELEASE_SAMPLES;
            }
        }
    }

    audio_write_eflags(flags);
}

void audio_stop(void) {
    uint32_t flags = audio_read_eflags();
    __asm__ volatile("cli");

    memset(audio_voices, 0, sizeof(audio_voices));
    memset(audio_dma_buffers, AUDIO_SILENCE_BYTE, sizeof(audio_dma_buffers));
    audio_fallback_frequency = 0;

    if (!audio_sb16_available) {
        pcspk_stop();
    } else if (audio_sb16_playing) {
        sb16_stop();
        audio_sb16_playing = false;
    }

    audio_write_eflags(flags);
}
