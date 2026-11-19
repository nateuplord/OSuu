#ifndef _KERNEL_ENTRYPOINT
#define _KERNEL_ENTRYPOINT

#include "cpu/gdt.h"

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

/** Virtual address just past the kernel stack, defined in `linker.ld`. */
extern uint8_t _linker_kernel_stack_top;

#endif
