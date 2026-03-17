; ============================================================
;  FunnyOS  –  Kernel Entry Point
;  Limine passes control here in 64-bit long mode.
;  We set up a small bootstrap stack then call kmain().
; ============================================================

bits 64
default rel

global _start
extern kmain

section .bss
align 16
stack_bottom:
    resb 65536          ; 64 KiB bootstrap stack
stack_top:

section .text
_start:
    ; ── Point RSP at our bootstrap stack ──────────────────
    lea  rsp, [stack_top]

    ; ── Clear RFLAGS (direction flag, interrupts off) ─────
    push 0
    popf

    ; ── Call the C kernel entry ────────────────────────────
    call kmain

    ; ── Should never return – halt forever ────────────────
.hang:
    cli
    hlt
    jmp .hang
