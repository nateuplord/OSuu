global loader       ; the entry symbol for ELF
global load_gdt     ; load GDT table
extern kernel_setup ; kernel

KERNEL_STACK_SIZE equ 2097152                  ; size of stack in bytes

section .bss
align 4                              ; align at 4 bytes
kernel_stack:                        ; label points to beginning of memory
    resb KERNEL_STACK_SIZE           ; reserve stack for the kernel

MAGIC2    equ 0xE85250D6                                   ; Multiboot2 magic
ARCH      equ 0                                            ; 0 = i386 protected mode
CHECKSUM2 equ -(MAGIC2 + ARCH + (header_end - header_start))

section .multiboot                   ; GNU GRUB Multiboot2 header
align 8
header_start:
    dd MAGIC2
    dd ARCH
    dd header_end - header_start     ; header_length
    dd CHECKSUM2

    ; terminating tag
    align 8
    dw 0
    dw 0
    dd 8
header_end:


section .text                                  ; start of the text (code)
loader:                                        ; the loader label (defined as entry point in linker script)
    mov  esp, kernel_stack + KERNEL_STACK_SIZE ; setup stack register to proper location
    push ebx                                   ; for multiboot info
    call kernel_setup
.loop:
    jmp .loop                                  ; loop forever


; More details: https://en.wikibooks.org/wiki/X86_Assembly/Protected_Mode
load_gdt:
    cli
    mov  eax, [esp+4]
    lgdt [eax] ; Load GDT from GDTDescriptor, eax at this line will point GDTR location

    ; Set bit-0 (Protection Enable bit-flag) in Control Register 0 (CR0)
    ; This is optional, as usually GRUB already start with protected mode flag enabled
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax

    ; Far jump to update cs register
    ; Warning: Invalid GDT will raise exception in following instruction below
    jmp 0x8:flush_cs
flush_cs:
    mov ax, 10h ; Update all segment register
    mov ss, ax
    mov ds, ax
    mov es, ax
    ret
