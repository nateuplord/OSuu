#include "driver/cmos.h"
#include "cpu/interrupt.h"
#include "cpu/portio.h"
#include <stdbool.h>
#include <stdint.h>

static volatile uint32_t cmos_periodic_ticks = 0;

/**
 * Select `reg` with NMIs masked, access its data, then re-enable NMIs. Writing
 * bit 7 low to the index port is what re-enables them; reading alone is not.
 */
static uint8_t cmos_read(uint8_t reg) {
    out(CMOS_INDEX_PORT, (uint8_t) (CMOS_NMI_DISABLE | reg));
    uint8_t value = in(CMOS_DATA_PORT);
    out(CMOS_INDEX_PORT, 0);
    return value;
}

static void cmos_write(
    uint8_t reg,
    uint8_t value
) {
    out(CMOS_INDEX_PORT, (uint8_t) (CMOS_NMI_DISABLE | reg));
    out(CMOS_DATA_PORT, value);
    out(CMOS_INDEX_PORT, 0);
}

static bool cmos_wait_update_clear(void) {
    for (uint32_t i = 0; i < 1000000u; ++i) {
        if ((cmos_read(CMOS_REG_STATUS_A) & CMOS_STATUS_A_UIP) == 0) {
            return true;
        }
    }
    return false;
}

static uint8_t cmos_decode_value(
    uint8_t value,
    bool binary_mode
) {
    if (binary_mode) {
        return value;
    }
    return (uint8_t) (((value >> 4) * 10u) + (value & 0x0Fu));
}

static uint8_t cmos_decode_hour(
    uint8_t raw_hour,
    uint8_t status_b
) {
    bool binary_mode = (status_b & CMOS_STATUS_B_BINARY) != 0;
    uint8_t hour;

    if ((status_b & CMOS_STATUS_B_24H) != 0) {
        hour = cmos_decode_value((uint8_t) (raw_hour & 0x3Fu), binary_mode);
    } else {
        hour = cmos_decode_value((uint8_t) (raw_hour & 0x7Fu), binary_mode);
        if (hour == 12) {
            hour = 0;
        }
        if ((raw_hour & CMOS_STATUS_B_PM) != 0) {
            hour = (uint8_t) (hour + 12);
        }
    }
    return hour + 7;
}

static uint16_t cmos_decode_year(
    uint8_t raw_year,
    uint8_t raw_century,
    bool binary_mode
) {
    uint8_t year = cmos_decode_value(raw_year, binary_mode);
    uint8_t century = cmos_decode_value(raw_century, binary_mode);

    // Some CMOS implementations (including common VM defaults) leave the
    // century register unconfigured. Fall back to the usual two-digit pivot.
    if (century == 0 || century > 99) {
        return (uint16_t) ((year >= 70 ? 1900u : 2000u) + year);
    }
    return (uint16_t) (century * 100u + year);
}

void cmos_init(void) {
    // Mask periodic interrupts while configuring, and clear any stale latch.
    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);
    cmos_write(CMOS_REG_STATUS_B, (uint8_t) (status_b & ~CMOS_STATUS_B_PIE));
    (void) cmos_read(CMOS_REG_STATUS_C);

    // Keep the crystal divider bits; only the rate nibble changes.
    uint8_t status_a = cmos_read(CMOS_REG_STATUS_A);
    status_a = (uint8_t) ((status_a & ~CMOS_STATUS_A_RATE_MASK) |
                          CMOS_STATUS_A_RATE_2HZ);
    cmos_write(CMOS_REG_STATUS_A, status_a);

    status_b = cmos_read(CMOS_REG_STATUS_B);
    cmos_write(CMOS_REG_STATUS_B, (uint8_t) (status_b | CMOS_STATUS_B_PIE));

    cmos_periodic_ticks = 0;

    // IRQ8 comes in through the slave PIC's IRQ2 cascade.
    out(PIC1_DATA, (uint8_t) (in(PIC1_DATA) & ~(1u << IRQ_CASCADE)));
    out(PIC2_DATA, (uint8_t) (in(PIC2_DATA) & ~(1u << (IRQ_CMOS - 8))));
}

bool cmos_read_datetime(struct CmosDateTime *out) {
    if (out == NULL) {
        return false;
    }

    static const uint8_t registers[] = {
        CMOS_REG_SECONDS, CMOS_REG_MINUTES, CMOS_REG_HOURS, CMOS_REG_WEEKDAY,
        CMOS_REG_DAY,     CMOS_REG_MONTH,   CMOS_REG_YEAR,  CMOS_REG_CENTURY,
    };
    const uint32_t field_count = sizeof(registers) / sizeof(registers[0]);
    uint8_t first[sizeof(registers)];
    uint8_t second[sizeof(registers)];

    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);
    bool binary_mode = (status_b & CMOS_STATUS_B_BINARY) != 0;

    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        if (!cmos_wait_update_clear()) {
            continue;
        }
        for (uint32_t i = 0; i < field_count; ++i) {
            first[i] = cmos_read(registers[i]);
        }

        // A second UIP-clear read confirms nothing rolled over midway.
        if (!cmos_wait_update_clear()) {
            continue;
        }
        for (uint32_t i = 0; i < field_count; ++i) {
            second[i] = cmos_read(registers[i]);
        }

        bool stable = true;
        for (uint32_t i = 0; i < field_count; ++i) {
            if (first[i] != second[i]) {
                stable = false;
                break;
            }
        }
        if (!stable) {
            continue;
        }

        uint8_t *values = first;
        out->second = cmos_decode_value(values[0], binary_mode);
        out->minute = cmos_decode_value(values[1], binary_mode);
        out->hour = cmos_decode_hour(values[2], status_b);
        out->weekday = cmos_decode_value(values[3], binary_mode);
        out->day = cmos_decode_value(values[4], binary_mode);
        out->month = cmos_decode_value(values[5], binary_mode);
        out->year = cmos_decode_year(values[6], values[7], binary_mode);
        return true;
    }
    return false;
}

uint32_t cmos_get_periodic_ticks(void) {
    return cmos_periodic_ticks;
}

void cmos_isr(void) {
    // Reading status C clears the interrupt latch; otherwise IRQ8 stays
    // asserted forever.
    (void) cmos_read(CMOS_REG_STATUS_C);
    ++cmos_periodic_ticks;
    pic_ack(IRQ_CMOS);
}
