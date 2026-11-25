ENTRY(_start)

SECTIONS {
    . = 0x00000000; /* User programs are loaded at virtual address 0 */

    .text ALIGN(4): {
        [a-z\/]*crt0.o(.text)  /* Put the entrypoint in front of the executable */
        *(.text*)
    }

    .rodata ALIGN(4): {
        *(.rodata*)
    }

    .data ALIGN(4): {
        *(.data*)
    }

    .bss ALIGN(4): {
        *(COMMON)
        *(.bss*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }

    _linker_user_program_end = .;
    ASSERT ((_linker_user_program_end <= 1 * 1024 * 1024), "Error: User program linking result is more than 1 MiB")
}
