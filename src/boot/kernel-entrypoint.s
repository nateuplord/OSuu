global loader                           ; the entry symbol for ELF
global load_gdt                         ; load GDT table
global set_tss_register                 ; set tss register to GDT entry
global kernel_execute_user_program      ; execute initial user program from kernel

extern kernel_setup                     ; kernel C entrypoint
extern _pg_kernel_pg_dir                ; kernel page directory
extern _pg_kernel_pg_table              ; kernel page table

KERNEL_VIRT_BASE equ 0xC0000000         ; kernel virtual memory
KERNEL_STACK_SIZE   equ 2097152         ; size of stack in bytes

; Must match header/memory/paging.h's KERNEL_RESERVED_PAGE_TABLE_COUNT exactly -
; see that header for what this constrains.
KERNEL_RESERVED_PAGE_TABLE_COUNT equ 4

MAGIC2    equ 0xE85250D6                                   ; Multiboot2 magic
ARCH      equ 0                                            ; 0 = i386 protected mode
CHECKSUM2 equ -(MAGIC2 + ARCH + (header_end - header_start))

section .bss
align 4                                 ; align at 4 bytes
kernel_stack:                           ; label points to beginning of memory
    resb KERNEL_STACK_SIZE              ; reserve stack for the kernel

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

; start of the text (code) section
section .setup.text

loader equ loader_entrypoint

; the loader label (defined as entry point in linker script)
loader_entrypoint:
    ; Build the initial 4 KiB identity/high-half mappings before paging is on,
    ; across all KERNEL_RESERVED_PAGE_TABLE_COUNT page tables (4 MiB each).
    mov edi, _pg_kernel_pg_table - KERNEL_VIRT_BASE
    xor eax, eax
    mov ecx, KERNEL_RESERVED_PAGE_TABLE_COUNT * 1024

.fill_kernel_page_table:
    mov edx, eax
    or  edx, 0x3
    mov [edi], edx
    add edi, 4
    add eax, 0x1000
    loop .fill_kernel_page_table

    ; Point each page table's low identity-mapped directory entry and its
    ; high-half mirror (dir[N] / dir[0xC00+N]) at the Nth page table.
    ; esi (not ebx!) is the scratch here - ebx still holds the multiboot2
    ; info pointer GRUB passed in, needed later by loader_virtual.
    ;
    ; user_supervisor=1 here (0x7, not 0x3) despite this being the KERNEL's
    ; own identity map: x86 ANDs the U/S bit from the PDE and PTE together,
    ; so leaving it 0 here would block ring-3 access to EVERY page under
    ; these directory entries regardless of what pg_alloc_user_frame sets at
    ; the per-page level - including user programs later loaded at low
    ; virtual addresses like 0. The individual pages stay supervisor-only by
    ; default anyway, since .fill_kernel_page_table above only sets 0x3 (no
    ; user_supervisor) per PTE; this only stops the directory from
    ; overriding that per-page decision.
    mov esi, _pg_kernel_pg_table - KERNEL_VIRT_BASE
    mov edi, _pg_kernel_pg_dir - KERNEL_VIRT_BASE
    mov ecx, KERNEL_RESERVED_PAGE_TABLE_COUNT

.fill_kernel_page_dir:
    mov eax, esi
    or  eax, 0x7
    mov [edi], eax
    mov [edi + 0xC00], eax

    add esi, 0x1000                     ; next page table (each is 4 KiB)
    add edi, 4                          ; next directory entry
    loop .fill_kernel_page_dir

    ; Recursive mapping at index 1023 (offset 0xFFC) for 0xFFC00000
    mov eax, _pg_kernel_pg_dir - KERNEL_VIRT_BASE
    or  eax, 0x3
    mov [_pg_kernel_pg_dir - KERNEL_VIRT_BASE + 0xFFC], eax

    ; Set CR3 (CPU page register)
    mov eax, _pg_kernel_pg_dir - KERNEL_VIRT_BASE
    mov cr3, eax

    ; Disable 4 MiB pages
    ; all mappings use 4 KiB page tables.
    mov eax, cr4
    and eax, 0xFFFFFFEF
    mov cr4, eax

    ; Enable paging
    mov eax, cr0

    ; PG flag
    or  eax, 0x80000000

    mov cr0, eax

    ; Jump into higher half first,
    ; cannot use C because call stack is still not working
    lea eax, [loader_virtual]
    jmp eax

section .text

loader_virtual:
    mov esp, kernel_stack + KERNEL_STACK_SIZE       ; Setup stack register to proper location
    push ebx                                        ; multiboot2 info pointer (unused for now)
    call kernel_setup

.loop:
    jmp .loop                                       ; loop forever

; More details: https://en.wikibooks.org/wiki/X86_Assembly/Protected_Mode

section .text

load_gdt:
    cli
    mov  eax, [esp+4]

    ; Load GDT from GDTDescriptor,
    ; eax at this line will point GDTR location
    lgdt [eax]

    ; Set bit-0 (Protection Enable bit-flag) in Control Register 0 (CR0)
    ; This is optional, as usually GRUB already start with protected mode flag enabled
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax

    ; Far jump to update cs register
    ; Warning: Invalid GDT will raise exception in any instruction below
    jmp 0x8:flush_cs

flush_cs:
    ; Update all segment register
    mov ax, 10h
    mov ss, ax
    mov ds, ax
    mov es, ax
    ret

set_tss_register:
    mov ax, 0x28 | 0 ; GDT TSS Selector, ring 0
    ltr ax
    ret

kernel_execute_user_program:
    mov eax, [esp + 4]      ; entrypoint (0x0)
    mov bx,  0x20 | 0x3     ; user data selector (0x23)
    mov ds,  bx
    mov es,  bx
    mov fs,  bx
    mov gs,  bx

    push 0x20 | 0x3          ; SS
    push 0xBFFFFFFC          ; ESP (top of mapped 0xBFFFF000 stack page)
    pushf
    pop  ebx
    or   ebx, 0x200          ; Ensure IF (Interrupt Enable) is set
    push ebx                 ; EFLAGS
    push 0x18 | 0x3          ; CS (User Code selector 0x1B)
    push eax                 ; EIP (0x0)
    iret
