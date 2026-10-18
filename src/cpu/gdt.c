#include "cpu/gdt.h"

/**
 * Predefined GDT.
 *
 * @note
 * Initial SegmentDescriptor already set properly according to:
 * - [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html) 
 * - [OSDev Wiki](https://wiki.osdev.org/)
 *
 * Table entry : [{Null Descriptor}, {Kernel Code}, {Kernel Data (variable, etc)}, ...].
 */
struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        {
            .segment_low    = 0,
            .base_low       = 0,
            .type_bit       = 0,
            .non_system     = 0,
            .dpl            = 0,
            .seg_present    = 0,
            .segment_high   = 0,
            .avl            = 0,
            .code_seg       = 0,
            .op_size        = 0,
            .granularity    = 0,
            .base_high      = 0,
        },
        {
            .segment_low    = 0xFFFF,
            .base_low       = 0,
            .type_bit       = 0xA,
            .non_system     = 1,
            .dpl            = 0,
            .seg_present    = 1,
            .segment_high   = 0xF,
            .avl            = 0,
            .code_seg       = 0,
            .op_size        = 1,
            .granularity    = 1,
            .base_high      = 0,
        },
        {
            .segment_low    = 0xFFFF,
            .base_low       = 0,
            .type_bit       = 0x2,
            .non_system     = 1,
            .dpl            = 0,
            .seg_present    = 1,
            .segment_high   = 0xF,
            .avl            = 0,
            .code_seg       = 0,
            .op_size        = 1,
            .granularity    = 1,
            .base_high      = 0,
        },
        {0},
    }};

/**
 * Predefined system GDTR.
 * 
 * @note
 * GDT pointed by this variable is already set to point `global_descriptor_table` above. 
 *
 * From [OSDev Wiki](https://wiki.osdev.org/Global_Descriptor_Table), `GDTR.size` is GDT size minus 1.
 */
struct GDTR _gdt_gdtr = {
    .size       = sizeof(global_descriptor_table) - 1,
    .address    = &global_descriptor_table,
};
