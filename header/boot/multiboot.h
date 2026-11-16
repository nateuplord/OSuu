#ifndef _MULTIBOOT
#define _MULTIBOOT

#include <stdint.h>

// Using references from https://wiki.osdev.org/Multiboot#Multiboot_2
struct Multiboot2Info {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

struct Multiboot2Tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

#endif
