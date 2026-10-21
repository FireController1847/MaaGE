#import <CoreServices/CoreServices.h>
#import <Security/Security.h>
#include "../access/oauth.h"
#include "maage/log.h"
#include <string.h>
#include <time.h>
#include <errno.h>
maage_status maage_auth_random(unsigned char *bytes, size_t count)
{
    OSStatus native;
    if (!bytes && count) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_CREATE, 0);
    if (!count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CREATE, 0);
    native = SecRandomCopyBytes(kSecRandomDefault, count, bytes);
    return maage_status_make(native == errSecSuccess ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ERROR,
        MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_auth_browser(const char *url)
{
    @autoreleasepool {
        CFURLRef target;
        OSStatus native;
        if (!url) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN, 0);
        target = CFURLCreateWithBytes(NULL, (const UInt8 *)url, (CFIndex)strlen(url), kCFStringEncodingUTF8, NULL);
        native = target ? LSOpenCFURLRef(target, NULL) : paramErr;
        if (target) CFRelease(target);
        return maage_status_make(native == noErr ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_SYSTEM_NOT_SUPPORTED,
            MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN, 0);
    }
}
maage_status maage_auth_sleep(unsigned milliseconds)
{
    struct timespec delay;
    if (!milliseconds) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_WAIT, 0);
    delay.tv_sec = milliseconds / 1000; delay.tv_nsec = (long)(milliseconds % 1000) * 1000000;
    while (nanosleep(&delay, &delay)) if (errno != EINTR)
        return maage_status_from_errno(errno, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WAIT);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_WAIT, 0);
}
