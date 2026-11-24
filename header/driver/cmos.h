#ifndef _CMOS_H
#define _CMOS_H

#include <stdbool.h>
#include <stdint.h>

/// CMOS index/address port. Bit 7 of the written value disables NMIs.
#define CMOS_INDEX_PORT 0x70

/// CMOS data port. The value at the selected index is read/written here.
#define CMOS_DATA_PORT 0x71

#define CMOS_REG_SECONDS 0x00
#define CMOS_REG_MINUTES 0x02
#define CMOS_REG_HOURS 0x04
#define CMOS_REG_WEEKDAY 0x06
#define CMOS_REG_DAY 0x07
#define CMOS_REG_MONTH 0x08
#define CMOS_REG_YEAR 0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B
#define CMOS_REG_STATUS_C 0x0C
#define CMOS_REG_CENTURY 0x32

#define CMOS_NMI_DISABLE 0x80

#define CMOS_STATUS_A_UIP 0x80
#define CMOS_STATUS_A_RATE_MASK 0x0F
#define CMOS_STATUS_A_RATE_2HZ 0x0F

#define CMOS_STATUS_B_24H 0x02
#define CMOS_STATUS_B_BINARY 0x04
#define CMOS_STATUS_B_PIE 0x40
#define CMOS_STATUS_B_PM 0x80

/**
 * Decoded CMOS real-time clock value. `weekday` uses the CMOS convention:
 * 1 = Sunday, 2 = Monday, ..., 7 = Saturday.
 */
struct CmosDateTime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t weekday;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

/**
 * Configure CMOS for 2 Hz periodic interrupts on IRQ8 and clear any pending
 * interrupt latch. Call before the IDT is installed / interrupts enabled.
 */
void cmos_init(void);

/**
 * Read the current date and time. Handles BCD/binary and 12/24-hour modes;
 * returns false if the clock never became stable or `out` is NULL.
 */
bool cmos_read_datetime(struct CmosDateTime *out);

/// Number of periodic CMOS interrupts since cmos_init().
uint32_t cmos_get_periodic_ticks(void);

/// IRQ8 handler: clear the CMOS interrupt latch, count the tick, ACK the PIC.
void cmos_isr(void);

#endif
