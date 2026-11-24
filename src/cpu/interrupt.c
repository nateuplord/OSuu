#include "cpu/interrupt.h"
#include "boot/kernel-entrypoint.h"
#include "cpu/portio.h"
#include "cpu/syscall.h"
#include "driver/cmos.h"
#include "driver/keyboard.h"
#include "driver/pit.h"
#include "driver/sb16.h"
#include "memory/swap.h"

struct TSSEntry _interrupt_tss_entry = {
    .esp0 = (uint32_t) &_linker_kernel_stack_top,
    .ss0 = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    // Frame address is the caller's EBP; +8 skips the saved EBP and return address.
    _interrupt_tss_entry.esp0 = (uint32_t) __builtin_frame_address(0) + 8;
}

void set_tss_kernel_stack(uint32_t stack_top) {
    _interrupt_tss_entry.esp0 = stack_top;
}

void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) {
        out(PIC2_COMMAND, PIC_ACK);
    }
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    // Starts the initialization sequence in cascade mode
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    // ICW2: Master PIC vector offset
    out(PIC1_DATA, PIC1_OFFSET);
    io_wait();

    // ICW2: Slave PIC vector offset
    out(PIC2_DATA, PIC2_OFFSET);
    io_wait();

    // ICW3: tell Master PIC, slave PIC at IRQ2 (0000 0100)
    out(PIC1_DATA, 0b0100);
    io_wait();

    // ICW3: tell Slave PIC its cascade identity (0000 0010)
    out(PIC2_DATA, 0b0010);
    io_wait();

    out(PIC1_DATA, ICW4_8086);
    io_wait();

    out(PIC2_DATA, ICW4_8086);
    io_wait();

    // Disable all interrupts
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_TIMER:
            pit_isr();
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;
        case PIC1_OFFSET + IRQ_LPT2:
            sb16_isr();
            break;
        case PIC2_OFFSET + (IRQ_CMOS - 8):
            cmos_isr();
            break;
        case 14:
            handle_page_fault(frame.int_stack.error_code);
            break;
        case SYSCALL_INTERRUPT_VECTOR:
            syscall(frame);
            break;
        default:
            break;
    }
}
