#include "links.h"
#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>

@interface MaaGELauncherLinks : NSObject {
@public
    BOOL requested;
}
- (void)install:(NSNotification *)notification;
- (void)receiveURL:(NSAppleEventDescriptor *)event reply:(NSAppleEventDescriptor *)reply;
@end
@implementation MaaGELauncherLinks
- (void)install:(NSNotification *)notification
{
    (void)notification;
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler:self
        andSelector:@selector(receiveURL:reply:) forEventClass:kInternetEventClass andEventID:kAEGetURL];
}
- (void)receiveURL:(NSAppleEventDescriptor *)event reply:(NSAppleEventDescriptor *)reply
{
    NSString *value = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
    NSString *expected = @MAAGE_LAUNCHER_AUTH_URI;
    (void)reply;
    /* Accept only the return link; ignore credentials and other URI payloads. */
    if (value && ([value length] == [expected length] || [value length] == [expected length] + 1) &&
        ([value caseInsensitiveCompare:expected] == NSOrderedSame ||
         [value caseInsensitiveCompare:[expected stringByAppendingString:@"/"]] == NSOrderedSame)) requested = YES;
}
@end
static MaaGELauncherLinks *maage_launcher_links;
int maage_launcher_links_init(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    maage_launcher_links = [[MaaGELauncherLinks alloc] init];
    if (maage_launcher_links) {
        [[NSNotificationCenter defaultCenter] addObserver:maage_launcher_links selector:@selector(install:)
            name:NSApplicationWillFinishLaunchingNotification object:nil];
        [maage_launcher_links install:nil];
    }
    [pool drain];
    return maage_launcher_links != nil;
}
int maage_launcher_links_poll(maage_window *window)
{
    maage_window_native native;
    NSWindow *handle;
    if (!maage_launcher_links || !maage_launcher_links->requested) return 0;
    maage_launcher_links->requested = NO;
    if (maage_window_get_native(window, &native) != MAAGE_WINDOW_OK || native.backend != MAAGE_WINDOW_COCOA) return 0;
    handle = (NSWindow *)native.handles.cocoa.window;
    if ([handle isMiniaturized]) [handle deminiaturize:nil];
    [handle makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    return 1;
}
void maage_launcher_links_shutdown(void)
{
    if (!maage_launcher_links) return;
    [[NSNotificationCenter defaultCenter] removeObserver:maage_launcher_links];
    [[NSAppleEventManager sharedAppleEventManager] removeEventHandlerForEventClass:kInternetEventClass andEventID:kAEGetURL];
    [maage_launcher_links release]; maage_launcher_links = nil;
}
