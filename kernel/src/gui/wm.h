#pragma once
#include <stdint.h>

/*
 * wm.h  –  Minimal window manager for FunnyOS
 *
 * Supports up to WM_MAX_WINDOWS draggable, closeable windows.
 * Each window has a titlebar, 1-px border, client area, and optional
 * static text content (displayed by the WM on every redraw).
 *
 * Integration:
 *   1. Call wm_init()  once (after mouse_init).
 *   2. Call wm_create() to add windows.
 *   3. Pass every mouse event to wm_on_mouse().
 */

#define WM_MAX_WINDOWS  8
#define WM_TITLEBAR_H   20      /* pixels tall */
#define WM_BORDER       2       /* border thickness */
#define WM_MAX_LINES    12      /* static content lines per window */
#define WM_LINE_LEN     56      /* chars per content line */

typedef struct {
    int      x, y;              /* top-left corner of the whole window */
    int      w, h;              /* total size including titlebar+border */
    char     title[48];
    uint32_t title_bg;          /* titlebar colour  */
    uint32_t body_bg;           /* client area colour */
    /* static text content shown inside the client area */
    char     lines[WM_MAX_LINES][WM_LINE_LEN];
    int      n_lines;
    /* if set, called instead of fill+lines draw for the client area */
    void   (*on_paint_client)(int x, int y, int w, int h);
    int      no_close;          /* 1 = hide and disable the X button */
    int      used;
} window_t;

/* Initialise the WM: paint the desktop and taskbar. */
void      wm_init(void);

/* Create a new window; returns pointer to it or NULL if full. */
window_t *wm_create(int x, int y, int w, int h, const char *title);

/* Close (destroy) a window and repaint. */
void      wm_close(window_t *win);

/* Append a line of static text into window's client area. */
void      wm_add_line(window_t *win, const char *text);

/* Repaint everything: desktop → taskbar → windows. */
void      wm_draw_all(void);

/* Feed a mouse event.  Call this from the mouse IRQ callback. */
void      wm_on_mouse(int x, int y, int btns, int prev_btns);
