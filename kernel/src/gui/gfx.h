#pragma once
#include <stdint.h>

/*
 * gfx.h  –  2D drawing primitives for FunnyOS GUI
 *
 * All functions draw directly to the Limine framebuffer via fb_put_pixel.
 * Coordinates are in pixels, (0,0) = top-left.
 */

/* ── Filled / outline rectangles ─────────────────────────────────────────── */
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_draw_rect(int x, int y, int w, int h, uint32_t color);

/* ── Lines and circles ────────────────────────────────────────────────────── */
void gfx_draw_line  (int x0, int y0, int x1, int y1, uint32_t color);
void gfx_draw_circle(int cx, int cy, int r, uint32_t color);
void gfx_fill_circle(int cx, int cy, int r, uint32_t color);

/* ── Text (8×16 VGA bitmap font) ─────────────────────────────────────────── */
/*  Pass bg = 0xFF000000 to draw the character transparently (no bg fill).   */
#define GFX_TRANSPARENT  0xFF000000u
void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_text(int x, int y, const char *s, uint32_t fg, uint32_t bg);
int  gfx_text_width(const char *s);   /* pixel width of a string */
int  gfx_char_w(void);                /* always 8  */
int  gfx_char_h(void);                /* always 16 */
