; isr.asm - interrupt service routine stubs for NinoKernel
; Vectors 0-31: CPU exceptions, 32-47: hardware IRQs 0-15 (after PIC remap)
bits 32

extern isr_handler

; Exception without a CPU-pushed error code: push a dummy 0 for a uniform stack frame
%macro ISR_NOERR 1
isr_stub_%1:
    push dword 0
    push dword %1
    jmp  isr_common
%endmacro

; Exception where the CPU already pushed an error code
%macro ISR_ERR 1
isr_stub_%1:
    push dword %1
    jmp  isr_common
%endmacro

section .text

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31
; Hardware IRQs 0-15 -> vectors 32-47 (no error code)
ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47

; Common stub: save state, call the C handler, restore state.
; Stack layout seen by C (struct registers): edi..eax (pusha), int_no, err_code, eip, cs, eflags
isr_common:
    pusha                    ; eax, ecx, edx, ebx, esp, ebp, esi, edi
    cld
    push esp                 ; argument: pointer to struct registers
    call isr_handler
    add  esp, 4
    popa
    add  esp, 8              ; drop int_no and err_code
    iretd

; Table of stub addresses, used by idt_init() in C
section .data
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dd isr_stub_%+i
%assign i i+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits