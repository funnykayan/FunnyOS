#include "string.h"
#include "../mm/pmm.h"

size_t kstrlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int kstrcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, size_t n) {
    while (n-- && *a && (*a == *b)) { a++; b++; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *kstrcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *kstrncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n-- && (*d++ = *src++));
    while (n--) *d++ = '\0';
    return dst;
}

char *kstrcat(char *dst, const char *src) {
    char *d = dst + kstrlen(dst);
    while ((*d++ = *src++));
    return dst;
}

char *kstrncat(char *dst, const char *src, size_t n) {
    char *d = dst + kstrlen(dst);
    while (n-- && (*d++ = *src++));
    *d = '\0';
    return dst;
}

char *kstrchr(const char *s, int c) {
    while (*s) { if (*s == c) return (char *)s; s++; }
    return NULL;
}

char *kstrstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

char *kstrrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) { if (*s == c) last = s; s++; }
    return (char *)last;
}

char *kstrdup(const char *s) {
    size_t len = kstrlen(s) + 1;
    char  *buf = (char *)kmalloc(len);
    if (buf) kmemcpy(buf, s, len);
    return buf;
}

void *kmemset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

void *kmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

int kmemcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++; pb++;
    }
    return 0;
}

static const char digits[] = "0123456789abcdef";

void kitoa(int64_t val, char *buf, int base) {
    if (base < 2 || base > 16) { buf[0] = '\0'; return; }
    char tmp[66]; int i = 0; int neg = 0;
    if (val < 0 && base == 10) { neg = 1; val = -val; }
    if (val == 0) { tmp[i++] = '0'; }
    else {
        uint64_t uval = (uint64_t)val;
        while (uval) { tmp[i++] = digits[uval % base]; uval /= base; }
    }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

void kutoa(uint64_t val, char *buf, int base) {
    if (base < 2 || base > 16) { buf[0] = '\0'; return; }
    char tmp[66]; int i = 0;
    if (val == 0) { tmp[i++] = '0'; }
    else { while (val) { tmp[i++] = digits[val % base]; val /= base; } }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

int64_t katoi(const char *s) {
    int64_t val = 0; int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s++ - '0'); }
    return neg ? -val : val;
}

int kstrsplit(char *s, char **tokens, int max_tokens) {
    int count = 0;
    while (*s && count < max_tokens) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        tokens[count++] = s;
        while (*s && *s != ' ' && *s != '\t') s++;
        if (*s) *s++ = '\0';
    }
    return count;
}

/* ── Standard C name aliases (no libc in kernel) ─────────────────────────── */
void *memcpy(void *dst, const void *src, size_t n)  { return kmemcpy(dst, src, n); }
void *memset(void *dst, int c, size_t n)             { return kmemset(dst, c, n); }
void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s2 = src;
    if (d < s2) { for (size_t i = 0; i < n; i++) d[i] = s2[i]; }
    else        { for (size_t i = n; i > 0; i--) d[i-1] = s2[i-1]; }
    return dst;
}
int    memcmp(const void *a, const void *b, size_t n){ return kmemcmp(a, b, n); }
size_t strlen(const char *s)                         { return kstrlen(s); }
int    strcmp(const char *a, const char *b)          { return kstrcmp(a, b); }
int    strncmp(const char *a, const char *b, size_t n){ return kstrncmp(a, b, n); }
char  *strcpy(char *dst, const char *src)            { return kstrcpy(dst, src); }
char  *strncpy(char *dst, const char *src, size_t n) { return kstrncpy(dst, src, n); }
char  *strcat(char *dst, const char *src)            { return kstrcat(dst, src); }
char  *strncat(char *dst, const char *src, size_t n) { return kstrncat(dst, src, n); }
char  *strchr(const char *s, int c)                  { return kstrchr(s, c); }
char  *strstr(const char *h, const char *n)           { return kstrstr(h, n); }
