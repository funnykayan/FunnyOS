#pragma once
#include <stdint.h>

/* Special key codes (values ≥ 0x80, returned as unsigned char) */
#define KEY_UP    0x80u
#define KEY_DOWN  0x81u
#define KEY_LEFT  0x82u
#define KEY_RIGHT 0x83u
#define KEY_HOME  0x84u
#define KEY_END   0x85u
#define KEY_DEL   0x86u
#define KEY_PGUP  0x87u
#define KEY_PGDN  0x88u
#define KEY_INS   0x89u

void  kbd_init(void);

/* Blocking read of a single character from the keyboard */
char  kbd_getchar(void);

/* Non-blocking – returns 0 if no key available */
char  kbd_try_getchar(void);

/* Read a full line (until Enter), echoes to terminal, stored in buf.
 * max_len includes the null terminator.
 * Returns number of characters read (not counting '\0'). */
int   kbd_readline(char *buf, int max_len);
