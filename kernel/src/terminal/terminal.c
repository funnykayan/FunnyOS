#include "terminal.h"
#include "font.h"
#include "../lib/string.h"
#include <stdint.h>
#include <stddef.h>

/* ── Framebuffer provided by Limine (set up in kernel.c) ──────────────────── */
extern void    *fb_addr;
extern uint64_t fb_width;
extern uint64_t fb_height;
extern uint64_t fb_pitch;         /* bytes per row          */
extern uint8_t  fb_bpp;           /* bits per pixel (32)    */

/* ── Terminal state ───────────────────────────────────────────────────────── */
static int cols, rows;
static int cur_col, cur_row;

static uint32_t fg_color = COLOR_GREEN;
static uint32_t bg_color = COLOR_BLACK;

/* ── Back-buffer + viewport ───────────────────────────────────────────────── */
/*
 * One uint32_t per pixel; stride is always TERM_BUF_MAX_W.
 * Sits in .bss (zeroed by the bootloader): 1024×800×4 ≈ 3.1 MiB.
 */
#define TERM_BUF_MAX_W  1024
#define TERM_BUF_MAX_H   800
static uint32_t term_backbuf[TERM_BUF_MAX_W * TERM_BUF_MAX_H];

static int vp_x = 0, vp_y = 0;   /* top-left in framebuffer (pixels) */
static int vp_w = 0, vp_h = 0;   /* viewport size in pixels           */

/* ── Internal helpers ─────────────────────────────────────────────────────── */

void fb_put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint64_t)x >= fb_width || (uint64_t)y >= fb_height) return;
    uint32_t *row = (uint32_t *)((uint8_t *)fb_addr + (uint64_t)y * fb_pitch);
    row[x] = color;
}

uint32_t fb_get_pixel(int x, int y) {
    if (x < 0 || y < 0 || (uint64_t)x >= fb_width || (uint64_t)y >= fb_height) return 0;
    uint32_t *row = (uint32_t *)((uint8_t *)fb_addr + y * fb_pitch);
    return row[x];
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            fb_put_pixel(x + dx, y + dy, color);
}

int fb_get_width(void)  { return (int)fb_width; }
int fb_get_height(void) { return (int)fb_height; }

/* ── Viewport-local pixel write (private) ────────────────────────────────── */
static inline void vp_put(int lx, int ly, uint32_t color) {
    if (lx < 0 || ly < 0 || lx >= vp_w || ly >= vp_h) return;
    fb_put_pixel(vp_x + lx, vp_y + ly, color);
    term_backbuf[ly * TERM_BUF_MAX_W + lx] = color;
}

/* ── Back-buffer blit to framebuffer (public, called by WM repaint) ─────── */
void term_blit_to_fb(void) {
    for (int y = 0; y < vp_h; y++)
        for (int x = 0; x < vp_w; x++)
            fb_put_pixel(vp_x + x, vp_y + y,
                         term_backbuf[y * TERM_BUF_MAX_W + x]);
}

static void draw_glyph(int col, int row, char c, uint32_t fg, uint32_t bg) {
    int lx = col * FONT_WIDTH;
    int ly = row * FONT_HEIGHT;
    const uint8_t *glyph = font8x16[(uint8_t)c < 128 ? (uint8_t)c : 0];
    for (int y = 0; y < FONT_HEIGHT; y++) {
        uint8_t bits = glyph[y];
        for (int x = 0; x < FONT_WIDTH; x++)
            vp_put(lx + x, ly + y, (bits & (0x80 >> x)) ? fg : bg);
    }
}

static void clear_row(int row) {
    int ly = row * FONT_HEIGHT;
    for (int y = 0; y < FONT_HEIGHT; y++)
        for (int x = 0; x < vp_w; x++)
            vp_put(x, ly + y, bg_color);
}

