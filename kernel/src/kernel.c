/* ============================================================
 *  FunnyOS  –  Kernel Entry  (kernel.c)
 *  Called by entry.asm after Limine sets up long mode.
 * ============================================================ */

#include "limine.h"
#include "cpu/cpu.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "terminal/terminal.h"
#include "lib/printf.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/pit.h"
#include "gui/wm.h"
#include "fs/fs.h"
#include "shell/shell.h"
#include <stdint.h>
#include <stddef.h>

/* ── Limine requests ─────────────────────────────────────────────────────── */

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_bootloader_info_request bootinfo_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST,
    .revision = 0
};

/* Required Limine base revision */
__attribute__((used, section(".requests_start_marker")))
static volatile uint64_t limine_base_revision[3] = {
    0xf9562b2d5c95a6c8ULL,
    0x6a7b384944536bdcULL,
    2
};

__attribute__((used, section(".requests_end_marker")))
static volatile uint64_t limine_end_marker[2] = {
    0xadc0e0531bb10d03ULL,
    0x9572709f31764c62ULL
};

/* ── Framebuffer globals (used by terminal.c) ────────────────────────────── */
void    *fb_addr  = NULL;
uint64_t fb_width  = 0;
uint64_t fb_height = 0;
uint64_t fb_pitch  = 0;
uint8_t  fb_bpp    = 32;

/* ── Panic ───────────────────────────────────────────────────────────────── */
__attribute__((noreturn))
void kpanic(const char *msg) {
    /* If terminal is up, print the panic message */
    term_set_color(COLOR_RED, COLOR_BLACK);
    kprintf("\n\n  *** KERNEL PANIC ***\n  %s\n", msg);
    kprintf("  System halted.\n");
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

/* ── Terminal repaint callback (used by WM) ──────────────────────────────── */
static void term_repaint_cb(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
    term_blit_to_fb();
}

/* ── Kernel main ─────────────────────────────────────────────────────────── */
void kmain(void) {
    /* 1. Validate framebuffer */
    if (!fb_request.response || fb_request.response->framebuffer_count == 0) {
        /* No display, just halt */
        __asm__ volatile ("cli; hlt");
    }

    struct limine_framebuffer *fb = fb_request.response->framebuffers[0];
    fb_addr   = fb->address;
    fb_width  = fb->width;
    fb_height = fb->height;
    fb_pitch  = fb->pitch;
    fb_bpp    = fb->bpp;

    /* 2. Initialise terminal (framebuffer text renderer) */
    term_init();

    /* 3. Print early boot message */
    term_set_color(COLOR_CYAN, COLOR_BLACK);
    kprintf("[BOOT] FunnyOS starting...\n");
    term_set_color(COLOR_GREEN, COLOR_BLACK);

    /* 4. GDT */
    kprintf("[CPU]  Loading GDT...\n");
    gdt_init();

    /* 5. IDT + PIC */
    kprintf("[CPU]  Loading IDT & remapping PIC...\n");
    idt_init();

    /* 6. Memory manager */
    uint64_t hhdm_off = hhdm_request.response ? hhdm_request.response->offset : 0;
    kprintf("[MEM]  Initialising physical memory manager...\n");
    pmm_init(hhdm_off);
    kprintf("[MEM]  Total RAM: %ld MiB\n", pmm_total_mem() / (1024*1024));
    kprintf("[HEAP] Initialising kernel heap...\n");
    heap_init();

    /* 7. Keyboard + mouse */
    kprintf("[KBD]  PS/2 keyboard driver ready.\n");
    kbd_init();
    kprintf("[MOUSE] PS/2 mouse driver ready.\n");
    mouse_init();
    kprintf("[PIT]  Programmable Interval Timer at 100 Hz.\n");
    pit_init(100);
    __asm__ volatile ("sti");

    /* 8. Window manager — create shell terminal window + floating tips */
    kprintf("[WM]   Initialising window manager...\n");
    wm_init();
    {
        /* ---- shell terminal window ---- */
        window_t *term_win = wm_create(8, 8, 860, 720, "Shell  -  FunnyOS");
        if (term_win) {
            term_win->no_close        = 1;
            term_win->body_bg         = 0x0A0A0Au;
            term_win->on_paint_client = term_repaint_cb;

            int cl_x = term_win->x + WM_BORDER;
            int cl_y = term_win->y + WM_BORDER + WM_TITLEBAR_H;
            int cl_w = term_win->w - WM_BORDER * 2;
            int cl_h = term_win->h - WM_BORDER * 2 - WM_TITLEBAR_H;
            term_set_color(COLOR_GREEN, 0x0A0A0Au);
            term_set_viewport(cl_x, cl_y, cl_w, cl_h);
            wm_draw_all();
        }

        /* ---- floating tips window ---- */
        window_t *tips = wm_create(882, 8, 128, 130, "Tips");
        if (tips) {
            wm_add_line(tips, "help = cmds");
            wm_add_line(tips, "cc file.c");
            wm_add_line(tips, "run file.bin");
            wm_add_line(tips, "color cyan");
            wm_add_line(tips, "clear = redraw");
        }
        mouse_set_callback(wm_on_mouse);
    }

    /* 8. Disk + filesystem */
    kprintf("[FS]   Initialising disk filesystem...\n");
    fs_init();

    /* 8. Boot info */
    if (bootinfo_request.response) {
        kprintf("[BOOT] Bootloader: %s %s\n",
                bootinfo_request.response->name,
                bootinfo_request.response->version);
    }

    kprintf("[BOOT] Kernel loaded at 0x%p\n", (void*)&kmain);

    /* 9. Hand off to the shell */
    shell_run();

    /* Should never reach here */
    kpanic("shell returned unexpectedly");
}
