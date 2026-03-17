#include "disk.h"
#include "../lib/string.h"

/* ── ATA PIO primary bus registers ──────────────────────────────────────── */
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECT_CNT    0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_ALT_STATUS  0x3F6

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_FLUSH   0xE7
#define ATA_CMD_IDENT   0xEC

#define ATA_SR_BSY      0x80
#define ATA_SR_DRDY     0x40
#define ATA_SR_DRQ      0x08
#define ATA_SR_ERR      0x01

/* ── port I/O ─────────────────────────────────────────────────────────── */
static inline uint8_t  inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v;
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(port)); return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port));
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(port));
}

static int disk_present = 0;

/* Busy-wait until BSY clears (and optionally DRQ sets) */
static int ata_wait(int drq) {
    int timeout = 0x100000;
    while (--timeout) {
        uint8_t s = inb(ATA_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_BSY) continue;
        if (!drq)            return 0;
        if (s & ATA_SR_DRQ)  return 0;
    }
    return -1; /* timeout */
}

/* Set up LBA28 registers */
static void ata_setup_lba(uint32_t lba, uint8_t count) {
    outb(ATA_DRIVE,    0xE0 | ((lba >> 24) & 0x0F)); /* master + LBA */
    outb(ATA_SECT_CNT, count);
    outb(ATA_LBA_LO,   (uint8_t)(lba >>  0));
    outb(ATA_LBA_MID,  (uint8_t)(lba >>  8));
    outb(ATA_LBA_HI,   (uint8_t)(lba >> 16));
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int disk_init(void) {
    /* Select master drive */
    outb(ATA_DRIVE, 0xA0);
    /* 400 ns delay (read alt status 4×) */
    for (int i = 0; i < 4; i++) inb(ATA_ALT_STATUS);

    /* Send IDENTIFY */
    outb(ATA_COMMAND, ATA_CMD_IDENT);
    uint8_t s = inb(ATA_STATUS);
    if (s == 0x00 || s == 0xFF) {
        disk_present = 0;
        return 0; /* no drive */
    }
    if (ata_wait(1) < 0) { disk_present = 0; return 0; }
    /* drain the IDENTIFY data */
    for (int i = 0; i < 256; i++) inw(ATA_DATA);
    disk_present = 1;
    return 1;
}

int disk_read_sector(uint32_t lba, void *buf) {
    if (!disk_present) return -1;
    if (ata_wait(0)   < 0) return -1;
    ata_setup_lba(lba, 1);
    outb(ATA_COMMAND, ATA_CMD_READ);
    if (ata_wait(1)   < 0) return -1;
    uint16_t *p = (uint16_t *)buf;
    for (int i = 0; i < 256; i++) p[i] = inw(ATA_DATA);
    return 0;
}

int disk_write_sector(uint32_t lba, const void *buf) {
    if (!disk_present) return -1;
    if (ata_wait(0)   < 0) return -1;
    ata_setup_lba(lba, 1);
    outb(ATA_COMMAND, ATA_CMD_WRITE);
    if (ata_wait(1)   < 0) return -1;
    const uint16_t *p = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++) outw(ATA_DATA, p[i]);
    disk_flush();
    return 0;
}

int disk_read(uint32_t lba, void *buf, uint32_t count) {
    uint8_t *p = (uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        if (disk_read_sector(lba + i, p) < 0) return -1;
        p += DISK_SECTOR_SIZE;
    }
    return 0;
}

int disk_write(uint32_t lba, const void *buf, uint32_t count) {
    const uint8_t *p = (const uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        if (disk_write_sector(lba + i, p) < 0) return -1;
        p += DISK_SECTOR_SIZE;
    }
    return 0;
}

void disk_flush(void) {
    if (!disk_present) return;
    outb(ATA_DRIVE, 0xA0);
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    ata_wait(0);
}
