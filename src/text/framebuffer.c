#include "text/framebuffer.h"
#include "cpu/portio.h"
#include "stdlib/string.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    uint16_t pos = r * 80 + c;
    out(0x3D4, 0x0F);
    out(0x3D5, pos & 0xFF);
    out(0x3D4, 0x0E);
    out(0x3D5, (pos >> 8) & 0xFF);
}

void framebuffer_write(
    uint8_t row, 
    uint8_t col, 
    char c, 
    uint8_t fg,
    uint8_t bg
) {
    uint16_t attrib = (bg << 4) | (fg & 0x0f);
    
    volatile uint16_t *where;
    where = (volatile uint16_t *) FRAMEBUFFER_MEMORY_OFFSET + (row * 80 + col);
    *where = c | (attrib << 8);
}

void framebuffer_clear(void) {
    for (int i = 0; i < 80 * 25; i++) {
        FRAMEBUFFER_MEMORY_OFFSET[i * 2] = 0x00;     // character
        FRAMEBUFFER_MEMORY_OFFSET[i * 2 + 1] = 0x07; // color
    }
}
