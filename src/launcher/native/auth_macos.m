#import <AppKit/AppKit.h>
#import <Security/Security.h>
#include "../access/oauth.h"
#include <string.h>
#include <time.h>
int maage_auth_random(unsigned char *bytes, size_t count)
{ return SecRandomCopyBytes(kSecRandomDefault, count, bytes) == errSecSuccess; }
int maage_auth_browser(const char *url)
{
    @autoreleasepool {
        /* LaunchServices supports worker-thread calls; AppKit remains on main. */
        CFURLRef target = CFURLCreateWithBytes(NULL, (const UInt8 *)url, (CFIndex)strlen(url), kCFStringEncodingUTF8, NULL);
        OSStatus result = target ? LSOpenCFURLRef(target, NULL) : paramErr;
        if (target) CFRelease(target); return result == noErr;
    }
}
void maage_auth_sleep(unsigned milliseconds)
{
    struct timespec delay; delay.tv_sec = milliseconds / 1000; delay.tv_nsec = (long)(milliseconds % 1000) * 1000000;
    nanosleep(&delay, NULL);
}
@interface MaaGEOAuthActivation : NSObject
- (void)openURL:(NSAppleEventDescriptor *)event reply:(NSAppleEventDescriptor *)reply;
@end
@implementation MaaGEOAuthActivation
- (void)openURL:(NSAppleEventDescriptor *)event reply:(NSAppleEventDescriptor *)reply
{
    (void)reply;
    NSString *uri = [event paramDescriptorForKeyword:keyDirectObject].stringValue;
    if (uri) maage_oauth_forward_url(uri.UTF8String);
    [NSApp activateIgnoringOtherApps:YES];
}
@end
static MaaGEOAuthActivation *maage_auth_activation;
void maage_auth_platform_start(void)
{
    [NSApplication sharedApplication];
    maage_auth_activation = [[MaaGEOAuthActivation alloc] init];
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler:maage_auth_activation andSelector:@selector(openURL:reply:) forEventClass:kInternetEventClass andEventID:kAEGetURL];
}
void maage_auth_platform_stop(void)
{
    [[NSAppleEventManager sharedAppleEventManager] removeEventHandlerForEventClass:kInternetEventClass andEventID:kAEGetURL];
    maage_auth_activation = nil;
}
