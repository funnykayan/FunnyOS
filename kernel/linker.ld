/* ============================================================
 *  FunnyOS  –  Linker Script for x86-64 Limine kernel
 * ============================================================ */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS {
    requests PT_LOAD FLAGS(5);   /* R-X: Limine request page */
    text     PT_LOAD FLAGS(5);   /* R-X */
    rodata   PT_LOAD FLAGS(4);   /* R-- */
    data     PT_LOAD FLAGS(6);   /* RW- */
}

SECTIONS {
    /* Higher-half kernel: Limine maps us here */
    . = 0xffffffff80000000;

    /* ── Limine request markers (must be page-aligned) ── */
    .requests_start_marker : {
        KEEP(*(.requests_start_marker))
    } :requests

    .requests : {
        KEEP(*(.requests))
    } :requests

    .requests_end_marker : {
        KEEP(*(.requests_end_marker))
    } :requests

    /* ── Text (code) ── */
    . = ALIGN(0x1000);
    .text : {
        *(.text._start)
        *(.text*)
    } :text

    /* ── Read-only data ── */
    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata*)
    } :rodata

    /* ── Initialised data ── */
    . = ALIGN(0x1000);
    .data : {
        *(.data*)
    } :data

    /* ── BSS (zero-initialised) ── */
    .bss : {
        *(COMMON)
        *(.bss*)
    } :data

    /* Strip debug/unneeded sections from the final binary */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment*)
        *(.gnu*)
    }
}
