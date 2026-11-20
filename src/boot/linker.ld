ENTRY(loader)                       /* the name of the entry label */
SECTIONS {
    . = 0x00100000;

    .multiboot ALIGN (0x1000) :
    {
        *(.multiboot)               /* put GRUB multiboot header at front */
    }

    .setup.text ALIGN (0x1000) :
    {
        *(.setup.text)              /* initial setup code: runs before paging is enabled */
    }

    . += 0xC0000000;

    _linker_kernel_virt_addr_start  = .;
    _linker_kernel_phys_addr_start = . - 0xC0000000;

    .text ALIGN (0x1000) : AT (ADDR (.text) - 0xC0000000)
    {
        *(.text*)                    /* all text sections from all files */
    }

    .rodata ALIGN (0x1000) : AT (ADDR (.rodata) - 0xC0000000)
    {
        *(.rodata*)                 /* all read-only data sections from all files */
    }

    .data ALIGN (0x1000) : AT (ADDR (.data) - 0xC0000000)
    {
        *(.data*)                    /* all data sections from all files */
    }

    .bss ALIGN (0x1000) : AT (ADDR (.bss) - 0xC0000000)
    {
        *(COMMON)                   /* all COMMON sections from all files */
        *(.bss*)                     /* all bss sections from all files */

        [a-z\/]*kernel-entrypoint.o(.bss)
        _linker_kernel_stack_top = .;
    }

    /* Linker variable that can be used in kernel */
    _linker_kernel_virt_addr_end = .;
    _linker_kernel_phys_addr_end = . - 0xC0000000;

    . = ALIGN(0x1000);
    realmode_lma = .;

    . = 0x9000;
    .realmode : AT(realmode_lma - 0xC0000000)
    {
        realmode_start = .;
        *(.realmode)
        realmode_end = .;
    }
    realmode_size = realmode_end - realmode_start;
}
