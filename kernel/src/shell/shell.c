#include "shell.h"
#include "../lib/printf.h"
#include "../lib/string.h"
#include "../terminal/terminal.h"
#include "../drivers/keyboard.h"
#include "../drivers/pit.h"
#include "../drivers/rtc.h"
#include "../mm/pmm.h"
#include "../mm/heap.h"
#include "../compiler/compiler.h"
#include "../fs/fs.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Line editor with history                                                   */
/* ═══════════════════════════════════════════════════════════════════════════ */
#define HISTORY_SIZE  16
#define LINE_MAX     256

static char history[HISTORY_SIZE][LINE_MAX];
static int  hist_head = 0;
static int  hist_count = 0;

static void hist_push(const char *line) {
    if (!*line) return;
    kstrncpy(history[hist_head], line, LINE_MAX - 1);
    hist_head  = (hist_head + 1) % HISTORY_SIZE;
    if (hist_count < HISTORY_SIZE) hist_count++;
}

/* ── Current working directory + shell color ─────────────────────────────── */
static char     cwd[64]   = "";  /* "" = root, "docs" = /docs */
static uint32_t shell_fg  = COLOR_GREEN;

/* -- Environment variables ------------------------------------------------ */
#define ENV_MAX   32
#define ENV_KLEN  32
#define ENV_VLEN  128
static char env_keys[ENV_MAX][ENV_KLEN];
static char env_vals[ENV_MAX][ENV_VLEN];
static int  env_count = 0;

static const char *senv_get(const char *key) {
    for (int i = 0; i < env_count; i++)
        if (kstrcmp(env_keys[i], key) == 0) return env_vals[i];
    return 0;
}

static void senv_set(const char *key, const char *val) {
    for (int i = 0; i < env_count; i++) {
        if (kstrcmp(env_keys[i], key) == 0) {
            kstrncpy(env_vals[i], val, ENV_VLEN - 1);
            return;
        }
    }
    if (env_count < ENV_MAX) {
        kstrncpy(env_keys[env_count], key, ENV_KLEN - 1);
        kstrncpy(env_vals[env_count], val, ENV_VLEN - 1);
        env_count++;
    }
}

static void senv_unset(const char *key) {
    for (int i = 0; i < env_count; i++) {
        if (kstrcmp(env_keys[i], key) == 0) {
            env_count--;
            kstrncpy(env_keys[i], env_keys[env_count], ENV_KLEN - 1);
            kstrncpy(env_vals[i], env_vals[env_count], ENV_VLEN - 1);
            return;
        }
    }
}

static void resolve_path(const char *name, char *out, size_t outsz) {
    /* If name starts with '/' or cwd is root, use name as-is */
    if (name[0] == '/' || cwd[0] == '\0') {
        kstrncpy(out, name[0] == '/' ? name + 1 : name, outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }
    kstrncpy(out, cwd, outsz - 1);
    size_t clen = kstrlen(out);
    if (clen + 1 < outsz) { out[clen++] = '/'; out[clen] = '\0'; }
    kstrncpy(out + clen, name, outsz - clen - 1);
    out[outsz - 1] = '\0';
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Command handlers                                                            */
/* ═══════════════════════════════════════════════════════════════════════════ */

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    term_set_color(COLOR_CYAN, COLOR_BLACK);
    kprintf("FunnyOS Shell  --  commands\n");
    kprintf("------------------------------------------\n");
    term_set_color(COLOR_GREEN, COLOR_BLACK);
    kprintf("  Files\n");
    kprintf("    ls [dir]           List directory\n");
    kprintf("    cat/head/wc/nl/sort/hexdump  <file>\n");
    kprintf("    write/append <file>  Write/append (end .\\n)\n");
    kprintf("    rm/cp/mv/touch/mkdir/cd/pwd\n");
    kprintf("    grep <pat> <file>   Find lines matching pat\n");
    kprintf("    find <name>         Find file by name\n");
    kprintf("    stat <file>         File metadata\n");
    kprintf("    df                  Disk usage\n");
    kprintf("  Shell\n");
    kprintf("    echo <text>         Print ($VAR expanded)\n");
    kprintf("    set  KEY=VALUE      Set env variable\n");
    kprintf("    unset KEY           Remove env variable\n");
    kprintf("    env                 List env variables\n");
    kprintf("    calc <expr>         Integer math\n");
    kprintf("    history             Command history\n");
    kprintf("    repeat N CMD        Run N times\n");
    kprintf("    color <name>        Shell text color\n");
    kprintf("    time CMD            Time a command\n");
    kprintf("    clear               Clear screen\n");
    kprintf("  System\n");
    kprintf("    date                Current date and time\n");
    kprintf("    uptime              Time since boot\n");
    kprintf("    sleep N             Sleep N seconds\n");
    kprintf("    ps                  Process list\n");
    kprintf("    meminfo             RAM and heap stats\n");
    kprintf("    uname               OS identification\n");
    kprintf("    about               OS info\n");
    kprintf("    reboot / shutdown / halt\n");
    kprintf("  Compiler\n");
    kprintf("    cc  <file>          Compile C source\n");
    kprintf("    run <file>          Run binary\n");
    kprintf("  Keys: arrows=history+cursor  Tab=complete\n");
}

static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    term_clear();
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) kputchar(' ');
        kputs(argv[i]);
    }
    kputchar('\n');
}

