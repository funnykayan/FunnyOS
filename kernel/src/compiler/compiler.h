#pragma once
#include <stdint.h>
#include <stddef.h>

/* ══ Result returned by the compiler ══════════════════════════════════════ */
typedef struct {
    const char *error;      /* NULL on success */
    size_t      code_size;  /* bytes of machine code produced */
} compiler_result_t;

/*
 * tcc_compile() – compile a C source string into x86-64 machine code.
 *
 *  src       : null-terminated C source text
 *  out_buf   : caller-allocated buffer that will receive raw x86-64 bytes
 *  out_size  : size of out_buf in bytes
 *
 * Supported language subset
 * ─────────────────────────
 *   • Types   : int, char, char* (no structs/unions)
 *   • Decls   : global & local int/char variables
 *   • Exprs   : + - * / %  == != < > <= >=  && ||  !  unary -  = (assign)
 *   • Stmts   : if/else  while  for  return  { }  expression-stmt
 *   • Funcs   : definitions with up to 6 int parameters (no varargs)
 *   • Builtins: printf(fmt, ...)  putchar(c)  getchar()
 *   • Strings : string literals (stored in code segment, read-only)
 *
 * The produced code uses the System V AMD64 calling convention.
 * Entry point is the function named "main"; it is called with no arguments.
 * Built-in functions are resolved to kernel function pointers at compile time.
 */
compiler_result_t tcc_compile(const char *src,
                               uint8_t    *out_buf,
                               size_t      out_size);
