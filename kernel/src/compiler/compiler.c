/* ============================================================
 *  FunnyOS  –  Tiny C Compiler  (tcc)
 *
 *  Single-pass recursive-descent compiler.
 *  Produces x86-64 machine code for a restricted C subset:
 *    • int / char / char* variables (global & local)
 *    • Arithmetic: + - * / %
 *    • Comparisons: == != < > <= >=
 *    • Logical: && || !
 *    • Assign: =
 *    • Control: if/else  while  for  return  { }
 *    • Funcs: definitions with ≤ 6 int params, recursive calls
 *    • Builtins: printf  putchar  getchar
 *    • String literals
 *
 *  Calling convention: System V AMD64
 * ============================================================ */

#include "compiler.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../mm/pmm.h"
#include <stdint.h>

/* ── forward declarations of kernel builtins ─────────────────────────────── */
extern void kprintf(const char *fmt, ...);
extern void kputchar(char c);
extern char kbd_getchar(void);

/* ════════════════════════════════════════════════════════════════════════════
 *  Lexer
 * ════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    T_EOF=0,
    /* keywords */
    T_INT, T_CHAR, T_VOID, T_RETURN, T_IF, T_ELSE, T_WHILE, T_FOR,
    /* literals */
    T_IDENT, T_NUMBER, T_STRING, T_CHARLIT,
    /* punctuation */
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_SEMI, T_COMMA, T_STAR, T_AMP,
    /* operators */
    T_PLUS, T_MINUS, T_SLASH, T_PERCENT,
    T_EQ, T_NEQ, T_LT, T_GT, T_LE, T_GE,
    T_ASSIGN, T_AND, T_OR, T_NOT,
    T_INC, T_DEC,
} tok_type_t;

#define MAX_IDENT   64
#define MAX_STR    256

typedef struct {
    tok_type_t type;
    int64_t    ival;
    char       sval[MAX_STR];
} token_t;

/* Lexer state */
static const char *lex_src;
static int         lex_pos;
static token_t     cur_tok;
static token_t     peek_tok;
static int         peek_valid;

/* Code emitter state */
static uint8_t *emit_buf;
static size_t   emit_pos;
static size_t   emit_cap;
static const char *compile_error;

/* ── utility ─────────────────────────────────────────────────────────────── */
#define ERR(msg) do { compile_error = (msg); return; } while(0)
#define ERRV(msg) do { compile_error = (msg); return 0; } while(0)
#define ERRC(msg) do { compile_error = (msg); } while(0)