static void cmd_ls(int argc, char **argv) {
    (void)argc; (void)argv;
    fs_file_t *tbl = fs_table();
    int n = fs_table_size();
    int found = 0;
    size_t plen = kstrlen(cwd);
    for (int i = 0; i < n; i++) {
        if (!tbl[i].used) continue;
        const char *name = tbl[i].name;
        if (plen > 0) {
            if (kstrncmp(name, cwd, plen) != 0 || name[plen] != '/') continue;
            name += plen + 1;  /* skip "cwd/" prefix */
        }
        if (kstrchr(name, '/') != NULL) continue; /* only direct children */
        if (tbl[i].flags & FS_FLAG_DIR)
            kprintf("  %-24s  <dir>\n", name);
        else
            kprintf("  %-24s  %d bytes\n", name, (int)tbl[i].size);
        found++;
    }
    if (!found) kprintf("  (empty)\n");
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: cat [file]\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *e = fs_find(path);
    if (!e) { kprintf("cat: %s: no such file\n", argv[1]); return; }
    if (e->flags & FS_FLAG_DIR) { kprintf("cat: %s: is a directory\n", argv[1]); return; }
    kputs(e->data);
    if (e->size && e->data[e->size - 1] != '\n') kputchar('\n');
}

static void cmd_write(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: write [file]\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *e = fs_find(path);
    if (!e) e = fs_create(path);
    if (!e) { kprintf("write: filesystem full\n"); return; }

    kprintf("-- Enter text. Finish with a single '.' on an empty line --\n");
    e->size = 0;
    char line_buf[LINE_MAX];
    while (1) {
        kprintf("> ");
        kbd_readline(line_buf, LINE_MAX);
        if (kstrcmp(line_buf, ".") == 0) break;
        size_t len = kstrlen(line_buf);
        if (e->size + len + 1 < FS_MAX_DATA) {
            kmemcpy(e->data + e->size, line_buf, len);
            e->size += len;
            e->data[e->size++] = '\n';
        } else {
            kprintf("write: file size limit reached\n");
            break;
        }
    }
    e->data[e->size] = '\0';
    fs_sync(e);
    kprintf("Wrote %d bytes to '%s'\n", (int)e->size, path);
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: rm [file]\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    if (!fs_find(path)) { kprintf("rm: %s: no such file\n", argv[1]); return; }
    fs_delete(path);
    kprintf("Deleted '%s'\n", path);
}

static void cmd_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t total = pmm_total_mem();
    uint64_t used  = pmm_used_mem();
    kprintf("Physical RAM\n");
    kprintf("  Total : %8ld KiB\n", total / 1024);
    kprintf("  Used  : %8ld KiB\n", used  / 1024);
    kprintf("  Free  : %8ld KiB\n", (total - used) / 1024);
    size_t ht, hu, hf;
    heap_stats(&ht, &hu, &hf);
    kprintf("Kernel Heap (%ld KiB pool)\n", (long)(ht / 1024));
    kprintf("  Used  : %8ld B\n",  (long)hu);
    kprintf("  Free  : %8ld B\n",  (long)hf);
}

static void cmd_about(int argc, char **argv) {
    (void)argc; (void)argv;
    term_set_color(COLOR_CYAN, COLOR_BLACK);
    kprintf("\n");
    kprintf("  ______                           ___  ___ \n");
    kprintf(" |  ____|                         / _ \\/ __|\n");
    kprintf(" | |__ _   _ _ __  _ __  _   _  | | | \\__ \\\n");
    kprintf(" |  __| | | | '_ \\| '_ \\| | | | | | | |__) |\n");
    kprintf(" | |  | |_| | | | | | | | |_| | | |_| / __/ \n");
    kprintf(" |_|   \\__,_|_| |_|_| |_|\\__, |  \\___/___|  \n");
    kprintf("                           __/ |             \n");
    kprintf("                          |___/              \n");
    kprintf("\n");
    term_set_color(COLOR_GREEN, COLOR_BLACK);
    kprintf("  FunnyOS v1.0  -  A minimal x86-64 kernel\n");
    kprintf("  Boot:   Limine bootloader\n");
    kprintf("  Arch:   x86-64 (long mode)\n");
    kprintf("  Built:  C + NASM    Toolchain: clang/ld.lld\n");
    kprintf("  Shell:  FunnyShell  Commands: 42\n");
    kprintf("  Disk:   ATA PIO  FS: FunnyFS\n");
    kprintf("  GUI:    Software framebuffer WM\n");
    kprintf("\n");
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("Rebooting...\n");
    /* Triple fault or PS/2 reset */
    uint8_t v = 0;
    while (v & 2) {
        __asm__ volatile ("inb $0x64, %0" : "=a"(v));
    }
    __asm__ volatile ("outb %0, $0x64" : : "a"((uint8_t)0xFE));
    /* fallback – triple fault */
    __asm__ volatile ("lidt 0; int3");
}

static void cmd_halt(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("System halted. It is safe to power off.\n");
    __asm__ volatile ("cli; hlt");
}

static void cmd_cc(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: cc [source_file]\n"); return; }
    char src_path[64]; resolve_path(argv[1], src_path, sizeof(src_path));
    fs_file_t *src = fs_find(src_path);
    if (!src) { kprintf("cc: %s: no such file\n", argv[1]); return; }

    char out_name[FS_NAME_LEN];
    kstrncpy(out_name, src_path, FS_NAME_LEN - 5);
    char *dot = kstrrchr(out_name, '.');
    if (dot) kstrcpy(dot, ".bin");
    else      kstrcat(out_name, ".bin");

    fs_file_t *dst = fs_find(out_name);
    if (!dst) dst = fs_create(out_name);
    if (!dst) { kprintf("cc: filesystem full\n"); return; }

    kprintf("Compiling '%s' -> '%s'...\n", src_path, out_name);
    compiler_result_t res = tcc_compile(src->data, (uint8_t *)dst->data, FS_MAX_DATA);
    if (res.error) {
        term_set_color(COLOR_RED, COLOR_BLACK);
        kprintf("cc: error: %s\n", res.error);
        term_set_color(COLOR_GREEN, COLOR_BLACK);
        fs_delete(out_name);
    } else {
        dst->size = res.code_size;
        fs_sync(dst);
        kprintf("OK  –  %d bytes -> '%s'.  Run with: run %s\n",
                (int)res.code_size, out_name, out_name);
    }
}

