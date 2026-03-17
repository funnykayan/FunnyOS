#pragma once
#include <stdint.h>
#include <stddef.h>

/* ATA PIO driver – primary bus, master drive (LBA28)
 *
 * Disk layout (512-byte sectors):
 *   Sector 0       : superblock
 *   Sectors 1-4    : file table  (32 entries × 64 bytes = 2048 bytes)
 *   Sector 8+      : file data   (slot N → sectors 8 + N*8, 4096 bytes each)
 */

#define DISK_SECTOR_SIZE    512
#define FS_MAGIC            0x46554E4E  /* 'FUNN' */
#define FS_MAX_FILES        32
#define FS_NAME_LEN         48          /* supports paths like "dir/file.c" */
#define FS_FILE_SECTORS     8           /* 8 × 512 = 4096 bytes / file   */
#define FS_TABLE_SECTOR     1           /* file table starts at sector 1  */
#define FS_TABLE_SECTORS    6           /* 32 × 64 bytes, rounded up = 6 secs */
#define FS_DATA_START      16           /* data slots start at sector 16  */

#define FS_FLAG_USED        0x01        /* slot is occupied               */
#define FS_FLAG_DIR         0x02        /* entry is a directory           */

/* On-disk file entry (exactly 64 bytes) */
typedef struct __attribute__((packed)) {
    char     name[FS_NAME_LEN];  /* 48 */
    uint32_t size;               /* bytes used in this slot */
    uint32_t flags;              /* FS_FLAG_* bits          */
    uint32_t slot;               /* data slot index 0..31   */
    uint8_t  _pad[4];            /* pad to 64 bytes         */
} disk_entry_t;

/* Initialise + detect drive (returns 1 if drive found, 0 if not) */
int  disk_init(void);

/* Raw sector I/O */
int  disk_read_sector (uint32_t lba, void *buf);
int  disk_write_sector(uint32_t lba, const void *buf);

/* Handle Disk Errors */

/* Multi-sector helpers */
int  disk_read (uint32_t lba, void *buf, uint32_t count);
int  disk_write(uint32_t lba, const void *buf, uint32_t count);

/* Flush write cache */
void disk_flush(void);
