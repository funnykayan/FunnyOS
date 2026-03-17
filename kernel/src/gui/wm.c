/*
 * wm.c  –  Minimal window manager for FunnyOS
 */
#include "wm.h"
#include "gfx.h"
#include "../lib/string.h"
#include "../terminal/terminal.h"
#include <stdint.h>

/* ── Colour palette ───────────────────────────────────────────────────────── */
#define COL_DESKTOP     0x2D6FA8u   /* steel-ocean blue desktop */
#define COL_DOT         0x3D7FB8u   /* lighter grid dot */
#define COL_TASKBAR     0x16213Eu   /* dark navy taskbar */
#define COL_TASKBAR_SEP 0x2A4080u
#define COL_TITLE_ACT   0x0050EFu   /* active window titlebar */
#define COL_TITLE_INACT 0x5A5A5Au   /* inactive */
#define COL_TITLE_FG    0xFFFFFFu
#define COL_BORDER_HI   0xCCCCCCu
#define COL_BORDER_LO   0x888888u
#define COL_CLOSE_BG    0xE81123u
#define COL_CLOSE_FG    0xFFFFFFu
#define COL_BODY_DEF    0xD4D0C8u   /* classic Windows 9x gray */
#define COL_CONTENT_FG  0x000000u

#define TASKBAR_H       30
#define CLOSE_W         18

/* ── State ────────────────────────────────────────────────────────────────── */
static window_t  windows[WM_MAX_WINDOWS];
static int       z_order[WM_MAX_WINDOWS];   /* [0]=bottom, [n-1]=top */
static int       n_windows = 0;
static int       drag_idx  = -1;
static int       drag_ox   = 0, drag_oy = 0;

/* ── Z-order ──────────────────────────────────────────────────────────────── */
static void z_bring_front(int idx) {
    int pos = -1;
    for (int i = 0; i < n_windows; i++)
        if (z_order[i] == idx) { pos = i; break; }
    if (pos < 0 || pos == n_windows - 1) return;
    for (int i = pos; i < n_windows - 1; i++)
        z_order[i] = z_order[i + 1];
    z_order[n_windows - 1] = idx;
}

/* ── Drawing ──────────────────────────────────────────────────────────────── */
static void draw_window(int idx) {
    window_t *w  = &windows[idx];
    int is_top   = (n_windows > 0 && z_order[n_windows - 1] == idx);
    uint32_t tbg = is_top ? COL_TITLE_ACT : COL_TITLE_INACT;

    /* raised 3-D border: outer dark, inner light */
    gfx_draw_rect(w->x,     w->y,     w->w,     w->h,     COL_BORDER_LO);
    gfx_draw_rect(w->x + 1, w->y + 1, w->w - 2, w->h - 2, COL_BORDER_HI);

    /* titlebar fill */
    gfx_fill_rect(w->x + WM_BORDER, w->y + WM_BORDER,
                  w->w - WM_BORDER * 2, WM_TITLEBAR_H, tbg);

    /* close button (skip if no_close) */
    int cb_x = w->x + w->w - WM_BORDER - CLOSE_W;
    int cb_y = w->y + WM_BORDER;
    if (!w->no_close) {
        gfx_fill_rect(cb_x, cb_y, CLOSE_W, WM_TITLEBAR_H, COL_CLOSE_BG);
        gfx_draw_char(cb_x + (CLOSE_W - 8) / 2,
                      cb_y + (WM_TITLEBAR_H - 16) / 2,
                      'X', COL_CLOSE_FG, COL_CLOSE_BG);
    }

    /* title text: only go as far as the close button (or full title bar if no_close) */
    int tx = w->x + WM_BORDER + 5;
    int ty = w->y + WM_BORDER + (WM_TITLEBAR_H - 16) / 2;
    int max_chars = (w->no_close ? (w->x + w->w - WM_BORDER - tx) : (cb_x - tx)) / 8;
    if (max_chars > 0) {
        char clipped[48];
        int ci = 0;
        while (w->title[ci] && ci < max_chars && ci < 47) {
            clipped[ci] = w->title[ci]; ci++;
        }
        clipped[ci] = '\0';
        gfx_draw_text(tx, ty, clipped, COL_TITLE_FG, tbg);
    }

    /* client area: call custom painter or default fill+lines */
    int cl_x = w->x + WM_BORDER;
    int cl_y = w->y + WM_BORDER + WM_TITLEBAR_H;
    int cl_w = w->w - WM_BORDER * 2;
    int cl_h = w->h - WM_BORDER * 2 - WM_TITLEBAR_H;
    if (cl_w > 0 && cl_h > 0) {
        if (w->on_paint_client) {
            w->on_paint_client(cl_x, cl_y, cl_w, cl_h);
        } else {
            gfx_fill_rect(cl_x, cl_y, cl_w, cl_h, w->body_bg);
            for (int i = 0; i < w->n_lines; i++) {
                int ly = cl_y + 4 + i * 18;
                if (ly + 16 > cl_y + cl_h) break;
                gfx_draw_text(cl_x + 5, ly, w->lines[i],
                              COL_CONTENT_FG, w->body_bg);
            }
        }
    }
}