static void cmd_run(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: run [file]\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *e = fs_find(path);
    if (!e) { kprintf("run: %s: no such file\n", argv[1]); return; }

    kprintf("Running '%s'...\n", argv[1]);

    /* Cast buffer to a function pointer and call it.
     * The compiled code must follow the cdecl calling convention and
     * must return an int exit code. */
    typedef int (*fn_t)(void);
    fn_t fn = (fn_t)(void *)e->data;

    /* Make memory executable.  Because we have no paging enforcement, the
     * memory is already executable in this flat kernel mapping.           */
    int ret = fn();
    kprintf("\n[Process exited with code %d]\n", ret);
}

/* ─── pwd ─────────────────────────────────────────────────────────────────── */
static void cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("/%s\n", cwd);
}

/* ─── cd ──────────────────────────────────────────────────────────────────── */
static void cmd_cd(int argc, char **argv) {
    if (argc < 2 || kstrcmp(argv[1], "/") == 0) {
        cwd[0] = '\0';
        return;
    }
    if (kstrcmp(argv[1], "..") == 0) {
        /* strip last component from cwd */
        char *sl = kstrrchr(cwd, '/');
        if (sl) *sl = '\0';
        else     cwd[0] = '\0';
        return;
    }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    if (!fs_is_dir(path)) {
        kprintf("cd: %s: not a directory\n", argv[1]);
        return;
    }
    kstrncpy(cwd, path, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';
}

/* ─── mkdir ───────────────────────────────────────────────────────────────── */
static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: mkdir <dir>\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    if (fs_find(path)) { kprintf("mkdir: %s: already exists\n", argv[1]); return; }
    if (!fs_mkdir(path)) { kprintf("mkdir: %s: failed (fs full?)\n", argv[1]); return; }
    kprintf("Directory '%s' created.\n", path);
}

/* ─── touch ───────────────────────────────────────────────────────────────── */
static void cmd_touch(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: touch <file>\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *f = fs_find(path);
    if (!f) {
        f = fs_create(path);
        if (!f) { kprintf("touch: %s: failed (fs full?)\n", argv[1]); return; }
        fs_sync(f);
        kprintf("Created '%s'.\n", path);
    } else {
        kprintf("'%s' already exists.\n", path);
    }
}

/* ─── cp ──────────────────────────────────────────────────────────────────── */
static void cmd_cp(int argc, char **argv) {
    if (argc < 3) { kprintf("Usage: cp <src> <dst>\n"); return; }
    char sp[64], dp[64];
    resolve_path(argv[1], sp, sizeof(sp));
    resolve_path(argv[2], dp, sizeof(dp));
    fs_file_t *src = fs_find(sp);
    if (!src) { kprintf("cp: %s: no such file\n", argv[1]); return; }
    if (fs_is_dir(sp)) { kprintf("cp: %s: is a directory\n", argv[1]); return; }
    fs_file_t *dst = fs_find(dp);
    if (!dst) dst = fs_create(dp);
    if (!dst) { kprintf("cp: %s: failed (fs full?)\n", argv[2]); return; }
    uint32_t sz = src->size > FS_MAX_DATA ? FS_MAX_DATA : src->size;
    kmemcpy(dst->data, src->data, sz);
    dst->size = sz;
    fs_sync(dst);
    kprintf("Copied '%s' -> '%s' (%d bytes)\n", sp, dp, (int)sz);
}

/* ─── mv ──────────────────────────────────────────────────────────────────── */
static void cmd_mv(int argc, char **argv) {
    if (argc < 3) { kprintf("Usage: mv <src> <dst>\n"); return; }
    char sp[64], dp[64];
    resolve_path(argv[1], sp, sizeof(sp));
    resolve_path(argv[2], dp, sizeof(dp));
    fs_file_t *src = fs_find(sp);
    if (!src) { kprintf("mv: %s: no such file\n", argv[1]); return; }
    if (fs_find(dp)) { kprintf("mv: %s: destination exists\n", argv[2]); return; }
    kstrncpy(src->name, dp, FS_NAME_LEN - 1);
    src->name[FS_NAME_LEN - 1] = '\0';
    fs_sync(src);
    kprintf("Renamed '%s' -> '%s'\n", sp, dp);
}

/* ─── hexdump ─────────────────────────────────────────────────────────────── */
static void cmd_hexdump(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: hexdump <file>\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *f = fs_find(path);
    if (!f) { kprintf("hexdump: %s: no such file\n", argv[1]); return; }
    uint32_t sz = f->size > 256 ? 256 : f->size;
    const unsigned char *p = (const unsigned char *)f->data;
    for (uint32_t i = 0; i < sz; i += 16) {
        kprintf("%04x  ", (unsigned)i);
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < sz) kprintf("%02x ", (unsigned)p[i+j]);
            else             kprintf("   ");
            if (j == 7) kprintf(" ");
        }
        kprintf(" |");
        for (uint32_t j = 0; j < 16 && i + j < sz; j++) {
            unsigned char c = p[i+j];
            kprintf("%c", (c >= 0x20 && c < 0x7f) ? (char)c : '.');
        }
        kprintf("|\n");
    }
    if (f->size > 256) kprintf("(showing first 256 of %d bytes)\n", (int)f->size);
}

