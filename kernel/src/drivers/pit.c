/* ============================================================
 *  FunnyOS  –  PIT (8253/8254 Programmable Interval Timer)
 *  IRQ0 is wired to channel 0; we program it for a square-wave
 *  at the requested frequency and use it as a system tick.
 * ============================================================ */

#include "pit.h"
#include "../cpu/cpu.h"
#include <stdint.h>

/* PIT I/O ports */
#define PIT_CHANNEL0  0x40
#define PIT_CMD       0x43
#define PIT_BASE_HZ   1193182UL   /* ~1.193182 MHz input clock */

static volatile uint64_t pit_ticks_val = 0;
static uint32_t pit_hz_val = 100;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static inline void pit_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

/* ── IRQ0 handler ─────────────────────────────────────────────────────────── */

static void pit_irq_handler(registers_t *regs) {
    (void)regs;
    pit_ticks_val++;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void pit_init(uint32_t hz) {
    if (hz == 0) hz = 100;
    pit_hz_val = hz;
    uint32_t div = (uint32_t)(PIT_BASE_HZ / (uint64_t)hz);

    /* Channel 0, lobyte/hibyte, mode 3 (square wave) */
    pit_outb(PIT_CMD,      0x36);
    pit_outb(PIT_CHANNEL0, (uint8_t)(div & 0xFF));
    pit_outb(PIT_CHANNEL0, (uint8_t)((div >> 8) & 0xFF));

    irq_register(0, pit_irq_handler);
}

uint64_t pit_ticks(void)  { return pit_ticks_val; }

uint64_t pit_ms(void) {
    return pit_ticks_val * 1000ULL / pit_hz_val;
}

uint64_t pit_uptime(void) {
    return pit_ticks_val / pit_hz_val;
}

void pit_sleep_ms(uint32_t ms) {
    uint64_t target = pit_ticks_val + (uint64_t)ms * pit_hz_val / 1000 + 1;
    while (pit_ticks_val < target)
        __asm__ volatile ("hlt");
}
