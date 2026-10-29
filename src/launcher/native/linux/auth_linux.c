#define _POSIX_C_SOURCE 200809L
#include "../../access/oauth.h"
#include <sys/random.h>
#include <errno.h>
#include <time.h>
#include <gio/gio.h>
maage_status maage_auth_random(unsigned char *bytes, size_t count) {
    ssize_t got;
    if (!bytes && count) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_CREATE, 0);
    if (!count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CREATE, 0);
    while (count) {
        got = getrandom(bytes, count, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return got < 0 ? maage_status_from_errno(errno, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_CREATE) : maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_CREATE, 0);
        bytes += got;
        count -= (size_t) got;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_auth_browser(const char *url) {
    GError *error = NULL;
    int ok;
    if (!url) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN, 0);
    ok = g_app_info_launch_default_for_uri(url, NULL, &error);
    /* GError strings may contain credentials in authorization URLs. */
    if (error) g_error_free(error);
    return maage_status_make(ok ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_SYSTEM_NOT_SUPPORTED,
                             MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN, 0);
}
maage_status maage_auth_sleep(unsigned milliseconds) {
    struct timespec delay;
    if (!milliseconds) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_WAIT, 0);
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (long) (milliseconds % 1000) * 1000000;
    while (nanosleep(&delay, &delay))
        if (errno != EINTR)
            return maage_status_from_errno(errno, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WAIT);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_WAIT, 0);
}