/* ─── wc ──────────────────────────────────────────────────────────────────── */
static void cmd_wc(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: wc <file>\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *f = fs_find(path);
    if (!f) { kprintf("wc: %s: no such file\n", argv[1]); return; }
    int lines = 0, words = 0, bytes = (int)f->size;
    int in_word = 0;
    for (uint32_t i = 0; i < f->size; i++) {
        char c = f->data[i];
        if (c == '\n') lines++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { in_word = 0; }
        else if (!in_word) { in_word = 1; words++; }
    }
    kprintf("%4d %4d %4d %s\n", lines, words, bytes, argv[1]);
}

/* ─── head ────────────────────────────────────────────────────────────────── */
static void cmd_head(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: head <file> [lines]\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *f = fs_find(path);
    if (!f) { kprintf("head: %s: no such file\n", argv[1]); return; }
    int max_lines = (argc >= 3) ? katoi(argv[2]) : 10;
    if (max_lines <= 0) max_lines = 10;
    int cur = 0;
    for (uint32_t i = 0; i < f->size && cur < max_lines; i++) {
        char c = f->data[i];
        kprintf("%c", c);
        if (c == '\n') cur++;
    }
}

/* ─── history ─────────────────────────────────────────────────────────────── */
static void cmd_history(int argc, char **argv) {
    (void)argc; (void)argv;
    for (int i = 0; i < HISTORY_SIZE; i++) {
        int idx = (hist_head + i) % HISTORY_SIZE;
        if (history[idx][0])
            kprintf("%3d  %s\n", i + 1, history[idx]);
    }
}

/* ─── calc (recursive-descent expression evaluator) ──────────────────────── */
static const char *calc_p;

static void calc_skip(void) {
    while (*calc_p == ' ' || *calc_p == '\t') calc_p++;
}

/* forward declaration */
static long long calc_expr(void);

static long long calc_primary(void) {
    calc_skip();
    if (*calc_p == '(') {
        calc_p++;
        long long v = calc_expr();
        calc_skip();
        if (*calc_p == ')') calc_p++;
        return v;
    }
    if (*calc_p == '-') { calc_p++; return -calc_primary(); }
    if (*calc_p == '+') { calc_p++; return  calc_primary(); }
    long long v = 0, neg = 0;
    if (*calc_p == '-') { neg = 1; calc_p++; }
    while (*calc_p >= '0' && *calc_p <= '9') v = v * 10 + (*calc_p++ - '0');
    return neg ? -v : v;
}

static long long calc_mul(void) {
    long long left = calc_primary();
    while (1) {
        calc_skip();
        char op = *calc_p;
        if (op != '*' && op != '/' && op != '%') break;
        calc_p++;
        long long right = calc_primary();
        if      (op == '*') left *= right;
        else if (op == '/') left = right ? left / right : 0;
        else                left = right ? left % right : 0;
    }
    return left;
}

static long long calc_expr(void) {
    long long left = calc_mul();
    while (1) {
        calc_skip();
        char op = *calc_p;
        if (op != '+' && op != '-') break;
        calc_p++;
        long long right = calc_mul();
        left = (op == '+') ? left + right : left - right;
    }
    return left;
}

static void cmd_calc(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: calc <expression>\n"); return; }
    /* Reconstruct the expression (argv split on spaces) */
    static char expr_buf[128];
    expr_buf[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) kstrcat(expr_buf, " ");
        kstrncat(expr_buf, argv[i], sizeof(expr_buf) - kstrlen(expr_buf) - 2);
    }
    calc_p = expr_buf;
    long long result = calc_expr();
    kprintf("%lld\n", result);
}

/* ─── forward decl so repeat can call dispatch ────────────────────────────── */
static void dispatch(char *line);

/* ─── grep <pattern> <file> ──────────────────────────────────────────────── */
static void cmd_grep(int argc, char **argv) {
    if (argc < 3) { kprintf("Usage: grep <pattern> <file>\n"); return; }
    char path[64]; resolve_path(argv[2], path, sizeof(path));
    fs_file_t *f = fs_find(path);
    if (!f) { kprintf("grep: %s: no such file\n", argv[2]); return; }
    const char *pat = argv[1];
    const char *p   = f->data;
    const char *end = f->data + f->size;
    int line_no     = 1, found = 0;
    while (p < end) {
        /* find end of this line */
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        /* check if pattern appears in [p, nl) */
        /* temporary: scan manually since line isn't NUL-terminated */
        int matched = 0;
        size_t plen = kstrlen(pat);
        for (const char *s = p; s + plen <= nl; s++) {
            if (kstrncmp(s, pat, plen) == 0) { matched = 1; break; }
        }
        if (matched) {
            term_set_color(COLOR_YELLOW, COLOR_BLACK);
            kprintf("%4d: ", line_no);
            term_set_color(shell_fg, COLOR_BLACK);
            for (const char *s = p; s < nl; s++) kputchar(*s);
            kputchar('\n');
            found++;
        }
        line_no++;
        p = nl + 1;
    }
    if (!found) kprintf("(no matches)\n");
}

/* ─── find <pattern> ─────────────────────────────────────────────────────── */
static void cmd_find(int argc, char **argv) {
    const char *pat = (argc >= 2) ? argv[1] : "";
    fs_file_t  *tbl = fs_table();
    int n           = fs_table_size();
    for (int i = 0; i < n; i++) {
        if (!tbl[i].used) continue;
        if (!*pat || kstrstr(tbl[i].name, pat)) {
            if (tbl[i].flags & FS_FLAG_DIR)
                kprintf("  <dir>  %s\n", tbl[i].name);
            else
                kprintf("  %s\n", tbl[i].name);
        }
    }
}

