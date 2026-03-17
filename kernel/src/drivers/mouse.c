/*
 * mouse.c – PS/2 auxiliary device (mouse) driver for FunnyOS
 *
 * Implements:
 *   • PS/2 controller init (enable aux port, IRQ12, streaming mode)
 *   • IRQ12 handler parsing standard 3-byte PS/2 packets
 *   • Software arrow cursor: saves pixels beneath it, draws on move,
 *     restores on erase — so the terminal underneath is never damaged.
 */
#include "mouse.h"
#include "../cpu/cpu.h"
#include "../terminal/terminal.h"
#include <stdint.h>

/* ── I/O helpers (same as keyboard.c; no header sharing needed) ───────────── */
#define PS2_DATA   0x60
#define PS2_STATUS 0x64   /* read  = status */
#define PS2_CMD    0x64   /* write = command */

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(port));
}

/* wait until the PS/2 input buffer is empty (safe to write) */
static inline void ps2_wait_write(void) {
    int timeout = 100000;
    while ((inb(PS2_STATUS) & 0x02) && --timeout);
}
/* wait until the PS/2 output buffer has data (safe to read) */
static inline void ps2_wait_read(void) {
    int timeout = 100000;
    while (!(inb(PS2_STATUS) & 0x01) && --timeout);
}

/* Send a byte to the mouse (via PS/2 "write to auxiliary device" command) */
static void mouse_write(uint8_t val) {
    ps2_wait_write();
    outb(PS2_CMD, 0xD4);   /* next byte goes to the aux port */
    ps2_wait_write();
    outb(PS2_DATA, val);
}

/* Read one byte back from the device and discard it (usually an ACK 0xFA) */
static void mouse_read_discard(void) {
    ps2_wait_read();
    inb(PS2_DATA);
}

/* ── Arrow cursor bitmap ──────────────────────────────────────────────────── */
/*
 * 10 pixels wide × 13 pixels tall.  Each row is a uint16_t bitmask where
 * bit 15 = leftmost pixel (column 0).
 *
 * Visual layout (B = black border, W = white fill, . = transparent):
 *
 *   Col:  0 1 2 3 4 5 6 7 8 9
 *   Row0: B . . . . . . . . .
 *   Row1: B B . . . . . . . .
 *   Row2: B W B . . . . . . .
 *   Row3: B W W B . . . . . .
 *   Row4: B W W W B . . . . .
 *   Row5: B W W W W B . . . .
 *   Row6: B W W W W W B . . .
 *   Row7: B W W W W W W B . .
 *   Row8: B W W W W B B . . .
 *   Row9: B W W B B W B . . .
 *   RowA: B B B . B W W B . .
 *   RowB: . . . . B W W B . .
 *   RowC: . . . . . B B . . .
 */
#define CUR_W  10
#define CUR_H  13

/* bit 15 = col 0 */
static const uint16_t CUR_BORDER[CUR_H] = {
    0x8000, /* row 0 */
    0xC000, /* row 1 */
    0xA000, /* row 2 */
    0x9000, /* row 3 */
    0x8800, /* row 4 */
    0x8400, /* row 5 */
    0x8200, /* row 6 */
    0x8100, /* row 7 */
    0x8600, /* row 8 */
    0x9A00, /* row 9 */
    0xE900, /* row A */
    0x0900, /* row B */
    0x0600, /* row C */
};
static const uint16_t CUR_FILL[CUR_H] = {
    0x0000, /* row 0 */
    0x0000, /* row 1 */
    0x4000, /* row 2 */
    0x6000, /* row 3 */
    0x7000, /* row 4 */
    0x7800, /* row 5 */
    0x7C00, /* row 6 */
    0x7E00, /* row 7 */
    0x7800, /* row 8 */
    0x6400, /* row 9 */
    0x0600, /* row A */
    0x0600, /* row B */
    0x0000, /* row C */
};

/* ── Cursor state ─────────────────────────────────────────────────────────── */
static int      cur_x = 0, cur_y = 0;
static int      cur_drawn = 0;
static uint32_t cur_save[CUR_W * CUR_H]; /* pixels saved from framebuffer */

/* Remove the cursor by restoring the pixels saved underneath it */
static void cursor_erase(void) {
    if (!cur_drawn) return;
    for (int y = 0; y < CUR_H; y++)
        for (int x = 0; x < CUR_W; x++)
            fb_put_pixel(cur_x + x, cur_y + y, cur_save[y * CUR_W + x]);
    cur_drawn = 0;
}

