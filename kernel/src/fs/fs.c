#include "fs.h"
#include "../drivers/disk.h"
#include "../lib/string.h"
#include "../mm/pmm.h"
#include "../lib/printf.h"

/* ── Superblock (fits in one sector) ─────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t file_count;  /* total used slots */
    uint8_t  _pad[504];   /* pad to 512 bytes */
} superblock_t;

/* ── In-RAM file table ───────────────────────────────────────────────────── */
static fs_file_t ram_table[FS_MAX_FILES];

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static uint32_t slot_to_lba(uint32_t slot) {
    return FS_DATA_START + slot * FS_FILE_SECTORS;
}

/* Write the on-disk file table from ram_table */
static void flush_table(void) {
    static uint8_t tbl_buf[FS_TABLE_SECTORS * DISK_SECTOR_SIZE];
    kmemset(tbl_buf, 0, sizeof(tbl_buf));
    disk_entry_t *de = (disk_entry_t *)tbl_buf;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!ram_table[i].used) continue;
        kstrncpy(de[i].name, ram_table[i].name, FS_NAME_LEN - 1);
        de[i].size  = (uint32_t)ram_table[i].size;
        de[i].flags = ram_table[i].flags | FS_FLAG_USED;
        de[i].slot  = (ram_table[i].flags & FS_FLAG_DIR) ? 0 : ram_table[i].slot;
    }
    disk_write(FS_TABLE_SECTOR, tbl_buf, FS_TABLE_SECTORS);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void fs_init(void) {
    kmemset(ram_table, 0, sizeof(ram_table));

    if (!disk_init()) {
        kprintf("[fs] No disk detected – files will be RAM-only\n");
        return;
    }

    /* Read superblock */
    static uint8_t sb_buf[DISK_SECTOR_SIZE];
    disk_read_sector(0, sb_buf);
    superblock_t *sb = (superblock_t *)sb_buf;

    if (sb->magic != FS_MAGIC) {
        /* First boot – format the disk */
        kprintf("[fs] Formatting disk...\n");
        kmemset(sb_buf, 0, sizeof(sb_buf));
        sb->magic      = FS_MAGIC;
        sb->file_count = 0;
        disk_write_sector(0, sb_buf);

        /* Zero the file table */
        static uint8_t zeros[FS_TABLE_SECTORS * DISK_SECTOR_SIZE];
        kmemset(zeros, 0, sizeof(zeros));
        disk_write(FS_TABLE_SECTOR, zeros, FS_TABLE_SECTORS);
        return;
    }

    /* Load file table */
    static uint8_t tbl_buf[FS_TABLE_SECTORS * DISK_SECTOR_SIZE];
    disk_read(FS_TABLE_SECTOR, tbl_buf, FS_TABLE_SECTORS);
    disk_entry_t *de = (disk_entry_t *)tbl_buf;

    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!(de[i].flags & FS_FLAG_USED)) continue;

        ram_table[i].used  = 1;
        ram_table[i].slot  = de[i].slot;
        ram_table[i].size  = de[i].size;
        ram_table[i].flags = de[i].flags;
        kstrncpy(ram_table[i].name, de[i].name, FS_NAME_LEN - 1);

        if (de[i].flags & FS_FLAG_DIR) continue; /* dirs have no data */

        /* Load file data into RAM */
        ram_table[i].data = (char *)kmalloc(FS_MAX_DATA);
        if (ram_table[i].data) {
            kmemset(ram_table[i].data, 0, FS_MAX_DATA);
            disk_read(slot_to_lba(de[i].slot),
                      ram_table[i].data,
                      FS_FILE_SECTORS);
        }
    }
}

fs_file_t *fs_find(const char *name) {
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (ram_table[i].used && kstrcmp(ram_table[i].name, name) == 0)
            return &ram_table[i];
    return NULL;
}

fs_file_t *fs_create(const char *name) {
    /* Collect used slots */
    uint8_t slot_used[FS_MAX_FILES] = {0};
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (ram_table[i].used)
            slot_used[ram_table[i].slot] = 1;

    /* Find a free RAM entry */
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (ram_table[i].used) continue;

        /* Find a free slot */
        int slot = -1;
        for (int s = 0; s < FS_MAX_FILES; s++) {
            if (!slot_used[s]) { slot = s; break; }
        }
        if (slot < 0) return NULL; /* disk full */

        kstrncpy(ram_table[i].name, name, FS_NAME_LEN - 1);
        ram_table[i].data  = (char *)kmalloc(FS_MAX_DATA);
        if (!ram_table[i].data) return NULL;
        kmemset(ram_table[i].data, 0, FS_MAX_DATA);
        ram_table[i].size  = 0;
        ram_table[i].slot  = (uint32_t)slot;
        ram_table[i].flags = FS_FLAG_USED;
        ram_table[i].used  = 1;
        flush_table();
        return &ram_table[i];
    }
    return NULL; /* table full */
}

int fs_sync(fs_file_t *f) {
    if (!f || !f->used) return -1;
    if (f->flags & FS_FLAG_DIR) { flush_table(); return 0; }
    if (!f->data) return -1;

    static uint8_t data_buf[FS_FILE_SECTORS * DISK_SECTOR_SIZE];
    kmemset(data_buf, 0, sizeof(data_buf));
    size_t sz = f->size < FS_MAX_DATA ? f->size : FS_MAX_DATA;
    kmemcpy(data_buf, f->data, sz);
    disk_write(slot_to_lba(f->slot), data_buf, FS_FILE_SECTORS);

    /* Update table */
    flush_table();
    return 0;
}

void fs_delete(const char *name) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!ram_table[i].used) continue;
        if (kstrcmp(ram_table[i].name, name) != 0) continue;

        if (!(ram_table[i].flags & FS_FLAG_DIR)) {
            /* Zero out data sectors on disk */
            static uint8_t zeros[FS_FILE_SECTORS * DISK_SECTOR_SIZE];
            kmemset(zeros, 0, sizeof(zeros));
            disk_write(slot_to_lba(ram_table[i].slot), zeros, FS_FILE_SECTORS);
            kfree(ram_table[i].data);
        }
        kmemset(&ram_table[i], 0, sizeof(ram_table[i]));
        flush_table();
        return;
    }
}

fs_file_t *fs_table(void) { return ram_table; }
int        fs_table_size(void) { return FS_MAX_FILES; }

int fs_list(void) {
    int count = 0;
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (ram_table[i].used) count++;
    return count;
}

int fs_mkdir(const char *path) {
    if (fs_find(path)) return -1; /* already exists */
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (ram_table[i].used) continue;
        kstrncpy(ram_table[i].name, path, FS_NAME_LEN - 1);
        ram_table[i].data  = NULL;
        ram_table[i].size  = 0;
        ram_table[i].slot  = 0xFFFFFFFF;
        ram_table[i].flags = FS_FLAG_USED | FS_FLAG_DIR;
        ram_table[i].used  = 1;
        flush_table();
        return 0;
    }
    return -1;
}

int fs_is_dir(const char *name) {
    fs_file_t *e = fs_find(name);
    return e && (e->flags & FS_FLAG_DIR);
}