ENTRY(loader)                       /* the name of the entry label */
SECTIONS {
    . = 0x00100000;                 /* the code should be loaded at 1 MB */
    .multiboot ALIGN (0x1000) :     /* align at 4 KB */
    {
        *(.multiboot)               /* GRUB multiboot header */
    }
    .text ALIGN (0x1000) :          /* align at 4 KB */
    {
        *(.text)                    /* all text sections from all files */
    }
    .rodata ALIGN (0x1000) :        /* align at 4 KB */
    {
        *(.rodata*)                 /* all read-only data sections from all files */
    }
    .data ALIGN (0x1000) :          /* align at 4 KB */
    {
        *(.data)                    /* all data sections from all files */
    }
    .bss ALIGN (0x1000) :           /* align at 4 KB */
    {
        *(COMMON)                   /* all COMMON sections from all files */
        *(.bss)                     /* all bss sections from all files */
    }

    /* Real-mode VBE trampoline: must physically RUN from a low address
     * (16-bit real mode code can't address anything near 0x00100000), but
     * is still loaded as part of the normal kernel image. AT(realmode_lma)
     * keeps its bytes stored sequentially in the file/at load time, while
     * giving the code itself a VMA of 0x9000 so its own internal jumps and
     * data references resolve to small, 16-bit-representable offsets.
     * vbe.s copies it from realmode_lma to realmode_start at runtime before
     * jumping there. */
    . = ALIGN(0x1000);
    realmode_lma = .;

    . = 0x9000;
    .realmode : AT(realmode_lma)
    {
        realmode_start = .;
        *(.realmode)
        realmode_end = .;
    }
    realmode_size = realmode_end - realmode_start;
}