/* Save pixels under the cursor then paint the arrow */
static void cursor_draw(void) {
    int sw = fb_get_width(), sh = fb_get_height();
    for (int y = 0; y < CUR_H; y++) {
        uint16_t brow = CUR_BORDER[y];
        uint16_t frow = CUR_FILL[y];
        for (int x = 0; x < CUR_W; x++) {
            int px = cur_x + x, py = cur_y + y;
            /* always save, even transparent pixels so erase is clean */
            cur_save[y * CUR_W + x] =
                (px < sw && py < sh) ? fb_get_pixel(px, py) : 0;
            if (px >= sw || py >= sh) continue;
            uint16_t bit = (uint16_t)(0x8000u >> x);
            if      (brow & bit) fb_put_pixel(px, py, 0x000000); /* border */
            else if (frow & bit) fb_put_pixel(px, py, 0xFFFFFF); /* fill   */
            /* transparent: leave screen pixel alone */
        }
    }
    cur_drawn = 1;
}

/* ── Mouse state ──────────────────────────────────────────────────────────── */
static volatile int mouse_buttons      = 0;
static volatile int mouse_prev_buttons = 0;

/* Optional callback: called between cursor_erase and cursor_draw. */
static void (*mouse_cb)(int, int, int, int) = 0;

/* ── 3-byte packet assembler ──────────────────────────────────────────────── */
static volatile uint8_t pkt[3];
static volatile int     pkt_phase = 0;

static void mouse_irq_handler(registers_t *regs) {
    (void)regs;
    uint8_t byte = inb(PS2_DATA);

    /*
     * Re-synchronise: the first byte of every packet must have bit 3 set.
     * If we're out of sync, wait until we see such a byte.
     */
    if (pkt_phase == 0 && !(byte & 0x08)) return;

    pkt[pkt_phase++] = byte;
    if (pkt_phase < 3) return;   /* need all 3 bytes before processing */
    pkt_phase = 0;

    /* Overflow bits — discard malformed packet */
    if (pkt[0] & 0xC0) return;

    /* Decode signed deltas (9-bit 2's complement via sign bits in byte 0) */
    int dx = (int)pkt[1] - ((pkt[0] & 0x10) ? 256 : 0);
    int dy = (int)pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);
    dy = -dy;                    /* PS/2 Y axis is inverted (up = positive) */

    mouse_prev_buttons = mouse_buttons;
    mouse_buttons = pkt[0] & 0x07;

    /* Move cursor */
    cursor_erase();
    cur_x += dx;
    cur_y += dy;

    /* Clamp to screen */
    if (cur_x < 0) cur_x = 0;
    if (cur_y < 0) cur_y = 0;
    int sw = fb_get_width(), sh = fb_get_height();
    if (cur_x > sw - CUR_W)  cur_x = sw - CUR_W;
    if (cur_y > sh - CUR_H)  cur_y = sh - CUR_H;

    /* Let the WM handle the event (may redraw windows) */
    if (mouse_cb)
        mouse_cb(cur_x, cur_y, mouse_buttons, mouse_prev_buttons);

    cursor_draw();
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void mouse_init(void) {
    /* 1. Enable the PS/2 auxiliary device port */
    ps2_wait_write();
    outb(PS2_CMD, 0xA8);

    /* 2. Read the controller config byte, enable IRQ12, clear clock-disable */
    ps2_wait_write();
    outb(PS2_CMD, 0x20);
    ps2_wait_read();
    uint8_t cfg = inb(PS2_DATA);
    cfg |=  0x02;   /* bit 1 = enable IRQ12 */
    cfg &= ~0x20;   /* bit 5 = disable aux clock – clear it to enable clock */
    ps2_wait_write();
    outb(PS2_CMD, 0x60);
    ps2_wait_write();
    outb(PS2_DATA, cfg);

    /* 3. Tell mouse to use default settings */
    mouse_write(0xF6);   /* Set Defaults */
    mouse_read_discard();

    /* 4. Enable streaming (data reports on movement) */
    mouse_write(0xF4);   /* Enable Data Reporting */
    mouse_read_discard();

    /* 5. Register IRQ12 handler */
    irq_register(12, mouse_irq_handler);

    /* 6. Place cursor at screen centre and draw it */
    cur_x = fb_get_width()  / 2;
    cur_y = fb_get_height() / 2;
    cursor_draw();
}

void mouse_get_state(int *x, int *y, int *buttons) {
    if (x)       *x       = cur_x;
    if (y)       *y       = cur_y;
    if (buttons) *buttons = mouse_buttons;
}

void mouse_set_callback(void (*cb)(int, int, int, int)) {
    mouse_cb = cb;
}
