#define _POSIX_C_SOURCE 200809L
#include "../access/oauth.h"
#include <sys/random.h>
#include <errno.h>
#include <time.h>
#include <gio/gio.h>
#include <string.h>
int maage_auth_random(unsigned char *bytes, size_t count)
{
    ssize_t got;
    while (count) {
        got = getrandom(bytes, count, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return 0;
        bytes += got; count -= (size_t)got;
    }
    return 1;
}
int maage_auth_browser(const char *url)
{ return g_app_info_launch_default_for_uri(url, NULL, NULL); }
void maage_auth_sleep(unsigned milliseconds)
{
    struct timespec delay;
    delay.tv_sec = milliseconds / 1000; delay.tv_nsec = (long)(milliseconds % 1000) * 1000000;
    while (nanosleep(&delay, &delay) && errno == EINTR) { }
}
void maage_auth_platform_start(void) { }
void maage_auth_platform_stop(void) { }
