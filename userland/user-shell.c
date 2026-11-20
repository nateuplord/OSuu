#include <stdint.h>

int main(void) {
    /* Visible marker: a 'U' in the top-left cell of the VGA text buffer. */
    volatile uint16_t *vga = (volatile uint16_t *) 0xB8000;
    vga[0] = (uint16_t) (0x0F00 | 'U');

    __asm__ volatile("mov %0, %%eax"
                     : /* <Empty> */
                     : "r"(0xDEADBEEF));
    return 0;
}
