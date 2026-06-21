/*
 * Freestanding stdlib.h shim for bare-metal Cortex-M33 build.
 * Provides minimal declarations; actual allocations go through mbedTLS
 * memory_buffer_alloc (which overrides calloc/free via platform hooks).
 */
#ifndef _BL_STDLIB_H
#define _BL_STDLIB_H

#include <stddef.h>

void *calloc(size_t nmemb, size_t size);
void *malloc(size_t size);
void  free(void *ptr);
void  abort(void);

/* exit() — bare-metal: spin on BKPT */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
__attribute__((noreturn)) void exit(int status);

#endif /* _BL_STDLIB_H */
