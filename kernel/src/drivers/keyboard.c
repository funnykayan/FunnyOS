#include "keyboard.h"
#include "../cpu/cpu.h"
#include "../lib/printf.h"
#include <stdint.h>
#include <stddef.h>

/* ── PS/2 ports ───────────────────────────────────────────────────────────── */
#define KBD_DATA    0x60
#define KBD_STATUS  0x64

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ── Circular key buffer ──────────────────────────────────────────────────── */
#define KBD_BUF_SIZE 256
static volatile char kbd_buf[KBD_BUF_SIZE];
static volatile int  kbd_head = 0;
static volatile int  kbd_tail = 0;

static void kbd_buf_push(char c) {
    int next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) {
        kbd_buf[kbd_head] = c;
        kbd_head = next;
    }
}

static char kbd_buf_pop(void) {
    if (kbd_tail == kbd_head) return 0;
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}

/* ── Scan code set 1 → ASCII translation ────────────────────────────────── */
static const char sc_normal[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   /* ctrl */
    'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   /* left shift */
    '\\','z','x','c','v','b','n','m',',','.','/',
    0,   /* right shift */
    '*',
    0,   /* alt */
    ' ', /* space */
    0,   /* caps lock */
    0,0,0,0,0,0,0,0,0,0,  /* F1..F10 */
    0,   /* num lock */
    0,   /* scroll lock */
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,
    0,0,                   /* F11,F12 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* ... */
};

static const char sc_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,
    'A','S','D','F','G','H','J','K','L',':','"','~',
    0,
    '|','Z','X','C','V','B','N','M','<','>','?',
    0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

static int shift_down  = 0;
static int caps_lock   = 0;
static int ctrl_down   = 0;
static int extended    = 0;   /* 1 after 0xE0 prefix byte */

/* ── Extended (0xE0) scan-code → KEY_* ───────────────────────────────────── */
static void handle_extended(uint8_t sc) {
    /* release events for extended keys: ignore (0x80 bit set) */
    if (sc & 0x80) return;
    switch (sc) {
        case 0x48: kbd_buf_push((char)KEY_UP);    break;
        case 0x50: kbd_buf_push((char)KEY_DOWN);  break;
        case 0x4B: kbd_buf_push((char)KEY_LEFT);  break;
        case 0x4D: kbd_buf_push((char)KEY_RIGHT); break;
        case 0x47: kbd_buf_push((char)KEY_HOME);  break;
        case 0x4F: kbd_buf_push((char)KEY_END);   break;
        case 0x53: kbd_buf_push((char)KEY_DEL);   break;
        case 0x49: kbd_buf_push((char)KEY_PGUP);  break;
        case 0x51: kbd_buf_push((char)KEY_PGDN);  break;
        case 0x52: kbd_buf_push((char)KEY_INS);   break;
        default:   break;
    }
}

/* ── IRQ1 handler ─────────────────────────────────────────────────────────── */
static void kbd_irq_handler(registers_t *regs) {
    (void)regs;
    uint8_t sc = inb(KBD_DATA);

    /* Extended key prefix */
    if (sc == 0xE0) { extended = 1; return; }
    if (extended)   { extended = 0; handle_extended(sc); return; }

    /* Key release: high bit set */
    if (sc & 0x80) {
        sc &= 0x7F;
        if (sc == 0x2A || sc == 0x36) shift_down = 0;
        if (sc == 0x1D) ctrl_down = 0;
        return;
    }

    /* Modifier tracking */
    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return; }
    if (sc == 0x3A) { caps_lock ^= 1; return; }
    if (sc == 0x1D) { ctrl_down = 1; return; }

    if (sc >= 128) return;

    char c;
    if (shift_down)
        c = sc_shift[sc];
    else
        c = sc_normal[sc];

    /* Apply caps lock to letters */
    if (caps_lock && c >= 'a' && c <= 'z') c -= 32;
    if (caps_lock && c >= 'A' && c <= 'Z') c += 32;

    /* Ctrl combos */
    if (ctrl_down) {
        if (c >= 'a' && c <= 'z') c -= 96;  /* Ctrl+a = 0x01 etc. */
        else if (c >= 'A' && c <= 'Z') c -= 64;
    }

    if (c) kbd_buf_push(c);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void kbd_init(void) {
    /* Flush any pending data */
    while (inb(KBD_STATUS) & 1) inb(KBD_DATA);
    irq_register(1, kbd_irq_handler);
}

char kbd_try_getchar(void) {
    return kbd_buf_pop();
}

char kbd_getchar(void) {
    char c;
    while (!(c = kbd_buf_pop())) __asm__ volatile ("hlt");
    return c;
}

int kbd_readline(char *buf, int max_len) {
    int pos = 0;
    while (pos < max_len - 1) {
        char c = kbd_getchar();

        if (c == '\n' || c == '\r') {
            kputchar('\n');
            break;
        }
        if (c == '\b') {
            if (pos > 0) {
                pos--;
                kputchar('\b');
            }
            continue;
        }
        /* Ctrl+C */
        if (c == 0x03) {
            kputs("^C\n");
            pos = 0;
            break;
        }
        if ((unsigned char)c < 32) continue;  /* ignore other control chars */

        buf[pos++] = c;
        kputchar(c);
    }
    buf[pos] = '\0';
    return pos;
}
