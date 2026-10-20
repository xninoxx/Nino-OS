; gdt_flush.asm - load the new GDT and reload all segment registers
bits 32

section .text
global gdt_flush

gdt_flush:
    mov  eax, [esp + 4]      ; pointer to the GDT descriptor
    lgdt [eax]
    mov  ax, 0x10            ; kernel data selector
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    jmp  0x08:.reload_cs     ; far jump reloads CS with the kernel code selector
.reload_cs:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits