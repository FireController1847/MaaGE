#import <CoreServices/CoreServices.h>
#import <Security/Security.h>
#include "../access/oauth.h"
#include "maage/log.h"
#include <string.h>
#include <time.h>
int maage_auth_random(unsigned char *bytes, size_t count)
{
    OSStatus status = SecRandomCopyBytes(kSecRandomDefault, count, bytes);
    if (status != errSecSuccess) maage_log_error("maage.launcher.access.platform", "macOS cryptographic random generation failed (OSStatus=%ld).", (long)status);
    return status == errSecSuccess;
}
int maage_auth_browser(const char *url)
{
    @autoreleasepool {
        /* LaunchServices supports worker-thread calls; AppKit remains on main. */
        CFURLRef target = CFURLCreateWithBytes(NULL, (const UInt8 *)url, (CFIndex)strlen(url), kCFStringEncodingUTF8, NULL);
        OSStatus result = target ? LSOpenCFURLRef(target, NULL) : paramErr;
        if (result != noErr) maage_log_warn("maage.launcher.access.platform", "System browser launch failed (LaunchServices OSStatus=%ld).", (long)result);
        if (target) CFRelease(target); return result == noErr;
    }
}
void maage_auth_sleep(unsigned milliseconds)
{
    struct timespec delay; delay.tv_sec = milliseconds / 1000; delay.tv_nsec = (long)(milliseconds % 1000) * 1000000;
    nanosleep(&delay, NULL);
}