/* ─── df (disk free) ─────────────────────────────────────────────────────── */
static void cmd_df(int argc, char **argv) {
    (void)argc; (void)argv;
    fs_file_t *tbl = fs_table();
    int n          = fs_table_size();
    int used_files = 0;
    uint32_t used_bytes = 0;
    for (int i = 0; i < n; i++) {
        if (!tbl[i].used) continue;
        used_files++;
        used_bytes += (uint32_t)tbl[i].size;
    }
    int free_files = FS_MAX_FILES - used_files;
    kprintf("Filesystem:  FunnyFS (flat, ATA PIO)\n");
    kprintf("Entries:     %d / %d used  (%d free)\n",
            used_files, FS_MAX_FILES, free_files);
    kprintf("Data used:   %d bytes  (%d max per file)\n",
            (int)used_bytes, FS_MAX_DATA);
    kprintf("Disk image:  4 MiB  (8192 x 512-byte sectors)\n");
}

/* ─── stat <file> ────────────────────────────────────────────────────────── */
static void cmd_stat(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: stat <file>\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *f = fs_find(path);
    if (!f) { kprintf("stat: %s: no such file\n", argv[1]); return; }
    const char *type = (f->flags & FS_FLAG_DIR) ? "directory" : "regular file";
    kprintf("  Name:  %s\n",  f->name);
    kprintf("  Type:  %s\n",  type);
    kprintf("  Size:  %d bytes\n", (int)f->size);
    kprintf("  Slot:  %d\n",  (int)f->slot);
    kprintf("  Flags: 0x%02x\n", (unsigned)f->flags);
}

/* ─── append <file> <text...> ────────────────────────────────────────────── */
static void cmd_append(int argc, char **argv) {
    if (argc < 3) { kprintf("Usage: append <file> <text...>\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *f = fs_find(path);
    if (!f) {
        f = fs_create(path);
        if (!f) { kprintf("append: %s: failed\n", argv[1]); return; }
    }
    if (f->flags & FS_FLAG_DIR) { kprintf("append: %s: is a directory\n", argv[1]); return; }
    for (int i = 2; i < argc; i++) {
        size_t wlen = kstrlen(argv[i]);
        if (f->size + wlen + 2 > FS_MAX_DATA) {
            kprintf("append: file full\n"); break;
        }
        if (i > 2) f->data[f->size++] = ' ';
        kmemcpy(f->data + f->size, argv[i], wlen);
        f->size += wlen;
    }
    if (f->size < FS_MAX_DATA) f->data[f->size++] = '\n';
    f->data[f->size] = '\0';
    fs_sync(f);
    kprintf("Appended to '%s'.\n", path);
}

/* ─── nl <file> (number lines) ───────────────────────────────────────────── */
static void cmd_nl(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: nl <file>\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *f = fs_find(path);
    if (!f) { kprintf("nl: %s: no such file\n", argv[1]); return; }
    int line = 1;
    const char *p   = f->data;
    const char *end = f->data + f->size;
    term_set_color(COLOR_YELLOW, COLOR_BLACK);
    kprintf("%6d  ", line++);
    term_set_color(shell_fg, COLOR_BLACK);
    while (p < end) {
        kputchar(*p);
        if (*p == '\n' && p + 1 < end) {
            term_set_color(COLOR_YELLOW, COLOR_BLACK);
            kprintf("%6d  ", line++);
            term_set_color(shell_fg, COLOR_BLACK);
        }
        p++;
    }
    kputchar('\n');
}

/* ─── sort <file> ────────────────────────────────────────────────────────── */
#define SORT_MAX_LINES 128
static void cmd_sort(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: sort <file>\n"); return; }
    char path[64]; resolve_path(argv[1], path, sizeof(path));
    fs_file_t *f = fs_find(path);
    if (!f) { kprintf("sort: %s: no such file\n", argv[1]); return; }

    static char  sort_buf[FS_MAX_DATA + 1];
    static char *ptrs[SORT_MAX_LINES];
    uint32_t sz = f->size < FS_MAX_DATA ? f->size : FS_MAX_DATA;
    kmemcpy(sort_buf, f->data, sz);
    sort_buf[sz] = '\0';

    /* split lines */
    int nlines = 0;
    ptrs[nlines++] = sort_buf;
    for (uint32_t i = 0; i < sz && nlines < SORT_MAX_LINES; i++) {
        if (sort_buf[i] == '\n') {
            sort_buf[i] = '\0';
            if (i + 1 < sz) ptrs[nlines++] = sort_buf + i + 1;
        }
    }

    /* insertion sort */
    for (int i = 1; i < nlines; i++) {
        char *key = ptrs[i];
        int j = i - 1;
        while (j >= 0 && kstrcmp(ptrs[j], key) > 0) {
            ptrs[j + 1] = ptrs[j];
            j--;
        }
        ptrs[j + 1] = key;
    }

    for (int i = 0; i < nlines; i++) {
        if (*ptrs[i] || i < nlines - 1)
            kprintf("%s\n", ptrs[i]);
    }
}

/* ─── color <name> ────────────────────────────────────────────────────────── */
static void cmd_color(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: color <green|white|cyan|yellow|red|gray>\n");
        return;
    }
    uint32_t col;
    if      (kstrcmp(argv[1], "green" ) == 0) col = COLOR_GREEN;
    else if (kstrcmp(argv[1], "white" ) == 0) col = COLOR_WHITE;
    else if (kstrcmp(argv[1], "cyan"  ) == 0) col = COLOR_CYAN;
    else if (kstrcmp(argv[1], "yellow") == 0) col = COLOR_YELLOW;
    else if (kstrcmp(argv[1], "red"   ) == 0) col = COLOR_RED;
    else if (kstrcmp(argv[1], "gray"  ) == 0) col = COLOR_GRAY;
    else { kprintf("color: unknown color '%s'\n", argv[1]); return; }
    shell_fg = col;
    term_set_color(shell_fg, COLOR_BLACK);
    kprintf("Shell color set to %s.\n", argv[1]);
}

/* ─── repeat <n> <command...> ────────────────────────────────────────────── */
static void cmd_repeat(int argc, char **argv) {
    if (argc < 3) { kprintf("Usage: repeat <n> <command...>\n"); return; }
    int n = (int)katoi(argv[1]);
    if (n <= 0 || n > 1000) { kprintf("repeat: n must be 1-1000\n"); return; }
    /* Reassemble the rest of argv into a command string */
    static char rbuf[LINE_MAX];
    rbuf[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (i > 2) kstrcat(rbuf, " ");
        kstrncat(rbuf, argv[i], sizeof(rbuf) - kstrlen(rbuf) - 2);
    }
    for (int i = 0; i < n; i++) {
        static char tmp[LINE_MAX];
        kstrncpy(tmp, rbuf, LINE_MAX - 1);
        dispatch(tmp);
    }
}

/* --- uname ---------------------------------------------------------------- */
static void cmd_uname(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("FunnyOS  1.0  x86_64  (Limine/bare-metal)\n");
}

/* --- date ----------------------------------------------------------------- */
static void cmd_date(int argc, char **argv) {
    (void)argc; (void)argv;
    rtc_time_t t;
    rtc_read(&t);
    static const char *months[] = {
        "", "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    const char *mon = (t.month >= 1 && t.month <= 12) ? months[t.month] : "???";
    kprintf("%s %s %02d %04d  %02d:%02d:%02d\n",
            rtc_weekday(t.year, t.month, t.day),
            mon, (int)t.day, (int)t.year,
            (int)t.hour, (int)t.min, (int)t.sec);
}

/* --- uptime --------------------------------------------------------------- */
static void cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t s = pit_uptime();
    uint64_t h = s / 3600;
    uint64_t m = (s % 3600) / 60;
    uint64_t sec = s % 60;
    if (h)
        kprintf("up %lld h %02lld m %02lld s\n", h, m, sec);
    else if (m)
        kprintf("up %lld m %02lld s\n", m, sec);
    else
        kprintf("up %lld s\n", sec);
}

/* --- sleep ---------------------------------------------------------------- */
static void cmd_sleep(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: sleep <seconds>\n"); return; }
    int n = (int)katoi(argv[1]);
    if (n <= 0 || n > 3600) { kprintf("sleep: 1-3600 seconds allowed\n"); return; }
    pit_sleep_ms((uint32_t)n * 1000);
}

/* --- shutdown ------------------------------------------------------------- */
static void cmd_shutdown(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("Powering off...\n");
    /* QEMU/ACPI port 0x604 value 0x2000 = S5 power off */
    __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
    /* Bochs / old QEMU */
    __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)0x0034), "Nd"((uint16_t)0xB004));
    /* Cloud Hypervisor / VirtualBox */
    __asm__ volatile ("outw %0, %1" :: "a"((uint16_t)0x2000), "Nd"((uint16_t)0x600));
    kprintf("Poweroff unsupported; use halt.\n");
    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}

