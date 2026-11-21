#include <stdint.h>

static const char message[] = "osu!mania";

int main(void) {
    /* Visible marker: "osu!mania" at the start of the VGA text buffer. */
    volatile uint16_t *vga = (volatile uint16_t *) 0xB8000;

    for (uint32_t i = 0; i < sizeof(message) - 1; ++i) {
        vga[i] = (uint16_t) (0x0F00 | message[i]);
    }

    __asm__ volatile("mov %0, %%eax"
                     : /* <Empty> */
                     : "r"(0xDEADBEEF));
    return 0;
}
