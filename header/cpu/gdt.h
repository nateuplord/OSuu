#ifndef _GDT_H
#define _GDT_H

#include <stdint.h>
#include <sys/types.h>

// Some GDT Constant
#define GDT_MAX_ENTRY_COUNT 32

/**
 * @note
 * As kernel SegmentDescriptor for code located at index `1` in GDT,
 * segment selector is `sizeof(SegmentDescriptor) * 1 = 0x8`
 */
#define GDT_KERNEL_CODE_SEGMENT_SELECTOR 0x8
#define GDT_KERNEL_DATA_SEGMENT_SELECTOR 0x10
#define GDT_USER_CODE_SEGMENT_SELECTOR 0x18
#define GDT_USER_DATA_SEGMENT_SELECTOR 0x20
#define GDT_TSS_SEGMENT_SELECTOR 0x28

extern struct GDTR gdtr;

/**
 * Stores system segment information.
 *
 * @note
 * Struct defined exactly as
 * [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html)
 * Segment Descriptor definition (Figure 3-8 Segment Descriptor).
 */
struct SegmentDescriptor {
    /* First 32-bit */

    /// 16-bit lower-bit segment limit
    uint16_t segment_low;

    /// 16-bit lower-bit base address
    uint16_t base_low;

    /* Next 16-bit (Bit 32 to 47) */

    /// 8-bit middle-bit base address
    uint8_t base_mid : 8;

    /// 4-bit contain type flags
    uint8_t type_bit : 4;

    /// 1-bit descriptor type (S): `0` = system, `1` = code or data
    uint8_t non_system : 1;

    /// 2-bit descriptor privilege level (DPL), ring 0-3
    uint8_t dpl : 2;

    /// 1-bit segment present flag (P)
    uint8_t seg_present : 1;

    /// 4-bit higher-bit segment limit (bits 19:16)
    uint8_t segment_high : 4;

    /// 1-bit available for use by system software (AVL)
    uint8_t avl : 1;

    /// 1-bit 64-bit code segment flag (L), IA-32e mode only
    uint8_t code_seg : 1;

    /// 1-bit default operation size (D/B): `0` = 16-bit, `1` = 32-bit
    uint8_t op_size : 1;

    /// 1-bit limit granularity (G): `0` = 1 byte, `1` = 4 KB units
    uint8_t granularity : 1;

    /// 8-bit higher-bit base address (bits 31:24)
    uint8_t base_high : 8;
} __attribute__((packed));

/**
 * Contains a list of segment descriptors.
 *
 * @note One GDT already defined in `memory.c`.
 * More details at
 * [OSDev Wiki](https://wiki.osdev.org/GDT_Tutorial)
 */
struct GlobalDescriptorTable {
    /// Fixed-width array of `SegmentDescriptor` with size `GDT_MAX_ENTRY_COUNT`
    struct SegmentDescriptor table[GDT_MAX_ENTRY_COUNT];
} __attribute__((packed));

/**
 * Carries information where's the GDT located and GDT size.
 * @note Global kernel variable defined at `memory.c`.
 */
struct GDTR {
    /// Global Descriptor Table size, use `sizeof` operator
    uint16_t size;

    /// GDT address, GDT should already defined properly
    struct GlobalDescriptorTable *address;
} __attribute__((packed));

#endif
