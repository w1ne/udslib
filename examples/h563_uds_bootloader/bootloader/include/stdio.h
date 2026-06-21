/*
 * Freestanding stdio.h shim for bare-metal Cortex-M33 / mbedTLS build.
 * mbedTLS platform.h includes <stdio.h> to get snprintf/vsnprintf and
 * FILE* for the optional fprintf hook. We provide minimal stubs.
 * None of these are called in practice with our minimal config
 * (no debug, no error strings printed at runtime).
 */
#ifndef _BL_STDIO_H
#define _BL_STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* Opaque FILE type — we never open files on bare metal */
typedef struct _BL_FILE FILE;

extern FILE *stdout;
extern FILE *stderr;

static inline int printf(const char *fmt, ...) { (void)fmt; return 0; }
static inline int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
static inline int snprintf(char *s, size_t n, const char *fmt, ...)
{
    (void)s; (void)n; (void)fmt; return 0;
}
static inline int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    (void)s; (void)n; (void)fmt; (void)ap; return 0;
}

#endif /* _BL_STDIO_H */
