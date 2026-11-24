#include "driver/sb16.h"
#include "cpu/interrupt.h"
#include "cpu/portio.h"
#include "driver/audio.h"
#include "process/process.h"
#include <stdbool.h>
#include <stdint.h>

#define SB16_BASE_PORT 0x220u
#define SB16_MIXER_ADDRESS_PORT (SB16_BASE_PORT + 4u)
#define SB16_MIXER_DATA_PORT (SB16_BASE_PORT + 5u)
#define SB16_RESET_PORT (SB16_BASE_PORT + 6u)
#define SB16_DSP_WRITE_PORT (SB16_BASE_PORT + 0x0Cu)
#define SB16_DSP_STATUS_PORT (SB16_BASE_PORT + 0x0Eu)
#define SB16_DSP_DATA_PORT (SB16_BASE_PORT + 0x0Au)

/// DSP status port bit: a set bit means the DSP cannot accept a write.
#define SB16_DSP_WRITE_BUSY 0x80u
#define SB16_DSP_DATA_READY 0x80u
#define SB16_DSP_RESET_ACK 0xAAu

#define SB16_DSP_SET_SAMPLE_RATE 0x41u
#define SB16_DSP_PLAY_8BIT 0x14u
#define SB16_DSP_SPEAKER_ON 0xD1u
#define SB16_DSP_SPEAKER_OFF 0xD3u

#define SB16_MIXER_VOICE_VOLUME 0x04u
#define SB16_MIXER_MASTER_VOLUME 0x22u
#define SB16_VOLUME 0xB0u

// 22,050 Hz: the DSP expects the high byte before the low byte.
#define SB16_SAMPLE_RATE_HIGH 0x56u
#define SB16_SAMPLE_RATE_LOW 0x42u

#define DMA1_CHANNEL 1u
#define DMA1_ADDRESS_PORT 0x02u
#define DMA1_COUNT_PORT 0x03u
#define DMA1_PAGE_PORT 0x83u
#define DMA_SINGLE_MASK_PORT 0x0Au
#define DMA_MODE_PORT 0x0Bu
#define DMA_CLEAR_FLIP_FLOP_PORT 0x0Cu
#define DMA_MASK_BIT 0x04u
// channel 1, single-cycle, increment, memory -> device
#define DMA_MODE_PLAYBACK 0x48u

#define PORT_POLL_LIMIT 100000u

static uint8_t *sb16_buffers[2];
static uint16_t sb16_sample_count = 0;
static uint32_t sb16_active_buffer = 0;
static bool sb16_playing = false;

static void sb16_dsp_write(uint8_t value) {
    for (uint32_t attempt = 0; attempt < PORT_POLL_LIMIT; attempt++) {
        if ((in(SB16_DSP_WRITE_PORT) & SB16_DSP_WRITE_BUSY) == 0) {
            out(SB16_DSP_WRITE_PORT, value);
            return;
        }
        io_wait();
    }
}

static bool sb16_reset(void) {
    out(SB16_RESET_PORT, 1u);
    for (uint32_t i = 0; i < 100u; i++) {
        io_wait();
    }
    out(SB16_RESET_PORT, 0u);

    for (uint32_t attempt = 0; attempt < PORT_POLL_LIMIT; attempt++) {
        if ((in(SB16_DSP_STATUS_PORT) & SB16_DSP_DATA_READY) != 0 &&
            in(SB16_DSP_DATA_PORT) == SB16_DSP_RESET_ACK) {
            return true;
        }
        io_wait();
    }
    return false;
}

static void sb16_start_buffer(uint32_t index) {
    uint32_t physical =
        (uint32_t) sb16_buffers[index] - KERNEL_VIRTUAL_ADDRESS_BASE;
    uint16_t count = (uint16_t) (sb16_sample_count - 1u);

    out(DMA_SINGLE_MASK_PORT, (uint8_t) (DMA_MASK_BIT | DMA1_CHANNEL));
    out(DMA_CLEAR_FLIP_FLOP_PORT, 0xFFu);
    out(DMA1_ADDRESS_PORT, (uint8_t) (physical & 0xFFu));
    out(DMA1_ADDRESS_PORT, (uint8_t) ((physical >> 8) & 0xFFu));
    out(DMA1_PAGE_PORT, (uint8_t) ((physical >> 16) & 0xFFu));
    out(DMA1_COUNT_PORT, (uint8_t) (count & 0xFFu));
    out(DMA1_COUNT_PORT, (uint8_t) ((count >> 8) & 0xFFu));
    out(DMA_MODE_PORT, (uint8_t) (DMA_MODE_PLAYBACK | DMA1_CHANNEL));
    out(DMA_SINGLE_MASK_PORT, DMA1_CHANNEL);

    sb16_dsp_write(SB16_DSP_PLAY_8BIT);
    sb16_dsp_write((uint8_t) (count & 0xFFu));
    sb16_dsp_write((uint8_t) ((count >> 8) & 0xFFu));
    sb16_active_buffer = index;
}

bool sb16_init(
    uint8_t *buffer_a,
    uint8_t *buffer_b,
    uint16_t sample_count
) {
    sb16_buffers[0] = buffer_a;
    sb16_buffers[1] = buffer_b;
    sb16_sample_count = sample_count;
    sb16_active_buffer = 0;
    sb16_playing = false;

    if (!sb16_reset()) {
        return false;
    }

    sb16_dsp_write(SB16_DSP_SET_SAMPLE_RATE);
    sb16_dsp_write(SB16_SAMPLE_RATE_HIGH);
    sb16_dsp_write(SB16_SAMPLE_RATE_LOW);

    out(SB16_MIXER_ADDRESS_PORT, SB16_MIXER_VOICE_VOLUME);
    out(SB16_MIXER_DATA_PORT, SB16_VOLUME);
    out(SB16_MIXER_ADDRESS_PORT, SB16_MIXER_MASTER_VOLUME);
    out(SB16_MIXER_DATA_PORT, SB16_VOLUME);

    out(PIC1_DATA, (uint8_t) (in(PIC1_DATA) & ~(1u << IRQ_LPT2)));
    return true;
}

void sb16_start(void) {
    sb16_dsp_write(SB16_DSP_SPEAKER_ON);
    sb16_start_buffer(0);
    sb16_playing = true;
}

void sb16_stop(void) {
    if (!sb16_playing) {
        return;
    }
    out(DMA_SINGLE_MASK_PORT, (uint8_t) (DMA_MASK_BIT | DMA1_CHANNEL));
    sb16_dsp_write(SB16_DSP_SPEAKER_OFF);
    sb16_playing = false;
}

void sb16_isr(void) {
    // Reading the DSP status port acknowledges the 8-bit DMA interrupt.
    (void) in(SB16_DSP_STATUS_PORT);
    pic_ack(IRQ_LPT2);

    if (!sb16_playing) {
        return;
    }

    uint32_t next = 1u - sb16_active_buffer;
    audio_render_block(sb16_buffers[next], sb16_sample_count);
    sb16_start_buffer(next);
}
