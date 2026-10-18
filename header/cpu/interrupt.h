#ifndef _INTERRUPT_H
#define _INTERRUPT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* -- PIC constants -- */

// PIC interrupt offset
#define PIC1_OFFSET          0x20
#define PIC2_OFFSET          0x28

// PIC ports
#define PIC1                 0x20
#define PIC2                 0xA0
#define PIC1_COMMAND         PIC1
#define PIC1_DATA            (PIC1 + 1)
#define PIC2_COMMAND         PIC2
#define PIC2_DATA            (PIC2 + 1)

// PIC ACK & mask constant
#define PIC_ACK              0x20
#define PIC_DISABLE_ALL_MASK 0xFF

// PIC remap constants
#define ICW1_ICW4            0x01   ///< ICW4 (not) needed 
#define ICW1_SINGLE          0x02   ///< Single (cascade) mode
#define ICW1_INTERVAL4       0x04   ///< Call address interval 4 (8)
#define ICW1_LEVEL           0x08   ///< Level triggered (edge) mode
#define ICW1_INIT            0x10   ///< Initialization - required!

#define ICW4_8086            0x01   ///< 8086/88 (MCS-80/85) mode
#define ICW4_AUTO            0x02   ///< Auto (normal) EOI
#define ICW4_BUF_SLAVE       0x08   ///< Buffered mode/slave
#define ICW4_BUF_MASTER      0x0C   ///< Buffered mode/master
#define ICW4_SFNM            0x10   ///< Special fully nested (not)


/* -- PICs IRQ list -- */

// PIC Master
#define IRQ_TIMER           0
#define IRQ_KEYBOARD        1
#define IRQ_CASCADE         2
#define IRQ_COM2            3
#define IRQ_COM1            4
#define IRQ_LPT2            5
#define IRQ_FLOPPY_DISK     6
#define IRQ_LPT1_SPUR       7

// PIC Slave
#define IRQ_CMOS            8
#define IRQ_PERIPHERAL_1    9
#define IRQ_PERIPHERAL_2    10
#define IRQ_PERIPHERAL_3    11
#define IRQ_MOUSE           12
#define IRQ_FPU             13
#define IRQ_PRIMARY_ATA     14
#define IRQ_SECOND_ATA      15

/**
 * CPU registers values.
 */
struct CPURegister {
    struct {
        uint32_t edi;
        uint32_t esi;
    } __attribute__((packed)) index;    ///< CPU index registers (`di`, `si`)

    struct {
        uint32_t esp;
        uint32_t ebp;
    } __attribute__((packed)) stack;    ///< CPU tack registers (`bp`, `sp`)

    struct {
        uint32_t ebx;
        uint32_t edx;
        uint32_t ecx;
        uint32_t eax;
    } __attribute__((packed)) general;  ///< CPU general purpose registers (`a`, `b`, `c`, `d`)

    struct {
        uint32_t gs;
        uint32_t fs;
        uint32_t es;
        uint32_t ds;
    } __attribute__((packed)) segment;  ///< CPU extra segment registers (`gs`, `fs`, `es`, `ds`)
} __attribute__((packed));

/**
 * Data pushed by CPU when interrupt / exception is raised.
 *
 * @details 
 * When returning from interrupt handler with `iret`, `esp` must be pointing to `eip` pushed before 
 * or in other words, `CPURegister`, `int_number` and `error_code` should be popped from stack. 
 *
 * @note Refer to 
 * [Intel Manual](https://www.intel.com/content/www/us/en/content-details/782158/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-1-2a-2b-2c-2d-3a-3b-3c-3d-and-4.html):
 * - Figure 6-4: _Stack usage on transfer to Interrupt_.
 */
struct InterruptStack {
    uint32_t error_code;    ///< Error code that pushed with the exception
    uint32_t eip;           ///< Instruction pointer where interrupt is raised
    uint32_t cs;            ///< Code segment selector where interrupt is raised
    uint32_t eflags;        ///< CPU eflags register when interrupt is raised
} __attribute__((packed));

/**
 * Entirety of general CPU states exactly before interrupt.
 *
 * @note
 * When used for interrupt handler, `cpu.stack` is kernel state before C function called, not user stack when it get called. 
 *
 * Check `InterruptStack` and interprivilege interrupt for more detail.
 */
struct InterruptFrame {
    struct CPURegister    cpu;          ///< CPU state
    uint32_t              int_number;   ///< Interrupt vector value
    struct InterruptStack int_stack;    ///< Hardware-defined (x86) stack state @details Will not access interprivilege `ss` and `esp`
} __attribute__((packed));

// Activate PIC mask for keyboard only
void activate_keyboard_interrupt(void);

// I/O port wait, around 1-4 microsecond, for I/O synchronization purpose
void io_wait(void);

/**
 * Send ACK to PIC
 *
 * @param irq 
 * Interrupt request number destination. Note: `ACKED_IRQ = irq + PIC1_OFFSET`
 */
void pic_ack(uint8_t irq);

// Shift PIC interrupt number to `PIC1_OFFSET` and `PIC2_OFFSET` (master and slave)
void pic_remap(void);

/**
 * Main interrupt handler when any interrupt / exception is raised.
 * > DO NOT CALL THIS FUNCTION.
 * 
 * @details
 * This function will be called first if any `INT 0x00` - `0x40` is raised, 
 * and will call proper ISR for respective interrupt / exception.
 * 
 * @note
 * If interprivilege interrupt is raised, `ss` and `esp` is automatically out of `main_interrupt_handler()`
 * parameter. Can be checked with `((int*) info) + 4` for user `esp`, `5` for user `ss`
 * 
 * Again, this function is not for normal function call, all parameter will be automatically set when interrupt is called.
 * 
 * @param frame 
 * Information about CPU during interrupt is raised
 */
void main_interrupt_handler(struct InterruptFrame frame);

#endif