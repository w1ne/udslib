/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Newlib port layer (NOT a shim): the standard glue newlib-nano expects an
 * embedded target to supply. nosys.specs provides _exit/_write/_close/_read/
 * _fstat/_isatty/_lseek/_getpid/_kill stubs; the one thing it cannot guess is
 * where the heap lives, so we supply a real _sbrk here.
 *
 * The heap grows up from the linker `end` symbol (just above .bss) toward the
 * stack, which descends from _estack at the top of RAM. We refuse to grow into
 * a guard gap below the current stack pointer so a runaway malloc cannot collide
 * with the stack. malloc/calloc/free (used by mbedTLS cipher_setup) get a real
 * heap from this.
 */
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>

/* Provided by bootloader.ld. Declared as arrays so taking their address does
 * not trip GCC's -Warray-bounds on linker-defined symbols. */
extern char end[];     /* first address past .bss == start of heap */
extern char _estack[]; /* top of RAM / initial stack pointer       */

/* Keep this much headroom between heap top and the stack. */
#define HEAP_STACK_GAP 4096u

void *_sbrk(ptrdiff_t incr)
{
    static char *heap_end; /* NULL until first call */
    char *prev;
    char *new_end;
    uintptr_t limit;

    if (heap_end == 0) {
        heap_end = end;
    }

    prev = heap_end;
    new_end = heap_end + incr;
    limit = (uintptr_t) _estack - HEAP_STACK_GAP;

    /* Refuse to grow into the stack guard gap. */
    if ((uintptr_t) new_end > limit) {
        errno = ENOMEM;
        return (void *) -1;
    }

    heap_end = new_end;
    return (void *) prev;
}
