#include "cpu/gdt.h"
#include "cpu/interrupt.h"
#include <stdint.h>

/**
 * Predefined GDT.
 *
 * @note
 * Initial SegmentDescriptor already set properly according to:
 * - [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html)
 * - [OSDev Wiki](https://wiki.osdev.org/)
 *
 * Table entry:
 * `[{Null}, {Kernel Code}, {Kernel Data}, {User Code}, {User Data}, {TSS}].`
 */
struct GlobalDescriptorTable gdt = {
    .table = {
        {
            .segment_low = 0,
            .base_low = 0,
            .type_bit = 0,
            .non_system = 0,
            .dpl = 0,
            .seg_present = 0,
            .segment_high = 0,
            .avl = 0,
            .code_seg = 0,
            .op_size = 0,
            .granularity = 0,
            .base_high = 0,
        },
        {
            .segment_low = 0xFFFF,
            .base_low = 0,
            .type_bit = 0xA,
            .non_system = 1,
            .dpl = 0,
            .seg_present = 1,
            .segment_high = 0xF,
            .avl = 0,
            .code_seg = 0,
            .op_size = 1,
            .granularity = 1,
            .base_high = 0,
        },
        {
            .segment_low = 0xFFFF,
            .base_low = 0,
            .type_bit = 0x2,
            .non_system = 1,
            .dpl = 0,
            .seg_present = 1,
            .segment_high = 0xF,
            .avl = 0,
            .code_seg = 0,
            .op_size = 1,
            .granularity = 1,
            .base_high = 0,
        },
        {
            .segment_low = 0xFFFF,
            .base_low = 0,
            .type_bit = 0xA,
            .non_system = 1,
            .dpl = 3,
            .seg_present = 1,
            .segment_high = 0xF,
            .avl = 0,
            .code_seg = 0,
            .op_size = 1,
            .granularity = 1,
            .base_high = 0,
        },
        {
            .segment_low = 0xFFFF,
            .base_low = 0,
            .type_bit = 0x2,
            .non_system = 1,
            .dpl = 3,
            .seg_present = 1,
            .segment_high = 0xF,
            .avl = 0,
            .code_seg = 0,
            .op_size = 1,
            .granularity = 1,
            .base_high = 0,
        },
        {0}, // TSS descriptor is filled in by gdt_install_tss()
    }
};

/**
 * Predefined system GDTR.
 *
 * @note
 * GDT pointed by this variable is already set to point
 * `global_descriptor_table` above.
 *
 * From [OSDev Wiki](https://wiki.osdev.org/Global_Descriptor_Table),
 * `GDTR.size` is GDT size minus 1.
 */
struct GDTR gdtr = {
    .size = sizeof(gdt) - 1,
    .address = &gdt,
};

void gdt_install_tss(void) {
    struct SegmentDescriptor *tss = &gdt.table[5];
    uint32_t base = (uint32_t) &_interrupt_tss_entry;
    uint32_t limit = sizeof(struct TSSEntry) - 1;

    tss->segment_low = limit & 0xFFFF;
    tss->base_low = base & 0xFFFF;
    tss->base_mid = (base >> 16) & 0xFF;
    tss->type_bit = 0x9; // 32-bit TSS (available)
    tss->non_system = 0;
    tss->dpl = 0;
    tss->seg_present = 1;
    tss->segment_high = (limit >> 16) & 0xF;
    tss->avl = 0;
    tss->code_seg = 0;
    tss->op_size = 1;
    tss->granularity = 0;
    tss->base_high = (base >> 24) & 0xFF;
}