static int is_digit(char c)  { return c >= '0' && c <= '9'; }
static int is_alpha(char c)  { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static int is_alnum(char c)  { return is_alpha(c) || is_digit(c); }

/* ── emitter helpers ─────────────────────────────────────────────────────── */
static void emit1(uint8_t b) {
    if (emit_pos < emit_cap) emit_buf[emit_pos++] = b;
    else ERRC("code buffer full");
}
static void emit2(uint8_t a, uint8_t b)            { emit1(a); emit1(b); }
static void emit3(uint8_t a, uint8_t b, uint8_t c) { emit1(a); emit1(b); emit1(c); }
static void emit4(uint8_t a,uint8_t b,uint8_t c,uint8_t d){emit1(a);emit1(b);emit1(c);emit1(d);}

static void emit_i32(int32_t v) {
    emit1((v>> 0)&0xFF); emit1((v>> 8)&0xFF);
    emit1((v>>16)&0xFF); emit1((v>>24)&0xFF);
}
static void emit_i64(int64_t v) {
    emit_i32((int32_t)(v & 0xFFFFFFFF));
    emit_i32((int32_t)((v >> 32) & 0xFFFFFFFF));
}

/* Patch a 32-bit relative offset at position `at` */
static void patch_i32(size_t at, int32_t v) {
    emit_buf[at+0] = (v>> 0)&0xFF; emit_buf[at+1] = (v>> 8)&0xFF;
    emit_buf[at+2] = (v>>16)&0xFF; emit_buf[at+3] = (v>>24)&0xFF;
}

/* Common x86-64 sequences */
static void emit_push_rax(void)  { emit1(0x50); }            /* push rax  */
static void emit_pop_rax(void)   { emit1(0x58); }            /* pop  rax  */
static void emit_pop_rbx(void)   { emit1(0x5B); }            /* pop  rbx  */
static void emit_push_rbp(void)  { emit1(0x55); }            /* push rbp  */
static void emit_pop_rbp(void)   { emit1(0x5D); }            /* pop  rbp  */
static void emit_ret(void)       { emit1(0xC3); }            /* ret       */
static void emit_nop(void)       { emit1(0x90); }

/* mov rbp, rsp */
static void emit_mov_rbp_rsp(void) { emit3(0x48,0x89,0xE5); }
/* mov rsp, rbp */
static void emit_mov_rsp_rbp(void) { emit3(0x48,0x89,0xEC); }

/* mov rax, imm64 */
static void emit_mov_rax_imm64(int64_t v) {
    emit2(0x48,0xB8); emit_i64(v);
}
/* mov rax, imm32 (sign-extend) */
static void emit_mov_rax_imm32(int32_t v) {
    emit3(0x48,0xC7,0xC0); emit_i32(v);
}

/* load rax from [rbp + disp] */
static void emit_load_rbp(int disp) {
    if (disp >= -128 && disp <= 127) {
        emit3(0x48,0x8B,0x45); emit1((uint8_t)(int8_t)disp);
    } else {
        emit3(0x48,0x8B,0x85); emit_i32(disp);
    }
}
/* store rax to [rbp + disp] */
static void emit_store_rbp(int disp) {
    if (disp >= -128 && disp <= 127) {
        emit3(0x48,0x89,0x45); emit1((uint8_t)(int8_t)disp);
    } else {
        emit3(0x48,0x89,0x85); emit_i32(disp);
    }
}

/* sub rsp, imm32 */
static void emit_sub_rsp_imm32(int32_t v) {
    emit3(0x48,0x81,0xEC); emit_i32(v);
}
/* add rsp, imm32 */
static void emit_add_rsp_imm32(int32_t v) {
    emit3(0x48,0x81,0xC4); emit_i32(v);
}

/* test rax, rax */
static void emit_test_rax(void) { emit3(0x48,0x85,0xC0); }

/* jz rel32 */
static void emit_jz(void) { emit2(0x0F,0x84); emit_i32(0); }
/* jnz rel32 */
static void emit_jnz(void){ emit2(0x0F,0x85); emit_i32(0); }
/* jmp rel32 */
static void emit_jmp(void){ emit1(0xE9);       emit_i32(0); }

/* call rax */
static void emit_call_rax(void) { emit2(0xFF,0xD0); }

/* ── Lexer implementation ─────────────────────────────────────────────────── */

static void skip_whitespace_comments(void) {
    while (1) {
        char c = lex_src[lex_pos];
        if (c == ' '||c=='\t'||c=='\n'||c=='\r') { lex_pos++; continue; }
        if (c=='/' && lex_src[lex_pos+1]=='/') {
            while (lex_src[lex_pos] && lex_src[lex_pos]!='\n') lex_pos++;
            continue;
        }
        if (c=='/' && lex_src[lex_pos+1]=='*') {
            lex_pos += 2;
            while (lex_src[lex_pos] &&
                   !(lex_src[lex_pos]=='*'&&lex_src[lex_pos+1]=='/')) lex_pos++;
            if (lex_src[lex_pos]) lex_pos += 2;
            continue;
        }
        /* Skip preprocessor directives (#include, #define, etc.) */
        if (c == '#') {
            while (lex_src[lex_pos] && lex_src[lex_pos] != '\n') lex_pos++;
            continue;
        }
        break;
    }
}

static token_t lex_next(void) {
    token_t t;
    kmemset(&t, 0, sizeof(t));
    skip_whitespace_comments();
    char c = lex_src[lex_pos];

    if (!c) { t.type = T_EOF; return t; }

    /* Numbers */
    if (is_digit(c)) {
        int64_t v = 0;
        if (c=='0' && (lex_src[lex_pos+1]=='x'||lex_src[lex_pos+1]=='X')) {
            lex_pos += 2;
            while (1) {
                char h = lex_src[lex_pos];
                if      (h>='0'&&h<='9') v = v*16+(h-'0');
                else if (h>='a'&&h<='f') v = v*16+(h-'a'+10);
                else if (h>='A'&&h<='F') v = v*16+(h-'A'+10);
                else break;
                lex_pos++;
            }
        } else {
            while (is_digit(lex_src[lex_pos])) v = v*10+(lex_src[lex_pos++]-'0');
        }
        t.type = T_NUMBER; t.ival = v; return t;
    }

    /* Identifiers & keywords */
    if (is_alpha(c)) {
        int i = 0;
        while (is_alnum(lex_src[lex_pos]) && i < MAX_IDENT-1)
            t.sval[i++] = lex_src[lex_pos++];
        t.sval[i] = '\0';
        if (!kstrcmp(t.sval,"int"))    { t.type=T_INT;    return t; }
        if (!kstrcmp(t.sval,"char"))   { t.type=T_CHAR;   return t; }
        if (!kstrcmp(t.sval,"void"))   { t.type=T_VOID;   return t; }
        if (!kstrcmp(t.sval,"return")) { t.type=T_RETURN; return t; }
        if (!kstrcmp(t.sval,"if"))     { t.type=T_IF;     return t; }
        if (!kstrcmp(t.sval,"else"))   { t.type=T_ELSE;   return t; }
        if (!kstrcmp(t.sval,"while"))  { t.type=T_WHILE;  return t; }
        if (!kstrcmp(t.sval,"for"))    { t.type=T_FOR;    return t; }
        t.type = T_IDENT; return t;
    }

    /* String literals */
    if (c == '"') {
        lex_pos++;
        int i = 0;
        while (lex_src[lex_pos] && lex_src[lex_pos]!='"' && i<MAX_STR-1) {
            if (lex_src[lex_pos]=='\\') {
                lex_pos++;
                switch(lex_src[lex_pos]) {
                case 'n':  t.sval[i++]='\n'; break;
                case 't':  t.sval[i++]='\t'; break;
                case '"':  t.sval[i++]='"';  break;
                case '\\': t.sval[i++]='\\'; break;
                case '0':  t.sval[i++]='\0'; break;
                default:   t.sval[i++]=lex_src[lex_pos]; break;
                }
            } else {
                t.sval[i++] = lex_src[lex_pos];
            }
            lex_pos++;
        }
        if (lex_src[lex_pos]=='"') lex_pos++;
        t.sval[i]='\0'; t.type=T_STRING; return t;
    }

    /* Char literals */
    if (c == '\'') {
        lex_pos++;
        if (lex_src[lex_pos]=='\\') {
            lex_pos++;
            switch(lex_src[lex_pos]) {
            case 'n': t.ival='\n'; break; case 't': t.ival='\t'; break;
            case '0': t.ival=0;   break;  default:  t.ival=lex_src[lex_pos];
            }
        } else {
            t.ival = (unsigned char)lex_src[lex_pos];
        }
        lex_pos++;
        if (lex_src[lex_pos]=='\'') lex_pos++;
        t.type = T_CHARLIT; return t;
    }

    lex_pos++;
    switch (c) {
    case '(': t.type=T_LPAREN;   return t;
    case ')': t.type=T_RPAREN;   return t;
    case '{': t.type=T_LBRACE;   return t;
    case '}': t.type=T_RBRACE;   return t;
    case '[': t.type=T_LBRACKET; return t;
    case ']': t.type=T_RBRACKET; return t;
    case ';': t.type=T_SEMI;     return t;
    case ',': t.type=T_COMMA;    return t;
    case '%': t.type=T_PERCENT;  return t;
    case '~': t.type=T_NOT;      return t;  /* reuse NOT */
    case '&':
        if (lex_src[lex_pos]=='&') { lex_pos++; t.type=T_AND; }
        else t.type=T_AMP;
        return t;
    case '|':
        if (lex_src[lex_pos]=='|') { lex_pos++; t.type=T_OR; }
        else t.type=T_OR;
        return t;
    case '!':
        if (lex_src[lex_pos]=='=') { lex_pos++; t.type=T_NEQ; }
        else t.type=T_NOT;
        return t;
    case '=':
        if (lex_src[lex_pos]=='=') { lex_pos++; t.type=T_EQ; }
        else t.type=T_ASSIGN;
        return t;
    case '<':
        if (lex_src[lex_pos]=='=') { lex_pos++; t.type=T_LE; }
        else t.type=T_LT;
        return t;
    case '>':
        if (lex_src[lex_pos]=='=') { lex_pos++; t.type=T_GE; }
        else t.type=T_GT;
        return t;
    case '+':
        if (lex_src[lex_pos]=='+') { lex_pos++; t.type=T_INC; }
        else t.type=T_PLUS;
        return t;
    case '-':
        if (lex_src[lex_pos]=='-') { lex_pos++; t.type=T_DEC; }
        else t.type=T_MINUS;
        return t;
    case '*': t.type=T_STAR;     return t;
    case '/': t.type=T_SLASH;    return t;
    default:  t.type=T_EOF;      return t;
    }
}

static void advance(void) {
    if (peek_valid) { cur_tok = peek_tok; peek_valid = 0; }
    else cur_tok = lex_next();
}

static token_t peek(void) {
    if (!peek_valid) { peek_tok = lex_next(); peek_valid = 1; }
    return peek_tok;
}

static int match(tok_type_t t) {
    if (cur_tok.type == t) { advance(); return 1; }
    return 0;
}

static void expect(tok_type_t t, const char *msg) {
    if (cur_tok.type != t) { ERRC(msg); }
    else advance();
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Symbol table
 * ════════════════════════════════════════════════════════════════════════════ */

#define MAX_SYMS   64
#define MAX_FUNCS  32

typedef enum { SYM_LOCAL, SYM_GLOBAL, SYM_FUNC, SYM_PARAM } sym_kind_t;

typedef struct {
    char        name[MAX_IDENT];
    sym_kind_t  kind;
    int         rbp_offset;  /* for locals/params: [rbp + offset] */
    uint8_t    *func_addr;   /* for functions                     */
    int         n_params;
    int         used;
} sym_t;

static sym_t  syms[MAX_SYMS];
static int    sym_count = 0;

/* Per-function local frame */
static int    frame_size;       /* bytes allocated on stack          */
static int    local_count;      /* number of locals in current scope */

static sym_t *sym_find(const char *name) {
    for (int i = sym_count - 1; i >= 0; i--)
        if (syms[i].used && kstrcmp(syms[i].name, name) == 0)
            return &syms[i];
    return NULL;
}

static sym_t *sym_add(const char *name, sym_kind_t kind, int offset) {
    if (sym_count >= MAX_SYMS) { ERRC("too many symbols"); return NULL; }
    sym_t *s = &syms[sym_count++];
    kstrncpy(s->name, name, MAX_IDENT-1);
    s->kind       = kind;
    s->rbp_offset = offset;
    s->func_addr  = NULL;
    s->n_params   = 0;
    s->used       = 1;
    return s;
}

/* Remove symbols added since snapshot */
static void sym_restore(int snapshot) {
    for (int i = snapshot; i < sym_count; i++) syms[i].used = 0;
    sym_count = snapshot;
}

/* ── String literal pool ──────────────────────────────────────────────────── */
/* We store string literals directly in the code buffer and reference them
 * by absolute address.  Each is emitted as a series of bytes followed by
 * a null terminator, then code resumes.  We use a jmp around the data.     */

/* Emit a string literal into the code buffer and return its address. */
static const uint8_t *emit_string_data(const char *s) {
    /* jump over the data */
    emit_jmp();
    size_t jmp_pos = emit_pos - 4; /* position of the rel32 placeholder */

    const uint8_t *str_addr = emit_buf + emit_pos;
    size_t len = kstrlen(s) + 1;
    for (size_t i = 0; i < len; i++) emit1((uint8_t)s[i]);

    /* patch the jump */
    int32_t rel = (int32_t)(emit_pos - (jmp_pos + 4));
    patch_i32(jmp_pos, rel);

    return str_addr;
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Parser / Code generator
 * ════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations */
static void parse_stmt(void);
static void parse_expr(void);
static void parse_block(void);

/* ── Register argument passing helpers ───────────────────────────────────── */
/* System V AMD64: first 6 integer args in rdi, rsi, rdx, rcx, r8, r9       */
static void emit_pop_argreg(int idx) {
    switch (idx) {
    case 0: emit1(0x5F); return; /* pop rdi */
    case 1: emit1(0x5E); return; /* pop rsi */
    case 2: emit1(0x5A); return; /* pop rdx */
    case 3: emit1(0x59); return; /* pop rcx */
    case 4: emit2(0x41,0x58); return; /* pop r8 */
    case 5: emit2(0x41,0x59); return; /* pop r9 */
    default: emit_pop_rax(); return; /* ignore extra args */
    }
}

/* ── Expression parsing ───────────────────────────────────────────────────── */

static void parse_primary(void) {
    if (compile_error) return;

    /* Number literal */
    if (cur_tok.type == T_NUMBER || cur_tok.type == T_CHARLIT) {
        int64_t v = cur_tok.ival;
        advance();
        if (v >= -2147483648LL && v <= 2147483647LL)
            emit_mov_rax_imm32((int32_t)v);
        else
            emit_mov_rax_imm64(v);
        return;
    }

    /* String literal → pointer to data */
    if (cur_tok.type == T_STRING) {
        char strbuf[MAX_STR];
        kstrncpy(strbuf, cur_tok.sval, MAX_STR-1);
        advance();
        const uint8_t *addr = emit_string_data(strbuf);
        emit_mov_rax_imm64((int64_t)(uintptr_t)addr);
        return;
    }

    /* Parenthesized expression */
    if (cur_tok.type == T_LPAREN) {
        advance();
        parse_expr();
        if (compile_error) return;
        expect(T_RPAREN, "expected ')'");
        return;
    }

    /* Identifier: variable access or function call */
    if (cur_tok.type == T_IDENT) {
        char name[MAX_IDENT];
        kstrncpy(name, cur_tok.sval, MAX_IDENT-1);
        advance();

        /* Function call? */
        if (cur_tok.type == T_LPAREN) {
            advance();

            /* Built-in: printf */
            if (!kstrcmp(name, "printf")) {
                /* Evaluate and push all args left-to-right */
                int argc = 0;
                if (cur_tok.type != T_RPAREN) {
                    do {
                        parse_expr(); if (compile_error) return;
                        emit_push_rax();
                        argc++;
                    } while (match(T_COMMA));
                }
                expect(T_RPAREN, "expected ')'");
                /* Pop args into registers in reverse */
                for (int i = argc-1; i >= 0; i--)
                    emit_pop_argreg(i);
                /* call kprintf */
                emit_mov_rax_imm64((int64_t)(uintptr_t)&kprintf);
                /* align stack: rsp must be 16-byte aligned before call.
                 * We push an even number of regs on the way in, which
                 * combined with the call's return address should keep
                 * alignment. Use a simple approach: sub/add rsp by 8 if needed. */
                emit_call_rax();
                /* rax = return value (we treat it as int) */
                return;
            }

            /* Built-in: putchar */
            if (!kstrcmp(name, "putchar")) {
                if (cur_tok.type != T_RPAREN) {
                    parse_expr(); if (compile_error) return;
                }
                expect(T_RPAREN, "expected ')'");
                /* move rax to rdi then call kputchar */
                emit3(0x48,0x89,0xC7);  /* mov rdi, rax */
                emit_mov_rax_imm64((int64_t)(uintptr_t)&kputchar);
                emit_call_rax();
                return;
            }

            /* Built-in: getchar */
            if (!kstrcmp(name, "getchar")) {
                expect(T_RPAREN, "expected ')'");
                emit_mov_rax_imm64((int64_t)(uintptr_t)&kbd_getchar);
                emit_call_rax();
                /* zero-extend al to rax */
                emit4(0x48,0x0F,0xB6,0xC0);
                return;
            }

            /* User-defined function call */
            sym_t *fn = sym_find(name);
            if (!fn || fn->kind != SYM_FUNC) {
                ERRC("undefined function"); return;
            }
            int argc = 0;
            if (cur_tok.type != T_RPAREN) {
                do {
                    parse_expr(); if (compile_error) return;
                    emit_push_rax();
                    argc++;
                } while (match(T_COMMA));
            }
            expect(T_RPAREN, "expected ')'");
            for (int i = argc-1; i >= 0; i--)
                emit_pop_argreg(i);
            emit_mov_rax_imm64((int64_t)(uintptr_t)fn->func_addr);
            emit_call_rax();
            return;
        }

        /* Variable read */
        sym_t *s = sym_find(name);
        if (!s) { ERRC("undefined variable"); return; }
        emit_load_rbp(s->rbp_offset);
        return;
    }

    ERRC("unexpected token in expression");
}

static void parse_unary(void) {
    if (compile_error) return;
    if (cur_tok.type == T_MINUS) {
        advance();
        parse_unary();
        /* neg rax */
        emit3(0x48,0xF7,0xD8);
        return;
    }
    if (cur_tok.type == T_NOT) {
        advance();
        parse_unary();
        emit_test_rax();
        emit3(0x0F,0x94,0xC0);  /* sete al */
        emit4(0x48,0x0F,0xB6,0xC0); /* movzx rax, al */
        return;
    }
    parse_primary();
}

static void parse_mul(void) {
    parse_unary(); if (compile_error) return;
    while (cur_tok.type==T_STAR||cur_tok.type==T_SLASH||cur_tok.type==T_PERCENT) {
        tok_type_t op = cur_tok.type; advance();
        emit_push_rax();
        parse_unary(); if (compile_error) return;
        /* rax = right, [rsp] = left */
        emit3(0x48,0x89,0xC3);  /* mov rbx, rax (save right) */
        emit_pop_rax();          /* rax = left */
        if (op == T_STAR) {
            emit4(0x48,0x0F,0xAF,0xC3); /* imul rax, rbx */
        } else {
            emit2(0x48,0x99); /* cqo */
            emit3(0x48,0xF7,0xFB); /* idiv rbx */
            if (op == T_PERCENT) {
                emit3(0x48,0x89,0xD0); /* mov rax, rdx */
            }
        }
    }
}

static void parse_add(void) {
    parse_mul(); if (compile_error) return;
    while (cur_tok.type==T_PLUS||cur_tok.type==T_MINUS) {
        tok_type_t op = cur_tok.type; advance();
        emit_push_rax();
        parse_mul(); if (compile_error) return;
        emit3(0x48,0x89,0xC3);  /* mov rbx, rax */
        emit_pop_rax();
        if (op==T_PLUS) emit3(0x48,0x01,0xD8);  /* add rax, rbx */
        else             emit3(0x48,0x29,0xD8);  /* sub rax, rbx */
    }
}

static void parse_relational(void) {
    parse_add(); if (compile_error) return;
    while (cur_tok.type==T_LT||cur_tok.type==T_GT||
           cur_tok.type==T_LE||cur_tok.type==T_GE) {
        tok_type_t op = cur_tok.type; advance();
        emit_push_rax();
        parse_add(); if (compile_error) return;
        emit3(0x48,0x89,0xC3);  /* mov rbx, rax */
        emit_pop_rax();
        emit3(0x48,0x39,0xD8);  /* cmp rax, rbx */
        uint8_t setcc = (op==T_LT)?0x9C:(op==T_GT)?0x9F:(op==T_LE)?0x9E:0x9D;
        emit3(0x0F,setcc,0xC0);
        emit4(0x48,0x0F,0xB6,0xC0);
    }
}

static void parse_equality(void) {
    parse_relational(); if (compile_error) return;
    while (cur_tok.type==T_EQ||cur_tok.type==T_NEQ) {
        tok_type_t op = cur_tok.type; advance();
        emit_push_rax();
        parse_relational(); if (compile_error) return;
        emit3(0x48,0x89,0xC3);
        emit_pop_rax();
        emit3(0x48,0x39,0xD8);
        uint8_t setcc = (op==T_EQ)?0x94:0x95;
        emit3(0x0F,setcc,0xC0);
        emit4(0x48,0x0F,0xB6,0xC0);
    }
}

static void parse_logic_and(void) {
    parse_equality(); if (compile_error) return;
    while (cur_tok.type == T_AND) {
        advance();
        emit_push_rax();
        parse_equality(); if (compile_error) return;
        emit_pop_rbx();
        /* rax = rax && rbx  →  (rax != 0) & (rbx != 0) */
        emit_test_rax();
        emit3(0x0F,0x95,0xC0); /* setne al (rax) */
        emit4(0x48,0x0F,0xB6,0xC0);
        emit3(0x48,0x85,0xDB); /* test rbx,rbx */
        emit3(0x0F,0x95,0xC3); /* setne bl */
        emit4(0x48,0x0F,0xB6,0xDB); /* movzx rbx,bl */
        emit3(0x48,0x21,0xD8); /* and rax,rbx */
    }
}

static void parse_logic_or(void) {
    parse_logic_and(); if (compile_error) return;
    while (cur_tok.type == T_OR) {
        advance();
        emit_push_rax();
        parse_logic_and(); if (compile_error) return;
        emit_pop_rbx();
        emit3(0x48,0x09,0xD8); /* or rax,rbx */
        emit_test_rax();
        emit3(0x0F,0x95,0xC0); /* setne al */
        emit4(0x48,0x0F,0xB6,0xC0);
    }
}

static void parse_assign(void) {
    /* Peek ahead: if IDENT followed by ASSIGN, it's an assignment */
    if (cur_tok.type == T_IDENT) {
        token_t id = cur_tok;
        token_t nx = peek();
        if (nx.type == T_ASSIGN) {
            advance(); advance(); /* consume ident and '=' */
            parse_assign(); if (compile_error) return;
            sym_t *s = sym_find(id.sval);
            if (!s) { ERRC("undefined variable in assignment"); return; }
            emit_store_rbp(s->rbp_offset);
            return;
        }
    }
    parse_logic_or();
}

static void parse_expr(void) { parse_assign(); }

/* ── Statement parsing ─────────────────────────────────────────────────────── */

static int is_type_token(tok_type_t t) {
    return t==T_INT || t==T_CHAR || t==T_VOID;
}

static void parse_local_decl(void) {
    /* type already consumed (but we re-consume the pointer star if needed) */
    int is_ptr = (cur_tok.type == T_STAR);
    if (is_ptr) advance();

    if (cur_tok.type != T_IDENT) { ERRC("expected identifier"); return; }
    char name[MAX_IDENT];
    kstrncpy(name, cur_tok.sval, MAX_IDENT-1);
    advance();

    local_count++;
    int offset = -(local_count * 8);
    frame_size = local_count * 8;
    sym_add(name, SYM_LOCAL, offset);

    /* Optional initializer */
    if (cur_tok.type == T_ASSIGN) {
        advance();
        parse_expr(); if (compile_error) return;
        emit_store_rbp(offset);
    } else {
        /* zero-initialize */
        emit_mov_rax_imm32(0);
        emit_store_rbp(offset);
    }
    expect(T_SEMI, "expected ';'");
}

static void parse_stmt(void) {
    if (compile_error) return;

    /* Block */
    if (cur_tok.type == T_LBRACE) { parse_block(); return; }

    /* Local variable declaration */
    if (is_type_token(cur_tok.type)) {
        advance();
        /* handle 'char *' */
        if (cur_tok.type == T_STAR) { /* do nothing special */ }
        parse_local_decl();
        return;
    }

    /* return statement */
    if (cur_tok.type == T_RETURN) {
        advance();
        if (cur_tok.type != T_SEMI) {
            parse_expr(); if (compile_error) return;
        } else {
            emit_mov_rax_imm32(0);
        }
        expect(T_SEMI, "expected ';' after return");
        /* Epilogue inline */
        emit_mov_rsp_rbp();
        emit_pop_rbp();
        emit_ret();
        return;
    }

    /* if statement */
    if (cur_tok.type == T_IF) {
        advance();
        expect(T_LPAREN, "expected '(' after if");
        parse_expr(); if (compile_error) return;
        expect(T_RPAREN, "expected ')'");

        emit_test_rax();
        emit_jz();
        size_t jz_pos = emit_pos - 4;

        parse_stmt(); if (compile_error) return;

        if (cur_tok.type == T_ELSE) {
            advance();
            emit_jmp();
            size_t jmp_pos = emit_pos - 4;
            patch_i32(jz_pos, (int32_t)(emit_pos - (jz_pos + 4)));
            parse_stmt(); if (compile_error) return;
            patch_i32(jmp_pos, (int32_t)(emit_pos - (jmp_pos + 4)));
        } else {
            patch_i32(jz_pos, (int32_t)(emit_pos - (jz_pos + 4)));
        }
        return;
    }

    /* while statement */
    if (cur_tok.type == T_WHILE) {
        advance();
        size_t loop_start = emit_pos;
        expect(T_LPAREN, "expected '(' after while");
        parse_expr(); if (compile_error) return;
        expect(T_RPAREN, "expected ')'");

        emit_test_rax();
        emit_jz();
        size_t jz_pos = emit_pos - 4;

        parse_stmt(); if (compile_error) return;

        /* jump back to loop start */
        emit_jmp();
        patch_i32(emit_pos - 4, (int32_t)(loop_start - emit_pos));
        patch_i32(jz_pos, (int32_t)(emit_pos - (jz_pos + 4)));
        return;
    }

    /* for statement */
    if (cur_tok.type == T_FOR) {
        advance();
        int snap = sym_count;
        expect(T_LPAREN, "expected '(' after for");

        /* init */
        if (cur_tok.type != T_SEMI) {
            if (is_type_token(cur_tok.type)) {
                advance(); parse_local_decl();
            } else {
                parse_expr(); if (compile_error) return;
                expect(T_SEMI, "expected ';'");
            }
        } else advance();

        size_t cond_start = emit_pos;
        size_t jz_pos = 0;
        int has_cond = (cur_tok.type != T_SEMI);
        if (has_cond) {
            parse_expr(); if (compile_error) return;
            emit_test_rax();
            emit_jz();
            jz_pos = emit_pos - 4;
        }
        expect(T_SEMI, "expected ';'");

        /* Save increment expression as bytes via second-pass trick:
         * emit a jmp over the increment, then the increment, then loop body.
         * Easier: collect increment token range and re-parse later.
         * Simpler approach: evaluate increment after body using a jmp. */

        /* We'll use the classic for-loop layout:
         *   init
         *   cond?  →  jz end
         *   jmp over-increment
         * inc_start:
         *   increment
         *   jmp cond_start
         * body_start:
         *   body
         *   jmp inc_start
         * end:                           */

        emit_jmp(); /* jmp to body, over increment */
        size_t jmp_over_inc = emit_pos - 4;
        size_t inc_start = emit_pos;

        /* Emit increment expression (save and restore lex state) */
        int saved_pos = lex_pos;
        const char *saved_src = lex_src;
        token_t saved_cur = cur_tok;
        token_t saved_peek = peek_tok;
        int saved_peek_valid = peek_valid;

        /* Scan over increment tokens to find ')' */
        int depth = 1;
        int inc_start_pos = lex_pos - (int)kstrlen(cur_tok.sval);
        /* Actually, the lexer already consumed tokens – we need the raw positions.
         * Simplest fix: save lex_pos before we started parsing the increment. */
        /* We'll just skip them for now and come back – use a forward-ref approach */
        /* Parse the increment, but save position first */
        int raw_pos_before_inc = lex_pos;
        /* Skip to ')' – count parens */
        /* We must operate at raw source level */
        while (lex_src[lex_pos] && depth > 0) {
            if (lex_src[lex_pos]=='(') depth++;
            if (lex_src[lex_pos]==')') { depth--; if (!depth) break; }
            lex_pos++;
        }
        int raw_pos_after_inc = lex_pos;
        /* Restore and re-lex the increment */
        lex_pos = raw_pos_before_inc;
        cur_tok = lex_next();
        peek_valid = 0;

        if (cur_tok.type != T_RPAREN) {
            parse_expr(); if (compile_error) return;
        }

        /* jmp back to cond_start */
        emit_jmp();
        patch_i32(emit_pos-4, (int32_t)(cond_start - emit_pos));

        /* Patch jmp_over_inc to here (body start) */
        patch_i32(jmp_over_inc, (int32_t)(emit_pos - (jmp_over_inc+4)));

        /* Advance past ')' */
        lex_pos = raw_pos_after_inc;
        if (lex_src[lex_pos]==')') lex_pos++;
        cur_tok = lex_next();
        peek_valid = 0;

        /* Body */
        parse_stmt(); if (compile_error) return;

        /* jmp to inc_start */
        emit_jmp();
        patch_i32(emit_pos-4, (int32_t)(inc_start - emit_pos));

        /* Patch jz to here (end) */
        if (has_cond)
            patch_i32(jz_pos, (int32_t)(emit_pos - (jz_pos+4)));

        sym_restore(snap);
        return;
    }

    /* Expression statement */
    parse_expr(); if (compile_error) return;
    expect(T_SEMI, "expected ';'");
}

static void parse_block(void) {
    expect(T_LBRACE, "expected '{'");
    int snap = sym_count;
    while (cur_tok.type != T_RBRACE && cur_tok.type != T_EOF && !compile_error)
        parse_stmt();
    sym_restore(snap);
    expect(T_RBRACE, "expected '}'");
}

/* ── Function definition ─────────────────────────────────────────────────── */

static void parse_funcdef(const char *fname) {
    /* Record function address BEFORE emitting prologue */
    sym_t *fn = sym_find(fname);
    if (!fn) fn = sym_add(fname, SYM_FUNC, 0);
    if (!fn) return;
    fn->kind      = SYM_FUNC;
    fn->func_addr = emit_buf + emit_pos;
    fn->used      = 1;

    int scope_snap = sym_count;
    local_count = 0;
    frame_size  = 0;

    /* Prologue */
    emit_push_rbp();
    emit_mov_rbp_rsp();

    /* Reserve space for locals – we'll patch this later */
    emit_sub_rsp_imm32(0);
    size_t frame_patch = emit_pos - 4;

    /* Parse parameters: '(' already consumed */
    int param_idx = 0;
    static const int8_t param_regs[] = {
        /* rdi=0x3F? no – we do them manually */
        0,1,2,3,4,5
    };
    if (cur_tok.type != T_RPAREN) {
        do {
            if (!is_type_token(cur_tok.type)) break;
            advance();
            if (cur_tok.type == T_STAR) advance();
            if (cur_tok.type != T_IDENT) break;
            char pname[MAX_IDENT];
            kstrncpy(pname, cur_tok.sval, MAX_IDENT-1);
            advance();

            local_count++;
            int offset = -(local_count * 8);
            sym_add(pname, SYM_PARAM, offset);

            /* Store the register argument onto the stack */
            /* mov [rbp+offset], rdi/rsi/... */
            switch (param_idx) {
            case 0: emit3(0x48,0x89,0x7D); emit1((uint8_t)(int8_t)offset); break; /* mov [rbp+off],rdi */
            case 1: emit3(0x48,0x89,0x75); emit1((uint8_t)(int8_t)offset); break; /* mov [rbp+off],rsi */
            case 2: emit3(0x48,0x89,0x55); emit1((uint8_t)(int8_t)offset); break; /* mov [rbp+off],rdx */
            case 3: emit3(0x48,0x89,0x4D); emit1((uint8_t)(int8_t)offset); break; /* mov [rbp+off],rcx */
            default: break;
            }
            param_idx++;
        } while (match(T_COMMA));
    }
    expect(T_RPAREN, "expected ')'");

    /* Parse body */
    parse_block();

    /* Patch frame size */
    int total_locals = local_count * 8;
    /* Align to 16 bytes */
    total_locals = (total_locals + 15) & ~15;
    patch_i32(frame_patch, total_locals);

    /* Default return 0 (if function falls off the end) */
    emit_mov_rax_imm32(0);
    emit_mov_rsp_rbp();
    emit_pop_rbp();
    emit_ret();

    sym_restore(scope_snap);
}

/* ── Top-level parsing ───────────────────────────────────────────────────── */

static void parse_toplevel(void) {
    while (cur_tok.type != T_EOF && !compile_error) {
        if (!is_type_token(cur_tok.type)) {
            ERRC("expected type at top level"); return;
        }
        advance();
        int is_ptr = (cur_tok.type == T_STAR);
        if (is_ptr) advance();

        if (cur_tok.type != T_IDENT) { ERRC("expected identifier"); return; }
        char name[MAX_IDENT];
        kstrncpy(name, cur_tok.sval, MAX_IDENT-1);
        advance();

        if (cur_tok.type == T_LPAREN) {
            advance();
            parse_funcdef(name);
        } else {
            /* Global variable */
            /* For simplicity, global vars are stored in a static area */
            static uint64_t global_store[64];
            static int      global_count = 0;
            if (global_count < 64) {
                int g = global_count++;
                sym_t *s = sym_add(name, SYM_LOCAL, 0);
                if (s) {
                    /* We abuse rbp_offset to store the absolute address */
                    /* Instead, the "address" is stored in func_addr */
                    s->func_addr = (uint8_t *)&global_store[g];
                    s->kind = SYM_GLOBAL;
                }
            }
            if (cur_tok.type == T_ASSIGN) {
                advance();
                /* constant expression only */
                if (cur_tok.type == T_NUMBER) {
                    sym_t *s = sym_find(name);
                    if (s) *(int64_t*)s->func_addr = cur_tok.ival;
                    advance();
                }
            }
            expect(T_SEMI, "expected ';'");
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════════════════════════════════════ */

compiler_result_t tcc_compile(const char *src,
                               uint8_t    *out_buf,
                               size_t      out_size) {
    compiler_result_t res = { NULL, 0 };

    if (out_size < 16) { res.error = "buffer too small"; return res; }

    /* Reserve 12 bytes at position 0 for the entry trampoline:
     *   mov rax, imm64  (10 bytes: 48 B8 + 8)
     *   jmp rax         (2 bytes:  FF E0)
     * We fill these in AFTER parsing so all absolute pointers (strings,
     * function addresses) are already at their final locations.           */
    emit_buf      = out_buf;
    emit_cap      = out_size;
    emit_pos      = 12;          /* start emitting code after the trampoline slot */

    lex_src       = src;
    lex_pos       = 0;
    compile_error = NULL;
    sym_count     = 0;
    peek_valid    = 0;
    frame_size    = 0;
    local_count   = 0;

    /* First token */
    advance();

    /* Parse the program */
    parse_toplevel();

    if (compile_error) {
        res.error = compile_error;
        return res;
    }

    /* Find 'main' */
    sym_t *main_fn = sym_find("main");
    if (!main_fn || !main_fn->func_addr) {
        res.error = "no 'main' function defined";
        return res;
    }

    /* Write trampoline at bytes 0-11 */
    size_t saved_pos = emit_pos;
    emit_pos = 0;
    emit_mov_rax_imm64((int64_t)(uintptr_t)main_fn->func_addr);
    emit2(0xFF, 0xE0); /* jmp rax */
    /* emit_pos should now be 12 */

    res.code_size = saved_pos;
    return res;
}
