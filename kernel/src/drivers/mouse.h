#pragma once
#include <stdint.h>

/* Initialise the PS/2 auxiliary (mouse) device and draw the initial cursor. */
void mouse_init(void);

/* Query the current cursor position and button state. */
void mouse_get_state(int *x, int *y, int *buttons);

/*
 * Register a callback invoked on every mouse event, BETWEEN cursor erase
 * and cursor redraw — so wm_on_mouse can call wm_draw_all() safely.
 * Signature: cb(x, y, current_buttons, previous_buttons)
 */
void mouse_set_callback(void (*cb)(int x, int y, int btns, int prev_btns));
