#include "kernel_api.h"
#include <stdbool.h>
#include <stdint.h>

#define CLOCK_ROW 59u
#define CLOCK_COLUMN 72u
#define CLOCK_TICKS_PER_UPDATE 2u

static void clock_write_pair(
    uint8_t value,
    uint8_t column
) {
    framebuffer_write(
        (uint8_t) CLOCK_ROW, column, (char) ('0' + value / 10u), 0xF, 0
    );
    framebuffer_write(
        (uint8_t) CLOCK_ROW, (uint8_t) (column + 1u),
        (char) ('0' + value % 10u), 0xF, 0
    );
}

static void clock_draw(const struct CmosDateTime *now) {
    clock_write_pair(now->hour, (uint8_t) CLOCK_COLUMN);
    framebuffer_write(
        (uint8_t) CLOCK_ROW, (uint8_t) (CLOCK_COLUMN + 2u), ':', 0xF, 0
    );
    clock_write_pair(now->minute, (uint8_t) (CLOCK_COLUMN + 3u));
    framebuffer_write(
        (uint8_t) CLOCK_ROW, (uint8_t) (CLOCK_COLUMN + 5u), ':', 0xF, 0
    );
    clock_write_pair(now->second, (uint8_t) (CLOCK_COLUMN + 6u));
}

int main(void) {
    uint32_t last_update_tick = cmos_get_periodic_ticks();

    while (true) {
        struct CmosDateTime now;
        if (cmos_read_datetime(&now)) {
            clock_draw(&now);
        }

        while (
            cmos_get_periodic_ticks() - last_update_tick <
            CLOCK_TICKS_PER_UPDATE
        ) {
            yield();
        }
        last_update_tick = cmos_get_periodic_ticks();
    }

    return 0;
}
