#ifndef _IDT_H
#define _IDT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * IDT hard limit
 *
 * @see [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - ch. 6.10 _Interrupt Descriptor Table_
 */
#define IDT_MAX_ENTRY_COUNT 256
#define ISR_STUB_TABLE_LIMIT 64
#define INTERRUPT_GATE_R_BIT_1 0b000
#define INTERRUPT_GATE_R_BIT_2 0b110
#define INTERRUPT_GATE_R_BIT_3 0b0

/**
 * Interrupt Handler / ISR stub for reducing code duplication.
 * @note This array can be iterated in `initialize_idt()`
 */
extern void *isr_stub_table[ISR_STUB_TABLE_LIMIT];

extern struct IDTR idtr;

/**
 * IDT entry that points into interrupt handler
 *
 * @note Struct defined exactly in
 * [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - Figure 6-2: _IDT Gate Descriptors_
 */
struct IDTGate {
    /// Handler address, bits 0..15
    uint16_t offset_low;

    /// Code segment selector (usually the kernel code segment, e.g. 0x08)
    uint16_t segment;

    /// Reserved, must be 0
    uint8_t _reserved : 5;

    /// Must be 0 for interrupt/trap gates
    uint8_t _r_bit_1 : 3;

    /**
     * Gate type
     * @details Low 3 bits:
     * - `0b110` = interrupt gate (clears IF on entry),
     * - `0b111` = trap gate (IF unchanged).
     *
     * Combined with `gate_32` to form the 4-bit type.
     */
    uint8_t _r_bit_2 : 3;

    /**
     * Gate size (the "D" bit)
     * @details
     * `1` = 32-bit gate (type `0xE`/ `0xF`),
     * `0` = 16-bit gate (`0x6` / `0x7`)
     */
    uint8_t gate_32 : 1;

    /**
     * Storage segment (S) bit
     * @details Must be `0` for interrupt/trap gates
     */
    uint8_t _r_bit_3 : 1;

    /**
     * Descriptor privilege level (0-3)
     * @details
     * Minimum ring allowed to trigger this gate via the `int n` instruction.
     * Hardware interrupts/exceptions ignore it.
     */
    uint8_t dpl : 2;

    /**
     * Present (P) bit
     * @details Must be 1 for the entry to be used, otherwise the CPU raises #NP
     */
    uint8_t valid_bit : 1;

    /// Handler address, bits 16..31
    uint16_t offset_high;
} __attribute__((packed));

/**
 * Contains a list of `IDTGate`s.
 * @note One IDT already defined in `idt.c`
 */
struct InterruptDescriptorTable {
    /// Fixed-width array of `IDTGate` with size `IDT_MAX_ENTRY_COUNT`
    struct IDTGate table[IDT_MAX_ENTRY_COUNT];
} __attribute__((packed));

/**
 * Carries information where the IDT is located and its size.
 * @note Global kernel variable defined at `idt.c`.
 */
struct IDTR {
    /// Interrupt Descriptor Table size, use `sizeof` operator
    uint16_t size;

    /// IDT address, IDT should already defined properly
    struct InterruptDescriptorTable *address;
} __attribute__((packed));

/**
 * Set IDTGate with proper interrupt handler values.
 * @note Will directly edit global IDT variable and set values properly
 *
 * @param int_vector
 * Interrupt vector to handle
 *
 * @param handler_address
 * Interrupt handler address
 *
 * @param gdt_seg_selector
 * GDT segment selector.
 * For kernel, use `GDT_KERNEL_CODE_SEGMENT_SELECTOR`
 *
 * @param privilege
 * Descriptor privilege level
 */
void set_interrupt_gate(
    uint8_t int_vector,
    void *handler_address,
    uint16_t gdt_seg_selector,
    uint8_t privilege
);

/**
 * Set IDT with proper values and load with `lidt`
 */
void initialize_idt(void);

#endif