/* --- env ------------------------------------------------------------------ */
static void cmd_env(int argc, char **argv) {
    (void)argc; (void)argv;
    if (env_count == 0) { kprintf("(no variables set)\n"); return; }
    for (int i = 0; i < env_count; i++)
        kprintf("%s=%s\n", env_keys[i], env_vals[i]);
}

/* --- set KEY=VALUE  or  set KEY VALUE ------------------------------------- */
static void cmd_set(int argc, char **argv) {
    if (argc == 1) { cmd_env(argc, argv); return; }
    /* find '=' in argv[1] */
    char key[ENV_KLEN], val[ENV_VLEN];
    char *eq = 0;
    for (char *p = argv[1]; *p; p++) {
        if (*p == '=') { eq = p; break; }
    }
    if (eq) {
        int kl = (int)(eq - argv[1]);
        if (kl <= 0 || kl >= ENV_KLEN) { kprintf("set: bad key\n"); return; }
        kstrncpy(key, argv[1], (size_t)kl); key[kl] = '\0';
        kstrncpy(val, eq + 1, ENV_VLEN - 1);
    } else {
        if (argc < 3) { kprintf("Usage: set KEY=VALUE  or  set KEY VALUE\n"); return; }
        kstrncpy(key, argv[1], ENV_KLEN - 1);
        kstrncpy(val, argv[2], ENV_VLEN - 1);
    }
    senv_set(key, val);
}

/* --- unset KEY ------------------------------------------------------------ */
static void cmd_unset(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: unset <KEY>\n"); return; }
    senv_unset(argv[1]);
}

/* --- time CMD ------------------------------------------------------------- */
static void cmd_time(int argc, char **argv) {
    if (argc < 2) { kprintf("Usage: time <command...>\n"); return; }
    static char tbuf[LINE_MAX];
    tbuf[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) kstrcat(tbuf, " ");
        kstrncat(tbuf, argv[i], sizeof(tbuf) - kstrlen(tbuf) - 2);
    }
    uint64_t t0 = pit_ms();
    dispatch(tbuf);
    uint64_t el = pit_ms() - t0;
    kprintf("\nreal  %lld.%03lld s\n", el / 1000ULL, el % 1000ULL);
}

/* --- ps ------------------------------------------------------------------- */
static void cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("PID  STATE   NAME\n");
    kprintf("---  ------  -----------\n");
    kprintf("  0  running kernel\n");
    kprintf("  1  running shell\n");
}

/* === Command dispatch table === */

typedef struct {
    const char *name;
    void (*fn)(int, char **);
} command_t;

