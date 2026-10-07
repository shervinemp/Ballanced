// C library functions that devkitPPC's newlib declares but does not provide.

#include <time.h>

// C11 timespec_get, used by the player's logger for timestamps.
int timespec_get(struct timespec *ts, int base)
{
    if (!ts || base != TIME_UTC)
        return 0;
    if (clock_gettime(CLOCK_REALTIME, ts) != 0)
        return 0;
    return base;
}
