// memcpy and memset for the Broadway.
//
// newlib's are generic C: they move whole words only when both pointers are
// word aligned and single bytes otherwise, and every destination cache line
// is read from memory before it is overwritten. These move words whatever the
// source alignment (the CPU loads misaligned words itself), and blocks of 128
// bytes or more claim whole destination lines with dcbz, which allocates a
// zeroed line without reading memory. Small copies, mostly matrices, keep a
// short path.
//
// Link this file into each executable, like MainStack.c. It is compiled with
// -fno-tree-loop-distribute-patterns so GCC does not turn these loops back
// into calls to memcpy and memset. Both must stay leaf functions without a
// stack frame: crt0 clears .bss with memset while its stack is in .bss.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t __attribute__((__may_alias__, __aligned__(1))) unaligned_word;
typedef uint32_t __attribute__((__may_alias__)) word;

enum
{
    kLargeBlock = 128
};

// dcbz only works on cached memory: MEM1 and MEM2 through their cached views.
static inline int IsCached(const unsigned char *p, size_t n)
{
    const uintptr_t start = (uintptr_t)p;
    const uintptr_t end = start + n;
    return (start >= 0x80000000u && end <= 0x81800000u) || (start >= 0x90000000u && end <= 0x94000000u);
}

static inline void ZeroLine(unsigned char *line)
{
    __asm__ __volatile__("dcbz 0,%0" : : "r"(line) : "memory");
}

// Copies from a word-aligned destination; returns the bytes left (under 4).
static inline __attribute__((always_inline)) size_t CopyWords(unsigned char **dp, const unsigned char **sp, size_t n)
{
    unsigned char *d = *dp;
    const unsigned char *s = *sp;
    for (; n >= 16; d += 16, s += 16, n -= 16)
    {
        const uint32_t a = ((const unaligned_word *)s)[0];
        const uint32_t b = ((const unaligned_word *)s)[1];
        const uint32_t c = ((const unaligned_word *)s)[2];
        const uint32_t e = ((const unaligned_word *)s)[3];
        ((word *)d)[0] = a;
        ((word *)d)[1] = b;
        ((word *)d)[2] = c;
        ((word *)d)[3] = e;
    }
    for (; n >= 4; d += 4, s += 4, n -= 4)
        *(word *)d = *(const unaligned_word *)s;
    *dp = d;
    *sp = s;
    return n;
}

static inline __attribute__((always_inline)) void CopyLarge(unsigned char *d, const unsigned char *s, size_t n)
{
    // dcbz must not clear source bytes not read yet: only without overlap
    // (memcpy's contract, which not every caller keeps).
    const int claimLines = (s + n <= d || d + n <= s) && IsCached(d, n);
    while ((uintptr_t)d & 3)
    {
        *d++ = *s++;
        --n;
    }
    if (claimLines)
    {
        while ((uintptr_t)d & 31)
        {
            *(word *)d = *(const unaligned_word *)s;
            d += 4;
            s += 4;
            n -= 4;
        }
        for (; n >= 32; d += 32, s += 32, n -= 32)
        {
            // The line is not part of the source, so it can go first.
            ZeroLine(d);
            for (int half = 0; half < 32; half += 16)
            {
                const uint32_t a = ((const unaligned_word *)(s + half))[0];
                const uint32_t b = ((const unaligned_word *)(s + half))[1];
                const uint32_t c = ((const unaligned_word *)(s + half))[2];
                const uint32_t e = ((const unaligned_word *)(s + half))[3];
                ((word *)(d + half))[0] = a;
                ((word *)(d + half))[1] = b;
                ((word *)(d + half))[2] = c;
                ((word *)(d + half))[3] = e;
            }
        }
    }
    n = CopyWords(&d, &s, n);
    while (n--)
        *d++ = *s++;
}

void *memcpy(void *__restrict dst, const void *__restrict src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (n >= kLargeBlock)
    {
        CopyLarge(d, s, n);
        return dst;
    }
    if (n >= 4)
    {
        while ((uintptr_t)d & 3)
        {
            *d++ = *s++;
            --n;
        }
        n = CopyWords(&d, &s, n);
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

static inline __attribute__((always_inline)) void SetLarge(unsigned char *d, unsigned char byte, size_t n)
{
    const uint32_t pattern = byte * 0x01010101u;
    const int claimLines = IsCached(d, n);
    while ((uintptr_t)d & 3)
    {
        *d++ = byte;
        --n;
    }
    if (claimLines)
    {
        while ((uintptr_t)d & 31)
        {
            *(word *)d = pattern;
            d += 4;
            n -= 4;
        }
        for (; n >= 32; d += 32, n -= 32)
        {
            ZeroLine(d);
            if (pattern != 0)
            {
                ((word *)d)[0] = pattern;
                ((word *)d)[1] = pattern;
                ((word *)d)[2] = pattern;
                ((word *)d)[3] = pattern;
                ((word *)d)[4] = pattern;
                ((word *)d)[5] = pattern;
                ((word *)d)[6] = pattern;
                ((word *)d)[7] = pattern;
            }
        }
    }
    for (; n >= 4; d += 4, n -= 4)
        *(word *)d = pattern;
    while (n--)
        *d++ = byte;
}

void *memset(void *dst, int value, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char byte = (unsigned char)value;
    if (n >= kLargeBlock)
    {
        SetLarge(d, byte, n);
        return dst;
    }
    if (n >= 4)
    {
        const uint32_t pattern = byte * 0x01010101u;
        while ((uintptr_t)d & 3)
        {
            *d++ = byte;
            --n;
        }
        for (; n >= 4; d += 4, n -= 4)
            *(word *)d = pattern;
    }
    while (n--)
        *d++ = byte;
    return dst;
}
