/*
 * Freestanding string.h shim for bare-metal Cortex-M33 build.
 * Declarations only; implementations are in libc_stubs.c.
 */
#ifndef _BL_STRING_H
#define _BL_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);
int   memcmp(const void *lhs, const void *rhs, size_t n);
void *memmove(void *dst, const void *src, size_t n);

/* mbedTLS uses these string functions */
int    strcmp(const char *s1, const char *s2);
size_t strlen(const char *s);

#endif /* _BL_STRING_H */
