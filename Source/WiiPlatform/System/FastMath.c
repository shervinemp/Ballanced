// Square roots for the Broadway, which has no square root instruction.
//
// newlib's sqrtf works out the root one bit at a time, a few hundred cycles a
// call, and the engine takes a few hundred roots a frame. Refining the
// hardware reciprocal square root estimate (5 bits) with four Newton steps in
// double precision reaches double precision, so the root rounds to the same
// float newlib returns.

#include <math.h>

float sqrtf(float x)
{
    // Zeros, NaN, infinity and negatives as IEEE 754 wants them.
    if (!(x > 0.0f) || x == INFINITY)
        return x < 0.0f ? NAN : x;

    const double d = x;
    double y;
    __asm__("frsqrte %0,%1" : "=f"(y) : "f"(d));
    const double half = 0.5 * d;
    for (int i = 0; i < 4; ++i)
        y = y * (1.5 - half * y * y);
    return (float)(d * y);
}
