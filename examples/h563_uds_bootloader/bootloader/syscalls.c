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

    /* Never shrink below the heap start (newlib-nano dlmalloc does not pass a
     * negative increment in practice, but guard the corruption path anyway). */
    if (incr < 0 && new_end < end) {
        errno = ENOMEM;
        return (void *) -1;
    }

    /* Refuse to grow within HEAP_STACK_GAP of the stack, bounded by BOTH the
     * top of RAM (_estack) and the *live* stack pointer — so a deep call/IRQ
     * chain that has already descended past _estack-GAP is still respected. */
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    limit = (uintptr_t) _estack;
    if (sp < limit) {
        limit = sp;
    }
    limit -= HEAP_STACK_GAP;

    if (incr > 0 && (uintptr_t) new_end > limit) {
        errno = ENOMEM;
        return (void *) -1;
    }

    heap_end = new_end;
    return (void *) prev;
}
