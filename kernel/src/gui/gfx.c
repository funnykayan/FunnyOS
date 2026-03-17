/*
 * gfx.c  –  2D drawing primitives
 * Uses fb_put_pixel / fb_get_pixel from terminal.h and the embedded 8×16 font.
 */
#include "gfx.h"
#include "../terminal/terminal.h"
#include "../terminal/font.h"   /* font8x16, FONT_WIDTH, FONT_HEIGHT */
#include <stdint.h>

/* ── Helpers ──────────────────────────────────────────────────────────────── */
static inline int gfx_abs(int x) { return x < 0 ? -x : x; }
static inline int gfx_min(int a, int b) { return a < b ? a : b; }
static inline int gfx_max(int a, int b) { return a > b ? a : b; }

/* ── Rectangles ───────────────────────────────────────────────────────────── */

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color) {
    fb_fill_rect(x, y, w, h, color);   /* delegate to terminal layer */
}

void gfx_draw_rect(int x, int y, int w, int h, uint32_t color) {
    /* top + bottom */
    for (int i = 0; i < w; i++) {
        fb_put_pixel(x + i, y,         color);
        fb_put_pixel(x + i, y + h - 1, color);
    }
    /* left + right */
    for (int i = 1; i < h - 1; i++) {
        fb_put_pixel(x,         y + i, color);
        fb_put_pixel(x + w - 1, y + i, color);
    }
}

/* ── Lines ────────────────────────────────────────────────────────────────── */

void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx =  gfx_abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -gfx_abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        fb_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ── Circles (Bresenham midpoint) ─────────────────────────────────────────── */

static inline void circle_plot8(int cx, int cy, int x, int y, uint32_t col) {
    fb_put_pixel(cx + x, cy + y, col);
    fb_put_pixel(cx - x, cy + y, col);
    fb_put_pixel(cx + x, cy - y, col);
    fb_put_pixel(cx - x, cy - y, col);
    fb_put_pixel(cx + y, cy + x, col);
    fb_put_pixel(cx - y, cy + x, col);
    fb_put_pixel(cx + y, cy - x, col);
    fb_put_pixel(cx - y, cy - x, col);
}

void gfx_draw_circle(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) { // this just barely works
        circle_plot8(cx, cy, x, y, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_fill_circle(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        /* draw horizontal spans to fill */
        int x1, x2, yy;
        /* upper half */
        yy = cy - y;
        x1 = gfx_max(cx - x, 0); x2 = cx + x;
        for (int i = x1; i <= x2; i++) fb_put_pixel(i, yy, color);
        yy = cy - x;
        x1 = gfx_max(cx - y, 0); x2 = cx + y;
        for (int i = x1; i <= x2; i++) fb_put_pixel(i, yy, color);
        /* lower half */
        yy = cy + y;
        x1 = gfx_max(cx - x, 0); x2 = cx + x;
        for (int i = x1; i <= x2; i++) fb_put_pixel(i, yy, color);
        yy = cy + x;
        x1 = gfx_max(cx - y, 0); x2 = cx + y;
        for (int i = x1; i <= x2; i++) fb_put_pixel(i, yy, color);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* ── Text ─────────────────────────────────────────────────────────────────── */

int gfx_char_w(void) { return FONT_WIDTH;  }
int gfx_char_h(void) { return FONT_HEIGHT; }

void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font8x16[(unsigned char)c < 128 ? (unsigned char)c : 0];
    int transparent = (bg == GFX_TRANSPARENT);
    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (bits & (0x80 >> col))
                fb_put_pixel(x + col, y + row, fg);
            else if (!transparent)
                fb_put_pixel(x + col, y + row, bg);
        }
    }
}

void gfx_draw_text(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    for (; *s; s++, x += FONT_WIDTH)
        gfx_draw_char(x, y, *s, fg, bg);
}

int gfx_text_width(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n * FONT_WIDTH;
}
