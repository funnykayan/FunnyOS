#include "printf.h"
#include "string.h"
#include "../terminal/terminal.h"
#include <stdarg.h>
#include <stdint.h>

void kputchar(char c) { term_putchar(c); }

void kputs(const char *s) {
    while (*s) kputchar(*s++);
}

void kvprintf(const char *fmt, va_list ap) {
    char buf[32];

    while (*fmt) {
        if (*fmt != '%') { kputchar(*fmt++); continue; }
        fmt++;  /* skip '%' */

        /* Flags */
        int zero_pad = 0, left_align = 0;
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        if (*fmt == '-') { left_align = 1; fmt++; }

        /* Width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        /* Long modifier */
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }

        char pad = zero_pad ? '0' : ' ';

        switch (*fmt++) {
        case 'd': case 'i': {
            int64_t v = is_long ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int);
            kitoa(v, buf, 10);
            int len = (int)kstrlen(buf);
            if (!left_align) for (int i = len; i < width; i++) kputchar(pad);
            kputs(buf);
            if (left_align)  for (int i = len; i < width; i++) kputchar(' ');
            break;
        }
        case 'u': {
            uint64_t v = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned);
            kutoa(v, buf, 10);
            int len = (int)kstrlen(buf);
            if (!left_align) for (int i = len; i < width; i++) kputchar(pad);
            kputs(buf);
            if (left_align)  for (int i = len; i < width; i++) kputchar(' ');
            break;
        }
        case 'x': case 'X': {
            uint64_t v = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned);
            kutoa(v, buf, 16);
            int len = (int)kstrlen(buf);
            if (!left_align) for (int i = len; i < width; i++) kputchar(pad);
            kputs(buf);
            if (left_align)  for (int i = len; i < width; i++) kputchar(' ');
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            kputs("0x");
            kutoa(v, buf, 16);
            kputs(buf);
            break;
        }
        case 'c':
            kputchar((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = (int)kstrlen(s);
            if (!left_align) for (int i = len; i < width; i++) kputchar(' ');
            kputs(s);
            if (left_align)  for (int i = len; i < width; i++) kputchar(' ');
            break;
        }
        case '%':
            kputchar('%');
            break;
        default:
            kputchar('?');
            break;
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
