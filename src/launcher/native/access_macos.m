#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include "../access/access.h"
#include "../access/oauth.h"
#include "maage/time.h"
#include <string.h>
static NSColor *maage_access_color(CGFloat gray)
{ return [NSColor colorWithSRGBRed:gray green:gray blue:gray alpha:1]; }
static NSTextField *maage_access_label(NSView *parent, NSString *text, NSRect frame, CGFloat size, BOOL bold)
{
    NSTextField *label = [NSTextField wrappingLabelWithString:text]; label.frame = frame;
    label.font = bold ? [NSFont systemFontOfSize:size weight:NSFontWeightSemibold] : [NSFont systemFontOfSize:size];
    label.textColor = maage_access_color(.9); [parent addSubview:label]; return label;
}
@interface MaaGEAccessController : NSObject <NSWindowDelegate, NSApplicationDelegate> {
    maage_access_snapshot _snapshot;
}
@property(nonatomic, assign) maage_access *access;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSView *panel;
@property(nonatomic, strong) NSPopUpButton *profiles;
@property(nonatomic, strong) NSTextField *identity, *status, *device;
@property(nonatomic, strong) NSButton *signIn, *fallback, *stored, *signOut, *remove, *codeCopyButton, *open, *stop, *next, *cancel;
@property(nonatomic, strong) NSProgressIndicator *progress;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic) unsigned revision;
@property(nonatomic) BOOL closing, manageAccounts;
@property(nonatomic) int result;
@end
@implementation MaaGEAccessController
- (void)finish:(int)result
{
    self.result = result; [self.timer invalidate]; self.timer = nil;
    [self.window orderOut:nil]; [NSApp stop:nil];
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
}
- (void)cancelAccess:(id)sender
{ (void)sender; self.closing = YES; maage_access_cancel(self.access); [self refresh:nil]; }
- (BOOL)windowShouldClose:(NSWindow *)sender
{ (void)sender; [self cancelAccess:nil]; return NO; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)application
{ (void)application; [self cancelAccess:nil]; return NSTerminateCancel; }
- (void)continueAccess:(id)sender
{ (void)sender; if (!_snapshot.busy && maage_access_allowed(self.access)) [self finish:1]; }
- (void)selectProfile:(id)sender { (void)sender; [self refresh:nil]; }
- (void)signIn:(id)sender { (void)sender; maage_access_sign_in(self.access, 0); [self refresh:nil]; }
- (void)deviceSignIn:(id)sender { (void)sender; maage_access_sign_in(self.access, 1); [self refresh:nil]; }
- (void)verifySaved:(id)sender
{ (void)sender; NSString *uuid = self.profiles.selectedItem.representedObject; if (uuid) maage_access_verify_local(self.access, uuid.UTF8String); [self refresh:nil]; }
- (void)signOut:(id)sender
{ (void)sender; NSString *uuid = self.profiles.selectedItem.representedObject; if (uuid) maage_access_sign_out(self.access, uuid.UTF8String); [self refresh:nil]; }
- (void)removeAccount:(id)sender
{ (void)sender; NSString *uuid = self.profiles.selectedItem.representedObject; if (uuid) maage_access_disconnect(self.access, uuid.UTF8String); [self refresh:nil]; }
- (void)copyCode:(id)sender
{ (void)sender; if (*_snapshot.device_code) { [NSPasteboard.generalPasteboard clearContents]; [NSPasteboard.generalPasteboard setString:@(_snapshot.device_code) forType:NSPasteboardTypeString]; } }
- (void)openBrowser:(id)sender
{ (void)sender; if (*_snapshot.device_url) [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@(_snapshot.device_url)]]; }
- (void)stopSignIn:(id)sender { (void)sender; maage_access_cancel_operation(self.access); }
- (void)refresh:(NSTimer *)timer
{
    (void)timer;
    NSString *selected = self.profiles.selectedItem.representedObject;
    maage_access_snapshot_get(self.access, &_snapshot);
    if (self.closing && !_snapshot.busy) { [self finish:0]; return; }
    if (!self.manageAccounts && _snapshot.auto_continue && _snapshot.allowed && !_snapshot.busy) { [self finish:1]; return; }
    if (self.revision != _snapshot.revision) {
        [self.profiles removeAllItems];
        for (unsigned i = 0; i < _snapshot.count; ++i) {
            maage_access_profile *p = &_snapshot.profiles[i];
            NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"%s — %s", p->name, maage_access_state_name(p->state)] action:NULL keyEquivalent:@""];
            [self.profiles.menu addItem:item]; self.profiles.lastItem.representedObject = @(p->uuid);
            if ([selected isEqualToString:@(p->uuid)]) [self.profiles selectItem:self.profiles.lastItem];
        }
        if (self.profiles.indexOfSelectedItem < 0 && _snapshot.count) [self.profiles selectItemAtIndex:0];
        self.revision = _snapshot.revision;
    }
    NSInteger index = self.profiles.indexOfSelectedItem;
    maage_access_profile *p = index >= 0 && (unsigned)index < _snapshot.count ? &_snapshot.profiles[index] : NULL;
    self.panel.hidden = p == NULL;
    self.identity.stringValue = p ? [NSString stringWithFormat:@"%s\nMinecraft: Java Edition · %s\n%s", p->uuid, maage_access_state_name(p->state), p->detail] : @"";
    self.status.stringValue = @(_snapshot.status);
    BOOL idle = !_snapshot.busy && !self.closing;
    self.profiles.enabled = idle; self.signIn.enabled = idle; self.fallback.enabled = idle;
    self.stored.enabled = idle && p && p->session_saved; self.signOut.enabled = idle && p && p->session_saved;
    self.remove.enabled = idle && p; self.cancel.enabled = !self.closing; self.stop.enabled = _snapshot.busy && !self.closing;
    self.next.enabled = idle && _snapshot.allowed;
    BOOL device = *_snapshot.device_code != 0;
    self.codeCopyButton.hidden = self.open.hidden = !device;
    if (device) {
        double seconds = _snapshot.device_deadline - maage_time_monotonic_seconds();
        self.device.stringValue = [NSString stringWithFormat:@"%s\nCode: %s · %.0f seconds remaining", _snapshot.device_url, _snapshot.device_code, seconds > 0 ? seconds : 0];
    } else self.device.stringValue = @"Your browser will open to complete sign-in.";
    if (_snapshot.busy) [self.progress startAnimation:nil]; else [self.progress stopAnimation:nil];
    if (!self.window.visible) [self.window makeKeyAndOrderFront:nil];
}
- (NSButton *)button:(NSString *)title action:(SEL)action frame:(NSRect)frame parent:(NSView *)parent
{
    NSButton *button = [NSButton buttonWithTitle:title target:self action:action]; button.frame = frame; [parent addSubview:button]; return button;
}
- (void)show
{
    self.revision = (unsigned)-1;
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 650, 630) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title = self.manageAccounts ? @"MaaGE Launcher — Microsoft Accounts" : @"MaaGE Launcher — Initial setup"; self.window.releasedWhenClosed = NO;
    self.window.delegate = self; self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]; self.window.backgroundColor = maage_access_color(.11);
    NSView *content = self.window.contentView;
    maage_access_label(content, @"MaaGE Launcher", NSMakeRect(28, 585, 240, 23), 14, YES);
    NSTextField *step = maage_access_label(content, self.manageAccounts ? @"Connected accounts" : @"Initial setup · 1 of 2", NSMakeRect(422, 585, 200, 23), 12, NO); step.alignment = NSTextAlignmentRight;
    maage_access_label(content, @"Verify Minecraft Access", NSMakeRect(30, 526, 590, 35), 25, YES);
    maage_access_label(content, @"Sign in with your Microsoft account to verify access to Minecraft: Java Edition.", NSMakeRect(32, 478, 586, 42), 13, NO);
    self.panel = [[NSView alloc] initWithFrame:NSMakeRect(28, 302, 594, 170)]; self.panel.wantsLayer = YES; self.panel.layer.backgroundColor = maage_access_color(.16).CGColor; self.panel.layer.cornerRadius = 8; [content addSubview:self.panel]; self.panel.hidden = YES;
    self.profiles = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(16, 129, 562, 28) pullsDown:NO]; self.profiles.menu.autoenablesItems = NO; self.profiles.target = self; self.profiles.action = @selector(selectProfile:); [self.panel addSubview:self.profiles];
    self.identity = maage_access_label(self.panel, @"", NSMakeRect(20, 46, 554, 78), 12, NO);
    self.stored = [self button:@"Refresh / Reverify" action:@selector(verifySaved:) frame:NSMakeRect(16, 8, 190, 30) parent:self.panel];
    self.signOut = [self button:@"Sign Out" action:@selector(signOut:) frame:NSMakeRect(310, 8, 120, 30) parent:self.panel];
    self.remove = [self button:@"Remove" action:@selector(removeAccount:) frame:NSMakeRect(440, 8, 138, 30) parent:self.panel];
    self.signIn = [self button:@"Sign in with Microsoft" action:@selector(signIn:) frame:NSMakeRect(28, 248, 594, 42) parent:content];
    self.signIn.contentTintColor = [NSColor colorWithSRGBRed:.91 green:.29 blue:.68 alpha:1];
    self.fallback = [self button:@"Use Device Code" action:@selector(deviceSignIn:) frame:NSMakeRect(28, 208, 200, 30) parent:content];
    self.stop = [self button:@"Cancel Sign-in" action:@selector(stopSignIn:) frame:NSMakeRect(436, 208, 186, 30) parent:content];
    self.device = maage_access_label(content, @"", NSMakeRect(32, 147, 584, 50), 12, NO);
    self.codeCopyButton = [self button:@"Copy Code" action:@selector(copyCode:) frame:NSMakeRect(28, 109, 125, 30) parent:content];
    self.open = [self button:@"Open Browser" action:@selector(openBrowser:) frame:NSMakeRect(164, 109, 150, 30) parent:content];
    self.progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(596, 116, 20, 20)]; self.progress.style = NSProgressIndicatorStyleSpinning; self.progress.displayedWhenStopped = NO; [content addSubview:self.progress];
    self.status = maage_access_label(content, @"", NSMakeRect(30, 54, 590, 52), 12, NO); self.status.textColor = maage_access_color(.70);
    self.cancel = [self button:@"Cancel" action:@selector(cancelAccess:) frame:NSMakeRect(402, 14, 100, 32) parent:content]; self.cancel.keyEquivalent = @"\e";
    self.next = [self button:@"Continue" action:@selector(continueAccess:) frame:NSMakeRect(512, 14, 110, 32) parent:content]; self.next.keyEquivalent = @"\r";
    self.next.enabled = NO; [self.window center];
    self.timer = [NSTimer scheduledTimerWithTimeInterval:.1 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
}
@end
int maage_access_native_run(maage_access *access, int manage_accounts)
{
    @autoreleasepool {
        [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        MaaGEAccessController *controller = [[MaaGEAccessController alloc] init]; controller.access = access; controller.manageAccounts = manage_accounts != 0;
        id previous = NSApp.delegate; NSApp.delegate = controller;
        NSMenu *menu = [[NSMenu alloc] init]; NSMenuItem *item = [[NSMenuItem alloc] init]; [menu addItem:item];
        NSMenu *application = [[NSMenu alloc] initWithTitle:@"MaaGE"];
        NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit MaaGE" action:@selector(cancelAccess:) keyEquivalent:@"q"]; quit.target = controller; [application addItem:quit]; item.submenu = application; NSApp.mainMenu = menu;
        [controller show]; [NSApp activateIgnoringOtherApps:YES]; [NSApp run];
        NSApp.delegate = previous; NSApp.mainMenu = nil; controller.window.delegate = nil; [controller.window close]; return controller.result;
    }
}
