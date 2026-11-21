#ifndef _KERNEL_ENTRYPOINT
#define _KERNEL_ENTRYPOINT

#include "cpu/gdt.h"

// Optional linker variable : Pointing to kernel start & end address
// Note : Use & operator, example : a = (uint32_t) &_linker_kernel_stack_top;
extern uint32_t _linker_kernel_virt_addr_start;
extern uint32_t _linker_kernel_virt_addr_end;

extern uint32_t _linker_kernel_phys_addr_start;
extern uint32_t _linker_kernel_phys_addr_end;

extern uint8_t _linker_kernel_stack_top;

/**
 * Load GDT from `gdtr` and complete init for protected mode.
 *
 * @note
 * This procedure is implemented in asm and will not activate the CPU interrupt flag
 *
 * @warning
 * Invalid address / definition of GDT will cause bootloop after calling this procedure.
 *
 * @param gdtr
 * Pointer to already defined & initialized GDTR
 */
extern void load_gdt(struct GDTR *gdtr);

/**
 * Load the TSS selector (GDT_TSS_SELECTOR) into the task register.
 *
 * @note
 * The TSS descriptor must already be installed with `gdt_install_tss()`.
 */
extern void set_tss_register(void);

/**
 * Execute user program from kernel, one way jump. This function is defined in asm source code.
 *
 * @param virtual_addr Pointer into user program that already in memory
 * @warning            Assuming pointed memory is properly loaded with instruction
 */
extern void kernel_execute_user_program(void *virtual_addr);

#endif