static const command_t commands[] = {
    { "help",     cmd_help     },
    { "clear",    cmd_clear    },
    { "echo",     cmd_echo     },
    { "ls",       cmd_ls       },
    { "cat",      cmd_cat      },
    { "write",    cmd_write    },
    { "rm",       cmd_rm       },
    { "meminfo",  cmd_meminfo  },
    { "about",    cmd_about    },
    { "cc",       cmd_cc       },
    { "run",      cmd_run      },
    { "reboot",   cmd_reboot   },
    { "halt",     cmd_halt     },
    { "pwd",      cmd_pwd      },
    { "cd",       cmd_cd       },
    { "mkdir",    cmd_mkdir    },
    { "touch",    cmd_touch    },
    { "cp",       cmd_cp       },
    { "mv",       cmd_mv       },
    { "hexdump",  cmd_hexdump  },
    { "wc",       cmd_wc       },
    { "head",     cmd_head     },
    { "history",  cmd_history  },
    { "calc",     cmd_calc     },
    { "grep",     cmd_grep     },
    { "find",     cmd_find     },
    { "df",       cmd_df       },
    { "stat",     cmd_stat     },
    { "append",   cmd_append   },
    { "nl",       cmd_nl       },
    { "sort",     cmd_sort     },
    { "color",    cmd_color    },
    { "repeat",   cmd_repeat   },
    { "uname",    cmd_uname    },
    { "date",     cmd_date     },
    { "uptime",   cmd_uptime   },
    { "sleep",    cmd_sleep    },
    { "shutdown", cmd_shutdown },
    { "env",      cmd_env      },
    { "set",      cmd_set      },
    { "unset",    cmd_unset    },
    { "time",     cmd_time     },
    { "ps",       cmd_ps       },
    { NULL, NULL }
};

static void dispatch(char *line) {
    /* $VAR expansion --------------------------------------------------------- */
    static char expanded[LINE_MAX];
    {
        int oi = 0;
        for (int ii = 0; line[ii] && oi < LINE_MAX - 1; ) {
            if (line[ii] == '$' && line[ii + 1]) {
                ii++;
                char varname[ENV_KLEN];
                int vn = 0;
                while (line[ii] && vn < ENV_KLEN - 1 &&
                       (line[ii] == '_' ||
                        (line[ii] >= 'A' && line[ii] <= 'Z') ||
                        (line[ii] >= 'a' && line[ii] <= 'z') ||
                        (line[ii] >= '0' && line[ii] <= '9'))) {
                    varname[vn++] = line[ii++];
                }
                varname[vn] = '\0';
                if (vn > 0) {
                    const char *v = senv_get(varname);
                    if (v)
                        for (int k = 0; v[k] && oi < LINE_MAX - 1; k++) expanded[oi++] = v[k];
                } else {
                    if (oi < LINE_MAX - 1) expanded[oi++] = '$';
                }
            } else {
                expanded[oi++] = line[ii++];
            }
        }
        expanded[oi] = '\0';
        line = expanded;
    }
    char *tokens[16];
    int count = kstrsplit(line, tokens, 16);
    if (count == 0) return;

    for (const command_t *c = commands; c->name; c++) {
        if (kstrcmp(c->name, tokens[0]) == 0) {
            c->fn(count, tokens);
            return;
        }
    }

    term_set_color(COLOR_RED, COLOR_BLACK);
    kprintf("Unknown command: '%s'  (type 'help' for a list)\n", tokens[0]);
    term_set_color(COLOR_GREEN, COLOR_BLACK);
}

/* === Line editor =========================================================== */

/* Repaint the editable buffer.  Cursor is assumed to be at 'start_cur' on
 * screen when called.  After it returns, cursor is at 'new_cur'. */
static void sl_repaint(const char *buf, int len, int new_cur,
                       int start_cur, int old_len) {
    for (int i = 0; i < start_cur;  i++) kputchar('\b');  /* go to col 0 */
    for (int i = 0; i < len;        i++) kputchar(buf[i]);
    int over = old_len - len;
    if (over < 0) over = 0;
    for (int i = 0; i < over; i++) kputchar(' ');
    int endcol = len + over;
    for (int i = new_cur; i < endcol; i++) kputchar('\b');
}

/* Tab-complete the last word in buf. Updates *len_p and *cur_p. */
static void sl_tabcomplete(char *buf, int *len_p, int *cur_p, int max_len) {
    int len = *len_p;
    buf[len] = '\0';
    /* find start of last word */
    int ws = len;
    while (ws > 0 && buf[ws - 1] != ' ') ws--;
    int  plen = len - ws;
    const char *pfx = buf + ws;
    int  is_cmd = (ws == 0);
    int  mc = 0;
    const char *solo = 0;
    if (is_cmd) {
        for (const command_t *c = commands; c->name; c++)
            if (kstrncmp(c->name, pfx, (size_t)plen) == 0) { mc++; solo = c->name; }
    } else {
        fs_file_t *tbl = fs_table();
        int sz = fs_table_size();
        for (int i = 0; i < sz; i++) {
            if (!tbl[i].used) continue;
            if (kstrncmp(tbl[i].name, pfx, (size_t)plen) == 0) { mc++; solo = tbl[i].name; }
        }
    }
    if (mc == 1) {
        int slen = (int)kstrlen(solo);
        for (int i = plen; i < slen && len < max_len - 1; i++) {
            for (int j = len; j > *cur_p; j--) buf[j] = buf[j-1];
            buf[*cur_p] = solo[i]; (*cur_p)++; len++;
        }
        buf[len] = '\0';
        *len_p = len;
    } else if (mc > 1) {
        kputchar('\n');
        if (is_cmd) {
            for (const command_t *c = commands; c->name; c++)
                if (kstrncmp(c->name, pfx, (size_t)plen) == 0)
                    kprintf("  %s", c->name);
        } else {
            fs_file_t *tbl = fs_table();
            int sz = fs_table_size();
            for (int i = 0; i < sz; i++) {
                if (!tbl[i].used) continue;
                if (kstrncmp(tbl[i].name, pfx, (size_t)plen) == 0)
                    kprintf("  %s", tbl[i].name);
            }
        }
        kputchar('\n');
        *len_p = len;
    }
}

