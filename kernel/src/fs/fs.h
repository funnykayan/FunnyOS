#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../drivers/disk.h"

#define FS_MAX_DATA     4096   /* max bytes per file (= FS_FILE_SECTORS × 512) */

/* In-RAM representation of an open/cached file */
typedef struct {
    char     name[FS_NAME_LEN];
    char    *data;      /* kmalloc'd buffer, FS_MAX_DATA bytes */
    size_t   size;
    uint32_t slot;      /* on-disk data slot index */
    uint32_t flags;     /* FS_FLAG_* bits           */
    int      used;
} fs_file_t;

/* Initialise FS (format if magic missing, then load all files into RAM) */
void fs_init(void);

/* Find a file by name (returns pointer to in-RAM entry or NULL) */
fs_file_t *fs_find(const char *name);

/* Create a new file (allocated in RAM + a free disk slot reserved) */
fs_file_t *fs_create(const char *name);

/* Create a directory entry */
int        fs_mkdir(const char *path);

/* Returns 1 if entry is a directory */
int        fs_is_dir(const char *name);

/* Flush a file's in-RAM data to disk */
int  fs_sync(fs_file_t *f);

/* Delete a file from RAM and disk */
void fs_delete(const char *name);

/* List all files: fills names[] and sizes[]; returns count */
int  fs_list(void);

/* Direct access to the in-RAM table (for ls, etc.) */
fs_file_t *fs_table(void);
int        fs_table_size(void);
