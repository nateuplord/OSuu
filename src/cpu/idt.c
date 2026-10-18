#include "cpu/idt.h"
#include "cpu/gdt.h"
#include <stdint.h>

struct InterruptDescriptorTable idt = {.table = {}};

struct IDTR idtr = {
    .size       = sizeof(idt) - 1,
    .address    = &idt,
};

void initialize_idt(void) {
    for (uint16_t i = 0; i < ISR_STUB_TABLE_LIMIT; i++) {
        set_interrupt_gate(
            i, 
            isr_stub_table[i], 
            GDT_KERNEL_CODE_SEGMENT_SELECTOR,
            0
        );
    }
    __asm__ volatile(
        "lidt %0" 
        : // Ignored branch
        : "m"(idtr));
    __asm__ volatile("sti");
}

void set_interrupt_gate(
    uint8_t int_vector, 
    void *handler_address,
    uint16_t gdt_seg_selector, 
    uint8_t privilege
) {
    struct IDTGate *idt_gate = &idt.table[int_vector];
    
    // Target system 32-bit and flag this as valid interrupt gate
    idt_gate->offset_low = (uint32_t)handler_address & 0xFFFF;
    idt_gate->segment = gdt_seg_selector;
    idt_gate->dpl = privilege;
    idt_gate->_r_bit_1 = INTERRUPT_GATE_R_BIT_1;
    idt_gate->_r_bit_2 = INTERRUPT_GATE_R_BIT_2;
    idt_gate->_r_bit_3 = INTERRUPT_GATE_R_BIT_3;
    idt_gate->gate_32 = 1;
    idt_gate->valid_bit = 1;
    idt_gate->offset_high = ((uint32_t)handler_address >> 16) & 0xFFFF;
}
