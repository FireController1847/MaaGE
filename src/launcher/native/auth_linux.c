#define _POSIX_C_SOURCE 200809L
#include "../access/oauth.h"
#include "maage/log.h"
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
        if (got <= 0) { maage_log_error("maage.launcher.access.platform", "Linux cryptographic random generation failed (errno=%d).", got < 0 ? errno : 0); return 0; }
        bytes += got; count -= (size_t)got;
    }
    return 1;
}
int maage_auth_browser(const char *url)
{
    GError *error = NULL;
    int ok = g_app_info_launch_default_for_uri(url, NULL, &error);
    if (!ok) maage_log_warn("maage.launcher.access.platform", "System browser launch failed (GIO error domain=%u; code=%d).", error ? error->domain : 0, error ? error->code : 0);
    /* GError messages can contain the authorization URL; never log them. */
    if (error) g_error_free(error);
    return ok;
}
void maage_auth_sleep(unsigned milliseconds)
{
    struct timespec delay;
    delay.tv_sec = milliseconds / 1000; delay.tv_nsec = (long)(milliseconds % 1000) * 1000000;
    while (nanosleep(&delay, &delay) && errno == EINTR) { }
}
