#define _POSIX_C_SOURCE 200809L
#include "maage/time.h"
#include "cmakecfg.h"
#if MAAGE_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
double maage_time_monotonic_seconds(void)
{
    LARGE_INTEGER frequency, ticks;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&ticks)) return -1.0;
    return (double)ticks.QuadPart / (double)frequency.QuadPart;
}
#elif MAAGE_PLATFORM_MACOS
#include <mach/mach_time.h>
double maage_time_monotonic_seconds(void)
{
    mach_timebase_info_data_t base;
    if (mach_timebase_info(&base) != KERN_SUCCESS || !base.denom) return -1.0;
    return (double)mach_absolute_time() * (double)base.numer / (double)base.denom / 1000000000.0;
}
#elif MAAGE_PLATFORM_LINUX
#include <time.h>
double maage_time_monotonic_seconds(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) return -1.0;
    return (double)time.tv_sec + (double)time.tv_nsec / 1000000000.0;
}
#else
double maage_time_monotonic_seconds(void) { return -1.0; }
#endif
