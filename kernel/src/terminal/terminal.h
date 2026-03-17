#pragma once
#include <stdint.h>

void term_init(void);
void term_putchar(char c);
void term_clear(void);
void term_set_color(uint32_t fg, uint32_t bg);

/* Bind the terminal to a sub-region of the framebuffer and clear it.      */
void term_set_viewport(int x, int y, int w, int h);

/* Repaint the viewport from the back-buffer (called by WM on window draw). */
void term_blit_to_fb(void);

/* Raw framebuffer access (used by drivers, e.g. mouse cursor) */
void     fb_put_pixel(int x, int y, uint32_t color);
uint32_t fb_get_pixel(int x, int y);
void     fb_fill_rect(int x, int y, int w, int h, uint32_t color);
int      fb_get_width(void);
int      fb_get_height(void);

/* ANSI-style color presets */
#define COLOR_BLACK   0x000000
#define COLOR_WHITE   0xFFFFFF
#define COLOR_GREEN   0x00FF00
#define COLOR_RED     0xFF0000
#define COLOR_CYAN    0x00FFFF
#define COLOR_YELLOW  0xFFFF00
#define COLOR_GRAY    0xAAAAAA
#define COLOR_DKGRAY  0x333333
