[bits 32]
section .text
global vbe_set_mode

extern realmode_start
extern realmode_size
extern realmode_lma

vbe_set_mode:
    push ebp
    mov ebp, esp
    pushad
    push ds
    push es
    push fs
    push gs

    mov esi, realmode_lma
    mov edi, realmode_start
    mov ecx, realmode_size
    rep movsb

    mov [vbe_saved_esp], esp
    sidt [vbe_saved_idtr]
    mov eax, [ebp+12]
    mov [vbe_saved_out], eax       ; out pointer, survives the trampoline round-trip here

    movzx ecx, word [ebp+8]        ; mode number, passed to the trampoline in cx
    mov edi, .trampoline_returned  ; return address, passed to the trampoline in edi

    cli
    jmp 0x18:realmode_start         ; GDT_16BIT_CODE_SEGMENT_SELECTOR

.trampoline_returned:
    ; back in our own protected mode here, landed via the trampoline's far jump
    mov ax, 0x10                    ; GDT_KERNEL_DATA_SEGMENT_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov esp, [vbe_saved_esp]
    lidt [vbe_saved_idtr]
    sti                             ; re-enable interrupts - cli'd above before the trampoline jump, never undone until now

    pop gs
    pop fs
    pop es
    pop ds
    popad

    movzx eax, byte [0x7FFF]        ; result flag the trampoline wrote
    test eax, eax
    jz .return

    mov edi, [vbe_saved_out]
    mov esi, 0x8000                  ; VBE Mode Info Block the trampoline filled

    mov ecx, [esi+40]                ; LFBAddress
    mov [edi+0], ecx                 ; -> framebuffer_addr

    movzx ecx, word [esi+16]         ; BytesPerScanLine
    mov [edi+4], ecx                 ; -> pitch

    movzx ecx, word [esi+18]         ; Width
    mov [edi+8], ecx                 ; -> width

    movzx ecx, word [esi+20]         ; Height
    mov [edi+12], ecx                ; -> height

    movzx ecx, byte [esi+25]         ; BitsPerPixel
    mov [edi+16], ecx                ; -> bpp

    mov eax, 1
.return:
    pop ebp
    ret

section .data
vbe_saved_esp:  dd 0
vbe_saved_out:  dd 0
vbe_saved_idtr: dw 0
                dd 0

section .realmode
bits 16

vbe_trampoline:
    mov ax, 0x20                    ; GDT_16BIT_DATA_SEGMENT_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov eax, cr0
    and eax, 0x7FFFFFFE              ; clear PE and PG - real mode can't have paging active
    mov cr0, eax

    jmp 0x0000:.enter_realmode        ; flush prefetch queue, CS becomes a real-mode segment

.enter_realmode:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00                    ; scratch real-mode stack

    lidt [.idt_ptr]                    ; real-mode IVT: base 0, limit 0x3FF
    mov [.mode_number], cx             ; stash inputs (cx/edi are about to be reused)
    mov [.return_addr], edi

    mov cx, [.mode_number]
    mov di, 0x8000                      ; VBE Mode Info Block buffer
    mov ax, 0x4F01                      ; Get VBE Mode Info
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov bx, [.mode_number]
    or bx, 0x4000                        ; request linear framebuffer
    mov ax, 0x4F02                       ; Set VBE Mode
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov byte [0x7FFF], 1
    jmp .back_to_pm

.fail:
    mov byte [0x7FFF], 0

.back_to_pm:
    cli
    mov eax, cr0
    or eax, 0x80000001                   ; set PE and PG - CR3 was never touched, so
    mov cr0, eax                         ; the same higher-half mappings come right back

    jmp 0x08:.finish_32bit                ; GDT_KERNEL_CODE_SEGMENT_SELECTOR

[bits 32]
.finish_32bit:
    mov ax, 0x10                          ; GDT_KERNEL_DATA_SEGMENT_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; edi itself was clobbered earlier (reused as di for the VBE buffer
    ; pointer) - reload the real return address from where it was saved
    mov edi, [.return_addr]
    jmp edi

[bits 16]
.idt_ptr:
    dw 0x3FF
    dd 0x00000000

.mode_number: dw 0
.return_addr: dd 0
