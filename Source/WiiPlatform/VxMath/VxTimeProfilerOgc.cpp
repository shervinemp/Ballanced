/**
 * @file VxTimeProfilerOgc.cpp
 * @brief VxTimeProfiler on the Broadway time base.
 */

#include "VxTimeProfiler.h"

#include <ogc/lwp_watchdog.h>
#include <string.h>

VxTimeProfiler &VxTimeProfiler::operator=(const VxTimeProfiler &t) {
    if (&t != this)
        memcpy(Times, t.Times, sizeof(Times));
    return *this;
}

void VxTimeProfiler::Reset() {
    const u64 now = gettime();
    memcpy(&Times[0], &now, sizeof(now));
}

float VxTimeProfiler::Current() {
    u64 start;
    memcpy(&start, &Times[0], sizeof(start));
    const u64 elapsed = diff_ticks(start, gettime());
    memcpy(&Times[2], &elapsed, sizeof(elapsed));
    return (float)((double)ticks_to_nanosecs(elapsed) / 1000000.0);
}
