#pragma once
#include <stdarg.h>

/* Formatted print to the kernel terminal */
void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);

/* Raw single character */
void kputchar(char c);

/* Raw string */
void kputs(const char *s);
