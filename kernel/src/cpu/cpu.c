#include "cpu.h"
#include <stdint.h>

/* ── I/O port helpers ──────────────────────────────────────────────────────── */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  GDT                                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

extern void gdt_flush(gdt_ptr_t *ptr);

#define GDT_ENTRIES 5
static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;

static void gdt_set(int i, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t gran) {
    gdt[i].base_low   = (base & 0xFFFF);
    gdt[i].base_mid   = (base >> 16) & 0xFF;
    gdt[i].base_high  = (base >> 24) & 0xFF;
    gdt[i].limit_low  = (limit & 0xFFFF);
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access      = access;
}

void gdt_init(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;

    gdt_set(0, 0,          0,          0x00, 0x00); /* null descriptor     */
    gdt_set(1, 0, 0xFFFFFFFF,          0x9A, 0xA0); /* kernel code 64-bit  */
    gdt_set(2, 0, 0xFFFFFFFF,          0x92, 0xC0); /* kernel data         */
    gdt_set(3, 0, 0xFFFFFFFF,          0xFA, 0xA0); /* user code  64-bit   */
    gdt_set(4, 0, 0xFFFFFFFF,          0xF2, 0xC0); /* user data           */

    gdt_flush(&gdt_ptr);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  IDT                                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

extern void idt_flush(idt_ptr_t *ptr);

/* ISR stubs declared in cpu_asm.asm */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

#define IDT_ENTRIES 256
static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;

static void (*irq_handlers[16])(registers_t *) = {0};

static void idt_set(int i, void (*handler)(void), uint8_t type_attr) {
    uintptr_t addr      = (uintptr_t)handler;
    idt[i].offset_low   = addr & 0xFFFF;
    idt[i].selector     = 0x08;          /* kernel code selector */
    idt[i].ist          = 0;
    idt[i].type_attr    = type_attr;
    idt[i].offset_mid   = (addr >> 16) & 0xFFFF;
    idt[i].offset_high  = (addr >> 32) & 0xFFFFFFFF;
    idt[i].zero         = 0;
}

#define TRAP_GATE  0x8F
#define INT_GATE   0x8E

void idt_init(void) {
    idt_ptr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idt_ptr.base  = (uint64_t)&idt;

    /* Exceptions */
    idt_set( 0, isr0,  TRAP_GATE); idt_set( 1, isr1,  TRAP_GATE);
    idt_set( 2, isr2,  TRAP_GATE); idt_set( 3, isr3,  TRAP_GATE);
    idt_set( 4, isr4,  TRAP_GATE); idt_set( 5, isr5,  TRAP_GATE);
    idt_set( 6, isr6,  TRAP_GATE); idt_set( 7, isr7,  TRAP_GATE);
    idt_set( 8, isr8,  TRAP_GATE); idt_set( 9, isr9,  TRAP_GATE);
    idt_set(10, isr10, TRAP_GATE); idt_set(11, isr11, TRAP_GATE);
    idt_set(12, isr12, TRAP_GATE); idt_set(13, isr13, TRAP_GATE);
    idt_set(14, isr14, TRAP_GATE); idt_set(15, isr15, TRAP_GATE);
    idt_set(16, isr16, TRAP_GATE); idt_set(17, isr17, TRAP_GATE);
    idt_set(18, isr18, TRAP_GATE); idt_set(19, isr19, TRAP_GATE);
    idt_set(20, isr20, TRAP_GATE); idt_set(21, isr21, TRAP_GATE);
    idt_set(22, isr22, TRAP_GATE); idt_set(23, isr23, TRAP_GATE);
    idt_set(24, isr24, TRAP_GATE); idt_set(25, isr25, TRAP_GATE);
    idt_set(26, isr26, TRAP_GATE); idt_set(27, isr27, TRAP_GATE);
    idt_set(28, isr28, TRAP_GATE); idt_set(29, isr29, TRAP_GATE);
    idt_set(30, isr30, TRAP_GATE); idt_set(31, isr31, TRAP_GATE);

    /* Remap PIC: IRQ 0-15 → vectors 32-47 */
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0x00); outb(0xA1, 0x00); /* unmask all */

    idt_set(32, irq0,  INT_GATE); idt_set(33, irq1,  INT_GATE);
    idt_set(34, irq2,  INT_GATE); idt_set(35, irq3,  INT_GATE);
    idt_set(36, irq4,  INT_GATE); idt_set(37, irq5,  INT_GATE);
    idt_set(38, irq6,  INT_GATE); idt_set(39, irq7,  INT_GATE);
    idt_set(40, irq8,  INT_GATE); idt_set(41, irq9,  INT_GATE);
    idt_set(42, irq10, INT_GATE); idt_set(43, irq11, INT_GATE);
    idt_set(44, irq12, INT_GATE); idt_set(45, irq13, INT_GATE);
    idt_set(46, irq14, INT_GATE); idt_set(47, irq15, INT_GATE);

    idt_flush(&idt_ptr);
    __asm__ volatile ("sti");
}

static const char *exception_names[] = {
    "Division By Zero",      "Debug",                   "NMI",
    "Breakpoint",            "Into Overflow",           "Out of Bounds",
    "Invalid Opcode",        "No FPU",                  "Double Fault",
    "FPU Segment Overrun",   "Bad TSS",                 "Segment Not Present",
    "Stack Fault",           "General Protection Fault","Page Fault",
    "Unknown",               "FPU Fault",               "Alignment Check",
    "Machine Check",         "SIMD Fault",
};

void isr_handler(registers_t *regs) {
    /* For now just freeze — a proper kernel would print & panic */
    (void)regs;
    __asm__ volatile ("cli; hlt");
}

void irq_handler(registers_t *regs) {
    int irq = (int)regs->int_no - 32;
    if (irq >= 0 && irq < 16 && irq_handlers[irq])
        irq_handlers[irq](regs);

    /* Send EOI */
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void irq_register(int irq, void (*handler)(registers_t *)) {
    if (irq >= 0 && irq < 16)
        irq_handlers[irq] = handler;
}
