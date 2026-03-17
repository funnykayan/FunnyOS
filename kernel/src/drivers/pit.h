#pragma once
#include <stdint.h>

/* Initialise the 8253/8254 PIT at the requested Hz (typically 100-1000). */
void     pit_init(uint32_t hz);

/* Monotonic tick counter (incremented by IRQ0 every 1/hz seconds). */
uint64_t pit_ticks(void);

/* Milliseconds elapsed since pit_init(). */
uint64_t pit_ms(void);

/* Seconds elapsed since pit_init(). */
uint64_t pit_uptime(void);

/* Busy-wait for ms milliseconds (requires interrupts enabled). */
void     pit_sleep_ms(uint32_t ms);