static void draw_taskbar(void) {
    int sw = fb_get_width(), sh = fb_get_height();
    gfx_fill_rect(0, sh - TASKBAR_H, sw, TASKBAR_H, COL_TASKBAR);
    gfx_draw_line(0, sh - TASKBAR_H, sw - 1, sh - TASKBAR_H, COL_TASKBAR_SEP);

    int ty = sh - TASKBAR_H + (TASKBAR_H - 16) / 2;
    gfx_draw_text( 8, ty, "FunnyOS",  0x00FFFFu, COL_TASKBAR);
    gfx_fill_rect(74, sh - TASKBAR_H + 4, 2, TASKBAR_H - 8, COL_TASKBAR_SEP);
    gfx_draw_text(80, ty,
        "Drag titlebar to move  |  Click X to close  |  Shell runs below",
        0xAAAAAAu, COL_TASKBAR);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void wm_init(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        windows[i].used = 0;
        z_order[i]      = i;
    }
    n_windows = 0;
    drag_idx  = -1;
    wm_draw_all();
}

window_t *wm_create(int x, int y, int w, int h, const char *title) {
    if (n_windows >= WM_MAX_WINDOWS) return 0;

    int idx = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (!windows[i].used) { idx = i; break; }
    if (idx < 0) return 0;

    window_t *win = &windows[idx];
    win->used            = 1;
    win->x               = x;  win->y    = y;
    win->w               = w;  win->h    = h;
    win->title_bg        = COL_TITLE_ACT;
    win->body_bg         = COL_BODY_DEF;
    win->n_lines         = 0;
    win->on_paint_client = 0;
    win->no_close        = 0;

    int ti = 0;
    while (title[ti] && ti < 47) { win->title[ti] = title[ti]; ti++; }
    win->title[ti] = '\0';

    z_order[n_windows++] = idx;
    wm_draw_all();
    return win;
}

void wm_close(window_t *win) {
    if (!win || !win->used) return;
    int idx = (int)(win - windows);
    win->used = 0;

    int pos = -1;
    for (int i = 0; i < n_windows; i++)
        if (z_order[i] == idx) { pos = i; break; }
    if (pos >= 0) {
        for (int i = pos; i < n_windows - 1; i++)
            z_order[i] = z_order[i + 1];
        n_windows--;
    }
    if (drag_idx == idx) drag_idx = -1;
    wm_draw_all();
}

void wm_add_line(window_t *win, const char *text) {
    if (!win || !win->used || win->n_lines >= WM_MAX_LINES) return;
    char *dst = win->lines[win->n_lines];
    int i = 0;
    while (text[i] && i < WM_LINE_LEN - 1) { dst[i] = text[i]; i++; }
    dst[i] = '\0';
    win->n_lines++;
}

void wm_draw_all(void) {
    int sw = fb_get_width(), sh = fb_get_height();

    /* desktop background */
    gfx_fill_rect(0, 0, sw, sh - TASKBAR_H, COL_DESKTOP);

    /* subtle dot grid */
    for (int py = 12; py < sh - TASKBAR_H; py += 16)
        for (int px = 12; px < sw; px += 16)
            fb_put_pixel(px, py, COL_DOT);

    /* windows — bottom z first */
    for (int i = 0; i < n_windows; i++)
        draw_window(z_order[i]);

    /* taskbar always on top of windows */
    draw_taskbar();
}

/* ── Mouse event handler ──────────────────────────────────────────────────── */

void wm_on_mouse(int x, int y, int btns, int prev_btns) {
    int pressed  = ( btns & 1) && !(prev_btns & 1);
    int held     = ( btns & 1);
    int released = !(btns & 1) &&  (prev_btns & 1);

    if (pressed) {
        /* hit-test topmost-first */
        for (int zi = n_windows - 1; zi >= 0; zi--) {
            int idx = z_order[zi];
            window_t *w = &windows[idx];
            if (x < w->x || x >= w->x + w->w ||
                y < w->y || y >= w->y + w->h)
                continue;

            z_bring_front(idx);

            /* close button (only if enabled) */
            if (!w->no_close) {
                int cb_x = w->x + w->w - WM_BORDER - CLOSE_W;
                int cb_y = w->y + WM_BORDER;
                if (x >= cb_x && y >= cb_y &&
                    x < cb_x + CLOSE_W && y < cb_y + WM_TITLEBAR_H) {
                    wm_close(w);
                    return;
                }
            }

            /* titlebar drag? */
            int drag_cb_x = w->no_close ? (w->x + w->w - WM_BORDER)
                                        : (w->x + w->w - WM_BORDER - CLOSE_W);
            if (y >= w->y + WM_BORDER &&
                y <  w->y + WM_BORDER + WM_TITLEBAR_H &&
                x <  drag_cb_x) {
                drag_idx = idx;
                drag_ox  = x - w->x;
                drag_oy  = y - w->y;
            }

            wm_draw_all();
            return;
        }
    }

    if (held && drag_idx >= 0) {
        window_t *w = &windows[drag_idx];
        int sw = fb_get_width(), sh = fb_get_height();
        int nx = x - drag_ox, ny = y - drag_oy;
        if (nx < 0)                        nx = 0;
        if (ny < 0)                        ny = 0;
        if (nx + w->w > sw)                nx = sw - w->w;
        if (ny + w->h > sh - TASKBAR_H)    ny = sh - TASKBAR_H - w->h;
        w->x = nx; w->y = ny;
        wm_draw_all();
        return;
    }

    if (released) drag_idx = -1;
}