/* Scroll one text row up using the back-buffer */
static void scroll(void) {
    for (int py = FONT_HEIGHT; py < vp_h; py++)
        kmemcpy(&term_backbuf[(py - FONT_HEIGHT) * TERM_BUF_MAX_W],
                &term_backbuf[py               * TERM_BUF_MAX_W],
                (size_t)vp_w * sizeof(uint32_t));
    /* clear last text row in back-buffer */
    for (int py = vp_h - FONT_HEIGHT; py < vp_h; py++)
        for (int px = 0; px < vp_w; px++)
            term_backbuf[py * TERM_BUF_MAX_W + px] = bg_color;
    term_blit_to_fb();
    cur_row = rows - 1;
    cur_col = 0;
}

/* Draw a blinking-style cursor block at current position */
static void draw_cursor(int visible) {
    int lx = cur_col * FONT_WIDTH;
    int ly = (cur_row + 1) * FONT_HEIGHT - 2;   /* bottom 2 scanlines */
    uint32_t color = visible ? fg_color : bg_color;
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < FONT_WIDTH; x++)
            vp_put(lx + x, ly + y, color);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void term_init(void) {
    int w = (int)fb_width  < TERM_BUF_MAX_W ? (int)fb_width  : TERM_BUF_MAX_W;
    int h = (int)fb_height < TERM_BUF_MAX_H ? (int)fb_height : TERM_BUF_MAX_H;
    vp_x = 0;  vp_y = 0;
    vp_w = w;  vp_h = h;
    cols = w / FONT_WIDTH;
    rows = h / FONT_HEIGHT;
    cur_col = 0;
    cur_row = 0;
    term_clear();
}

void term_clear(void) {
    for (int py = 0; py < vp_h; py++) {
        for (int px = 0; px < vp_w; px++) {
            term_backbuf[py * TERM_BUF_MAX_W + px] = bg_color;
            fb_put_pixel(vp_x + px, vp_y + py, bg_color);
        }
    }
    cur_col = 0;
    cur_row = 0;
}

void term_set_viewport(int x, int y, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > TERM_BUF_MAX_W) w = TERM_BUF_MAX_W;
    if (h > TERM_BUF_MAX_H) h = TERM_BUF_MAX_H;
    vp_x = x;  vp_y = y;
    vp_w = w;  vp_h = h;
    cols = w / FONT_WIDTH;
    rows = h / FONT_HEIGHT;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    cur_col = 0;
    cur_row = 0;
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            term_backbuf[py * TERM_BUF_MAX_W + px] = bg_color;
            fb_put_pixel(x + px, y + py, bg_color);
        }
    }
}

void term_set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

void term_putchar(char c) {
    switch (c) {
    case '\n':
        draw_cursor(0);
        cur_col = 0;
        cur_row++;
        if (cur_row >= rows) scroll();
        else draw_cursor(1);
        return;

    case '\r':
        draw_cursor(0);
        cur_col = 0;
        draw_cursor(1);
        return;

    case '\t':
        /* Align to next 8-column tab stop */
        draw_cursor(0);
        cur_col = (cur_col + 8) & ~7;
        if (cur_col >= cols) {
            cur_col = 0;
            cur_row++;
            if (cur_row >= rows) scroll();
        }
        draw_cursor(1);
        return;

    case '\b':
        draw_cursor(0);
        if (cur_col > 0) {
            cur_col--;
        } else if (cur_row > 0) {
            cur_row--;
            cur_col = cols - 1;
        }
        draw_glyph(cur_col, cur_row, ' ', fg_color, bg_color);
        draw_cursor(1);
        return;

    default:
        if ((unsigned char)c < 32) return;  /* skip other control codes */
        draw_cursor(0);
        draw_glyph(cur_col, cur_row, c, fg_color, bg_color);
        cur_col++;
        if (cur_col >= cols) {
            cur_col = 0;
            cur_row++;
            if (cur_row >= rows) scroll();
        }
        draw_cursor(1);
        return;
    }
}
