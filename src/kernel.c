#include "header/cpu/gdt.h"
#include "header/kernel-entrypoint.h"
#include "header/text/framebuffer.h"
#include <stdbool.h>
#include <stdint.h>

void kernel_setup(void) {
  load_gdt(&_gdt_gdtr);
  framebuffer_clear();
  framebuffer_write(3, 8, 'W', 0xF, 0);
  framebuffer_write(3, 9, 'e', 0xF, 0);
  framebuffer_write(3, 10, 'l', 0xF, 0);
  framebuffer_write(3, 11, 'c', 0xF, 0);
  framebuffer_write(3, 12, 'o', 0xF, 0);
  framebuffer_write(3, 13, 'm', 0xF, 0);
  framebuffer_write(3, 14, 'e', 0xF, 0);
  framebuffer_write(3, 15, ' ', 0xF, 0);
  framebuffer_write(3, 16, 'T', 0xF, 0);
  framebuffer_write(3, 17, 'o', 0xF, 0);

  framebuffer_write(4, 18, 'M', 0x3, 0);
  framebuffer_write(4, 19, 'a', 0x3, 0);
  framebuffer_write(4, 20, 'i', 0x3, 0);
  framebuffer_write(4, 21, 'M', 0x3, 0);
  framebuffer_write(4, 22, 'a', 0x3, 0);
  framebuffer_write(4, 23, 'i', 0x3, 0);

  while (true)
    ;
}
