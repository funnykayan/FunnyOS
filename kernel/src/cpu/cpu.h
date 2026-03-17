#pragma once
#include <stdint.h>

/* ── GDT ───────────────────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;  /* limit_high (4 bits) | flags (4 bits) */
    uint8_t  base_high;
} gdt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t     limit;
    uint64_t     base;
} gdt_ptr_t;

void gdt_init(void);

/* ── IDT ───────────────────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;          /* Interrupt Stack Table offset */
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idt_ptr_t;

/* Saved register state pushed by the ISR stubs */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    /* pushed by CPU automatically */
    uint64_t rip, cs, rflags, rsp, ss;
} registers_t;

void idt_init(void);
void isr_handler(registers_t *regs);
void irq_handler(registers_t *regs);
void irq_register(int irq, void (*handler)(registers_t *));
