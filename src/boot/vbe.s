[bits 32]
section .text
global vbe_set_mode

extern realmode_start
extern realmode_size
extern realmode_lma

; bool vbe_set_mode(uint16_t mode, struct VbeModeInfo *out)
;
; The real work happens in the .realmode section below, which the linker
; script places at VMA 0x9000 (so its own jumps/data references resolve to
; small, 16-bit-representable offsets) while keeping its bytes stored
; sequentially in the file at realmode_lma. This function copies it from
; realmode_lma to realmode_start (0x9000) at runtime, jumps there, and reads
; the results back once it returns.
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

; ===================== real-mode trampoline =====================
; Linked at VMA 0x9000 (see linker.ld), physically run from there after
; vbe_set_mode copies it down. vbe_trampoline: is this section's own first
; label (not relying on realmode_start lining up byte-for-byte), and acts
; as the local-label scope anchor for .fail/.back_to_pm below - this
; section has no enclosing function label the way vbe_set_mode did above.
section .realmode
bits 16

vbe_trampoline:
    mov ax, 0x20                    ; GDT_16BIT_DATA_SEGMENT_SELECTOR
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov eax, cr0
    and eax, 0xFFFFFFFE              ; clear PE
    mov cr0, eax

    jmp 0x0000:.enter_realmode        ; flush prefetch queue, CS becomes a real-mode segment

.enter_realmode:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00                    ; scratch real-mode stack

    lidt [.idt_ptr]                    ; real-mode IVT: base 0, limit 0x3FF
    ; deliberately staying cli'd here - the PIC is already remapped to
    ; vectors 0x20+ by this point (pic_remap, called earlier in
    ; kernel_setup), so a hardware IRQ firing against this real-mode IVT
    ; would jump to a bogus/unmapped entry and likely triple-fault. The
    ; VBE calls below are simple synchronous BIOS calls that don't need
    ; interrupts enabled.

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
    or eax, 1                            ; set PE
    mov cr0, eax

    ; A memory-based far jump (e.g. "jmp dword far [esp]") would need to
    ; read its far pointer via ss (implicitly, through esp) - but ss is
    ; still 0 here, and the instant PE is set, 0 means the null descriptor,
    ; not "a valid real-mode segment" anymore. Any such memory access
    ; faults, and with no protected-mode IDT loaded yet, that cascades into
    ; a triple fault. So: use an IMMEDIATE far jump instead (no memory
    ; access, no segment dependency at all) to a label in this same
    ; low-address section, reload segments safely there (mov ds/es/ss
    ; never touches memory), and finish with a register-indirect jump
    ; (edi already holds the return address - also no memory operand).
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
