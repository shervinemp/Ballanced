// sqrtf from FastMath.c against newlib's bit-by-bit root.

#include "TestFramework.h"

#include <float.h>
#include <math.h>
#include <string.h>

#include <ogc/lwp_watchdog.h>

extern "C" float __ieee754_sqrtf(float x);

namespace
{
    unsigned int Bits(float value)
    {
        unsigned int bits;
        memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    float FromBits(unsigned int bits)
    {
        float value;
        memcpy(&value, &bits, sizeof(value));
        return value;
    }
}

void RunMathTests()
{
    wiitest::BeginSuite("Math");

    // Every exponent, including denormals, with a spread of mantissas.
    int mismatches = 0;
    unsigned int firstBits = 0;
    unsigned int seed = 12345;
    for (unsigned int exponent = 0; exponent < 255; ++exponent)
    {
        for (int i = 0; i < 64; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const unsigned int mantissa = i == 0 ? 0 : (i == 1 ? 0x7FFFFF : seed >> 9);
            const float x = FromBits(exponent << 23 | mantissa);
            if (Bits(sqrtf(x)) != Bits(__ieee754_sqrtf(x)))
            {
                if (mismatches++ == 0)
                    firstBits = Bits(x);
            }
        }
    }
    WT_CHECK(mismatches == 0, "sqrtf matches newlib: %d of %d differ, first 0x%08x", mismatches, 255 * 64,
             firstBits);

    WT_CHECK(Bits(sqrtf(0.0f)) == Bits(0.0f), "sqrtf(+0)");
    WT_CHECK(Bits(sqrtf(-0.0f)) == Bits(-0.0f), "sqrtf(-0)");
    WT_CHECK(sqrtf(INFINITY) == INFINITY, "sqrtf(inf)");
    WT_CHECK(isnan(sqrtf(-1.0f)), "sqrtf(-1) is NaN");
    WT_CHECK(isnan(sqrtf(-INFINITY)), "sqrtf(-inf) is NaN");
    WT_CHECK(isnan(sqrtf(NAN)), "sqrtf(NaN) is NaN");
    WT_CHECK(sqrtf(4.0f) == 2.0f && sqrtf(1.0f) == 1.0f && sqrtf(0.25f) == 0.5f, "exact roots");
    WT_CHECK(sqrtf(FLT_MAX) == __ieee754_sqrtf(FLT_MAX), "sqrtf(FLT_MAX)");

    // How much faster, for the log.
    volatile float sink = 0.0f;
    const int calls = 10000;
    u64 start = gettime();
    for (int i = 1; i <= calls; ++i)
        sink = sink + sqrtf((float)i);
    const u64 fast = gettime() - start;
    start = gettime();
    for (int i = 1; i <= calls; ++i)
        sink = sink + __ieee754_sqrtf((float)i);
    const u64 slow = gettime() - start;
    wiitest::Log("  sqrtf: %u ns a call, newlib %u ns", (unsigned)(ticks_to_nanosecs(fast) / calls),
                 (unsigned)(ticks_to_nanosecs(slow) / calls));

    wiitest::EndSuite();
}
