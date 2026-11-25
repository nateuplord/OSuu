// memtest - Phase 1 check for MAP_REGION (syscall 34) and full teardown.
//
// 1. map_region() rejects bad ranges (empty, wrapping, into the kernel half).
// 2. Maps 32 MiB at 0x10000000 - a region with no page table, which would
//    otherwise kill the process on first touch - and touches every page:
//    each must arrive zeroed and keep what's written to it.
// 3. Hands back to the shell. RAM is 128 MiB (~112 MiB for processes), so if
//    exit leaked demand-paged memory, about the 4th run in a row would die
//    out of memory. Running it 5+ times back to back is the leak test.
#include "kernel_api.h"
#include <stdbool.h>
#include <stdint.h>

#define REGION_BASE 0x10000000u
#define REGION_SIZE (32u * 1024u * 1024u)
#define PAGE_SIZE 4096u

#define COLOR_TEXT 0x07
#define COLOR_OK 0x0A
#define COLOR_FAIL 0x0C

static void print(const char *s, uint8_t color) {
    uint32_t n = 0;
    while (s[n] != '\0')
        n++;
    framebuffer_puts(s, n, color);
}

static void print_uint(uint32_t value, uint8_t color) {
    char buf[11];
    uint32_t n = 0;
    do {
        buf[n++] = (char) ('0' + value % 10);
        value /= 10;
    } while (value != 0);
    char out[11];
    for (uint32_t i = 0; i < n; i++)
        out[i] = buf[n - 1 - i];
    framebuffer_puts(out, n, color);
}

static bool check(const char *label, bool ok) {
    print(ok ? "  [ OK ] " : "  [FAIL] ", ok ? COLOR_OK : COLOR_FAIL);
    print(label, COLOR_TEXT);
    print("\n", COLOR_TEXT);
    return ok;
}

int main(void) {
    framebuffer_clear();
    framebuffer_set_cursor(0, 0);
    print("memtest - MAP_REGION + teardown\n\n", COLOR_TEXT);

    bool all_ok = true;

    print("Rejects bad ranges:\n", COLOR_TEXT);
    all_ok &= check("size 0", !map_region((void *) REGION_BASE, 0));
    all_ok &= check(
        "range crossing 0xC0000000",
        !map_region((void *) 0xBFFF0000u, 0x20000u)
    );
    all_ok &= check(
        "range that wraps past 4 GiB",
        !map_region((void *) 0xFFFFF000u, 0x2000u)
    );

    print("\nMaps 32 MiB at 0x10000000:\n", COLOR_TEXT);
    if (!check("map_region accepted", map_region((void *) REGION_BASE, REGION_SIZE))) {
        sleep_ms(3000);
        return_to_shell();
    }

    // Touch every page. A fault here without the PDEs would kill us outright,
    // so merely getting through this loop proves MAP_REGION worked.
    uint32_t pages = REGION_SIZE / PAGE_SIZE;
    uint32_t not_zeroed = 0;
    for (uint32_t i = 0; i < pages; i++) {
        volatile uint32_t *word =
            (volatile uint32_t *) (REGION_BASE + i * PAGE_SIZE);
        if (word[0] != 0 || word[1023] != 0)
            not_zeroed++;
        word[0] = 0xD00D0000u ^ i;
        word[1023] = ~i;
    }
    all_ok &= check("every page touched without a fault", true);
    all_ok &= check("every new page arrived zeroed", not_zeroed == 0);

    uint32_t bad = 0;
    for (uint32_t i = 0; i < pages; i++) {
        volatile uint32_t *word =
            (volatile uint32_t *) (REGION_BASE + i * PAGE_SIZE);
        if (word[0] != (0xD00D0000u ^ i) || word[1023] != ~i)
            bad++;
    }
    all_ok &= check("every page kept its data", bad == 0);
    print("  pages: ", COLOR_TEXT);
    print_uint(pages, COLOR_TEXT);
    print("  bad: ", COLOR_TEXT);
    print_uint(bad + not_zeroed, bad + not_zeroed ? COLOR_FAIL : COLOR_TEXT);
    print("\n\n", COLOR_TEXT);

    print(all_ok ? "ALL PASSED" : "SOME CHECKS FAILED", all_ok ? COLOR_OK : COLOR_FAIL);
    print(" - back to shell in 3 s. Run it 5+ times to check for leaks.\n", COLOR_TEXT);
    sleep_ms(3000);
    return_to_shell();
}
