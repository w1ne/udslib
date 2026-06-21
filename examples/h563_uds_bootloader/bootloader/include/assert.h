/*
 * Freestanding assert.h shim for bare-metal Cortex-M33 / mbedTLS build.
 * mbedTLS uses static_assert (C11) for compile-time checks; NDEBUG disables
 * runtime assert(). We define both to be safe.
 */
#ifndef _BL_ASSERT_H
#define _BL_ASSERT_H

/* Silence runtime assert — no fprintf/abort chain on bare metal */
#define assert(expr) ((void)(expr))

/* C11 static_assert is a keyword; provide the macro alias if missing */
#ifndef static_assert
#define static_assert(expr, msg) _Static_assert(expr, msg)
#endif

#endif /* _BL_ASSERT_H */
