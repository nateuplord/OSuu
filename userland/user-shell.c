#include "syscall.h"
#include <stdint.h>

static const char message[] = "osu!mania";

int main(void) {
    /* Visible marker: "osu!mania" via the real syscall path (puts -> the
     * VBE graphics framebuffer), proving int 0x30 actually works from ring 3
     * instead of poking the legacy VGA text buffer directly. */
    syscall(SYSCALL_TEXT_PUTS, (uint32_t) message, sizeof(message) - 1, 0xF);

    __asm__ volatile("mov %0, %%eax"
                     : /* <Empty> */
                     : "r"(0xDEADBEEF));
    return 0;
}