/* Full-featured line editor: history (up/dn), cursor (lt/rt/Home/End),
 * insert, Backspace, Delete, Tab-complete, Ctrl-C, Ctrl-L. */
static int shell_readline(char *buf, int max_len) {
    int len      = 0;
    int cur      = 0;
    int old_len  = 0;
    int hist_pos = hist_count;
    char hist_save[LINE_MAX];
    hist_save[0] = '\0';
    buf[0]       = '\0';

    for (;;) {
        unsigned char uc = (unsigned char)kbd_getchar();

        if (uc == '\n' || uc == '\r') {
            buf[len] = '\0'; kputchar('\n'); return len;
        }
        if (uc == 0x03) {                          /* Ctrl+C */
            kputs("^C\n"); buf[0] = '\0'; return 0;
        }
        if (uc == 0x0C) {                          /* Ctrl+L */
            term_clear();
            for (int i = 0; i < len; i++) kputchar(buf[i]);
            for (int i = cur; i < len; i++) kputchar('\b');
            old_len = len; continue;
        }

        /* -- Up arrow: older history ----------------------------------------- */
        if (uc == KEY_UP) {
            if (!hist_count) continue;
            if (hist_pos == hist_count) kstrncpy(hist_save, buf, max_len - 1);
            if (hist_pos > 0) {
                hist_pos--;
                int idx = (hist_head - hist_count + hist_pos + HISTORY_SIZE) % HISTORY_SIZE;
                int pc = cur, pl = len;
                kstrncpy(buf, history[idx], max_len - 1);
                len = cur = (int)kstrlen(buf);
                sl_repaint(buf, len, cur, pc, pl);
                old_len = len;
            }
            continue;
        }
        /* -- Down arrow: newer history -------------------------------------- */
        if (uc == KEY_DOWN) {
            if (hist_pos < hist_count) {
                int pc = cur, pl = len;
                hist_pos++;
                if (hist_pos == hist_count) kstrncpy(buf, hist_save, max_len - 1);
                else {
                    int idx = (hist_head - hist_count + hist_pos + HISTORY_SIZE) % HISTORY_SIZE;
                    kstrncpy(buf, history[idx], max_len - 1);
                }
                len = cur = (int)kstrlen(buf);
                sl_repaint(buf, len, cur, pc, pl);
                old_len = len;
            }
            continue;
        }
        if (uc == KEY_LEFT)  { if (cur > 0)   { cur--; kputchar('\b'); } continue; }
        if (uc == KEY_RIGHT) { if (cur < len) { kputchar(buf[cur]); cur++; } continue; }
        if (uc == KEY_HOME)  {
            for (int i = 0; i < cur; i++) kputchar('\b'); cur = 0; continue; }
        if (uc == KEY_END)   {
            for (int i = cur; i < len; i++) kputchar(buf[i]); cur = len; continue; }

        /* -- Delete --------------------------------------------------------- */
        if (uc == KEY_DEL) {
            if (cur < len) {
                for (int i = cur; i < len - 1; i++) buf[i] = buf[i+1];
                int pl = len; len--; buf[len] = '\0';
                sl_repaint(buf, len, cur, cur, pl);
                old_len = len;
            }
            continue;
        }
        /* -- Backspace ------------------------------------------------------ */
        if (uc == '\b' || uc == 127) {
            if (cur > 0) {
                for (int i = cur - 1; i < len - 1; i++) buf[i] = buf[i+1];
                int pl = len; len--; cur--; buf[len] = '\0';
                kputchar('\b');
                sl_repaint(buf, len, cur, cur, pl - 1);
                old_len = len;
            }
            continue;
        }
        /* -- Tab completion ------------------------------------------------- */
        if (uc == '\t') {
            int pc = cur, pl = len;
            sl_tabcomplete(buf, &len, &cur, max_len);
            if (len != pl || cur != pc)
                sl_repaint(buf, len, cur, pc, pl);
            old_len = len; continue;
        }
        if (uc < 0x20) continue;  /* skip other controls */

        /* -- Regular character insert --------------------------------------- */
        if (len < max_len - 1) {
            for (int i = len; i > cur; i--) buf[i] = buf[i-1];
            buf[cur] = (char)uc;
            len++; buf[len] = '\0';
            /* print from cur to end then back to cur+1 */
            for (int i = cur; i < len; i++) kputchar(buf[i]);
            for (int i = cur + 1; i < len; i++) kputchar('\b');
            cur++; old_len = len;
        }
    }
    (void)old_len;   /* suppress -Wunused if loop never breaks */
}

/* === Shell main loop ======================================================= */

void shell_run(void) {
    char line[LINE_MAX];

    term_set_color(COLOR_CYAN, COLOR_BLACK);
    kprintf("\n  Welcome to FunnyOS!  Type 'help' for commands.\n\n");
    term_set_color(COLOR_GREEN, COLOR_BLACK);

    while (1) {
        term_set_color(COLOR_CYAN, COLOR_BLACK);
        kprintf("funnyos:/%s", cwd);
        term_set_color(COLOR_WHITE, COLOR_BLACK);
        kprintf("$ ");
        term_set_color(shell_fg, COLOR_BLACK);

        shell_readline(line, LINE_MAX);
        hist_push(line);
        dispatch(line);
        term_set_color(shell_fg, COLOR_BLACK);
    }
}
