#include "cpu/interrupt.h"
#include "cpu/portio.h"
#include "cpu/scheduling.h"
#include "driver/keyboard.h"
#include "memory/swap.h"

#define PIT_MAX_FREQUENCY 1193182
#define PIT_TIMER_FREQUENCY 1000
#define PIT_TIMER_COUNTER (PIT_MAX_FREQUENCY / PIT_TIMER_FREQUENCY)
#define PIT_COMMAND_REGISTER_PIO 0x43
#define PIT_COMMAND_VALUE_BINARY_MODE 0b0
#define PIT_COMMAND_VALUE_OPR_SQUARE_WAVE (0b011 << 1)
#define PIT_COMMAND_VALUE_ACC_LOHIBYTE (0b11 << 4)
#define PIT_COMMAND_VALUE_CHANNEL (0b00 << 6)
#define PIT_COMMAND_VALUE (PIT_COMMAND_VALUE_BINARY_MODE | \
                           PIT_COMMAND_VALUE_OPR_SQUARE_WAVE | \
                           PIT_COMMAND_VALUE_ACC_LOHIBYTE | \
                           PIT_COMMAND_VALUE_CHANNEL)
#define PIT_CHANNEL_0_DATA_PIO 0x40

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

void activate_timer_interrupt(void) {
    __asm__ volatile("cli");

    uint32_t pit_timer_counter_to_fire = PIT_TIMER_COUNTER;
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) (pit_timer_counter_to_fire & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO,
        (uint8_t) ((pit_timer_counter_to_fire >> 8) & 0xFF));

    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
    __asm__ volatile("sti");
}

struct InterruptFrame *main_interrupt_handler(void) {
    uintptr_t frame_address;
    /* The ISR stub puts the frame address in EBP before calling us. */
    __asm__ volatile("movl (%%ebp), %0" : "=r"(frame_address));
    struct InterruptFrame *frame = (struct InterruptFrame *) frame_address;

    switch (frame->int_number) {
        case PIC1_OFFSET + IRQ_TIMER:
            pic_ack(IRQ_TIMER);
            if (cpu_save(frame) == 0 && rr_switch() == 0) {
                struct InterruptFrame *next_frame = cpu_load(frame);
                if (next_frame != NULL) return next_frame;
            }
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;
        case 14:
            handle_page_fault(frame->int_stack.error_code);
            break;
        default:
            break;
    }
    return frame;
}
