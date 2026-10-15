#ifndef MAAGE_TIME_H
#define MAAGE_TIME_H
/* Seconds on a monotonic clock with an unspecified origin; negative on failure.
 * Wall-clock changes do not affect this clock. Differences measure elapsed time. */
double maage_time_monotonic_seconds(void);
#endif
