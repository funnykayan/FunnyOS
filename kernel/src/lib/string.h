#pragma once
#include <stdint.h>
#include <stddef.h>

/* Standard-style string functions */
size_t  kstrlen(const char *s);
int     kstrcmp(const char *a, const char *b);
int     kstrncmp(const char *a, const char *b, size_t n);
char   *kstrcpy(char *dst, const char *src);
char   *kstrncpy(char *dst, const char *src, size_t n);
char   *kstrcat(char *dst, const char *src);
char   *kstrncat(char *dst, const char *src, size_t n);
char   *kstrchr(const char *s, int c);
char   *kstrrchr(const char *s, int c);
char   *kstrstr(const char *haystack, const char *needle);
char   *kstrdup(const char *s);   /* uses kmalloc */
void   *kmemset(void *dst, int c, size_t n);
void   *kmemcpy(void *dst, const void *src, size_t n);
int     kmemcmp(const void *a, const void *b, size_t n);

/* Integer ↔ string conversions */
void    kitoa(int64_t val,  char *buf, int base);
void    kutoa(uint64_t val, char *buf, int base);
int64_t katoi(const char *s);

/* Split a string by whitespace, returns token count (≤ max_tokens) */
int     kstrsplit(char *s, char **tokens, int max_tokens);

/* Standard C aliases (no libc) */
void   *memcpy(void *dst, const void *src, size_t n);
void   *memset(void *dst, int c, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
int     memcmp(const void *a, const void *b, size_t n);
size_t  strlen(const char *s);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t n);
char   *strcpy(char *dst, const char *src);
char   *strncpy(char *dst, const char *src, size_t n);
char   *strcat(char *dst, const char *src);
char   *strncat(char *dst, const char *src, size_t n);
char   *strchr(const char *s, int c);
char   *strstr(const char *haystack, const char *needle);
