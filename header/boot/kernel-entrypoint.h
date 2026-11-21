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

extern void kernel_execute_user_program(void *virtual_addr);

#endif
