#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n-- > 0u) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    while (n-- > 0u) {
        *d++ = (uint8_t) value;
    }
    return dst;
}

int memcmp(const void *lhs, const void *rhs, size_t n)
{
    const uint8_t *a = (const uint8_t *) lhs;
    const uint8_t *b = (const uint8_t *) rhs;
    while (n-- > 0u) {
        if (*a != *b) {
            return (int) *a - (int) *b;
        }
        ++a;
        ++b;
    }
    return 0;
}

/* Overlap-safe move (mbedTLS, linked in the security task, calls memmove). */
void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    if (d == s || n == 0u) {
        return dst;
    }
    if (d < s) {
        while (n-- > 0u) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n-- > 0u) {
            *--d = *--s;
        }
    }
    return dst;
}

/* AEABI runtime helpers (IHI0043) return void — the compiler emits these as
 * intrinsics and ignores any return value. */
void __aeabi_memcpy(void *dst, const void *src, size_t n)
{
    memcpy(dst, src, n);
}

void __aeabi_memcpy4(void *dst, const void *src, size_t n)
{
    memcpy(dst, src, n);
}

void __aeabi_memcpy8(void *dst, const void *src, size_t n)
{
    memcpy(dst, src, n);
}

void __aeabi_memset(void *dst, size_t n, int value)
{
    memset(dst, value, n);
}

void __aeabi_memclr(void *dst, size_t n)
{
    memset(dst, 0, n);
}

void __aeabi_memclr4(void *dst, size_t n)
{
    memset(dst, 0, n);
}

void __aeabi_memclr8(void *dst, size_t n)
{
    memset(dst, 0, n);
}
