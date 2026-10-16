#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include "../access/access.h"
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
@property(nonatomic, strong) NSPopUpButton *profiles;
@property(nonatomic, strong) NSTextField *identity, *status;
@property(nonatomic, strong) NSButton *connect, *disconnect, *rescan, *manual, *next, *cancel;
@property(nonatomic, strong) NSProgressIndicator *progress;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic) unsigned revision;
@property(nonatomic) BOOL closing;
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
- (void)selectProfile:(id)sender
{ (void)sender; [self refresh:nil]; }
- (void)connectProfile:(id)sender
{
    (void)sender;
    NSString *uuid = self.profiles.selectedItem.representedObject;
    if (!uuid || _snapshot.busy) return;
    NSAlert *alert = [[NSAlert alloc] init]; alert.messageText = @"Connect and Verify Minecraft Access";
    alert.informativeText = [NSString stringWithFormat:@"%@\n\n%s", self.identity.stringValue, MAAGE_ACCESS_CONSENT];
    [alert addButtonWithTitle:@"Connect & Verify"]; [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] == NSAlertFirstButtonReturn) maage_access_connect(self.access, uuid.UTF8String);
    [self refresh:nil];
}
- (void)disconnectProfile:(id)sender
{ (void)sender; NSString *uuid = self.profiles.selectedItem.representedObject; if (uuid) maage_access_disconnect(self.access, uuid.UTF8String); [self refresh:nil]; }
- (void)scan:(id)sender
{ (void)sender; maage_access_scan(self.access, NULL); [self refresh:nil]; }
- (void)chooseDirectory:(id)sender
{
    (void)sender;
    NSOpenPanel *picker = [NSOpenPanel openPanel]; picker.canChooseDirectories = YES; picker.canChooseFiles = NO;
    picker.allowsMultipleSelection = NO; picker.title = @"Choose Launcher Configuration Folder";
    picker.message = @"Choose the folder containing accounts.json (PrismLauncher/MultiMC) or launcher_accounts.json.";
    if ([picker runModal] == NSModalResponseOK) maage_access_scan(self.access, picker.URL.path.UTF8String);
    [self refresh:nil];
}
- (void)refresh:(NSTimer *)timer
{
    (void)timer;
    NSString *selected = self.profiles.selectedItem.representedObject;
    maage_access_snapshot_get(self.access, &_snapshot);
    if (self.closing && !_snapshot.busy) { [self finish:0]; return; }
    if (self.revision != _snapshot.revision) {
        [self.profiles removeAllItems];
        for (unsigned i = 0; i < _snapshot.count; ++i) {
            maage_access_profile *p = &_snapshot.profiles[i];
            NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"%s — %s", p->name, maage_access_state_name(p->state)] action:NULL keyEquivalent:@""];
            [self.profiles.menu addItem:item];
            self.profiles.lastItem.representedObject = @(p->uuid);
            if ([selected isEqualToString:@(p->uuid)]) [self.profiles selectItem:self.profiles.lastItem];
        }
        if (!_snapshot.count) [self.profiles addItemWithTitle:@"No Minecraft accounts discovered"];
        if (self.profiles.indexOfSelectedItem < 0) [self.profiles selectItemAtIndex:0];
        self.revision = _snapshot.revision;
    }
    NSInteger index = self.profiles.indexOfSelectedItem;
    maage_access_profile *p = index >= 0 && (unsigned)index < _snapshot.count ? &_snapshot.profiles[index] : NULL;
    self.identity.stringValue = p ? [NSString stringWithFormat:@"%s\n%s · %s\n%s", p->uuid, p->sources[0] ? p->sources : "Source unavailable", maage_access_state_name(p->state), p->detail] : @"Sign in through an existing launcher, then rescan. No account names are generated by MaaGE.";
    self.status.stringValue = @(_snapshot.status);
    BOOL idle = !_snapshot.busy && !self.closing;
    self.profiles.enabled = idle && _snapshot.count > 0;
    self.connect.enabled = idle && p && p->available;
    self.connect.title = p && p->connected ? @"Verify Again" : @"Connect & Verify";
    self.disconnect.enabled = idle && p && p->connected;
    self.rescan.enabled = idle; self.manual.enabled = idle; self.cancel.enabled = !self.closing;
    self.next.enabled = idle && _snapshot.allowed;
    if (_snapshot.busy) [self.progress startAnimation:nil]; else [self.progress stopAnimation:nil];
}
- (NSButton *)button:(NSString *)title action:(SEL)action frame:(NSRect)frame parent:(NSView *)parent
{
    NSButton *button = [NSButton buttonWithTitle:title target:self action:action]; button.frame = frame; [parent addSubview:button]; return button;
}
- (void)show
{
    self.revision = (unsigned)-1;
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 650, 610) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"MaaGE Launcher — Initial setup"; self.window.releasedWhenClosed = NO;
    self.window.delegate = self; self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]; self.window.backgroundColor = maage_access_color(.11);
    NSView *content = self.window.contentView;
    maage_access_label(content, @"MaaGE Launcher", NSMakeRect(28, 565, 240, 23), 14, YES);
    NSTextField *step = maage_access_label(content, @"Initial setup · 1 of 2", NSMakeRect(422, 565, 200, 23), 12, NO); step.alignment = NSTextAlignmentRight;
    NSTextField *icon = maage_access_label(content, @"✓", NSMakeRect(30, 494, 42, 38), 30, YES); icon.textColor = [NSColor colorWithSRGBRed:.91 green:.29 blue:.68 alpha:1];
    maage_access_label(content, @"Verify Minecraft Access", NSMakeRect(78, 500, 540, 35), 25, YES);
    maage_access_label(content, @"Connect an existing Minecraft account to continue setting up MaaGE.", NSMakeRect(79, 450, 535, 42), 13, NO);
    NSView *panel = [[NSView alloc] initWithFrame:NSMakeRect(28, 270, 594, 172)]; panel.wantsLayer = YES; panel.layer.backgroundColor = maage_access_color(.16).CGColor; panel.layer.cornerRadius = 8; [content addSubview:panel];
    self.profiles = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(16, 129, 562, 28) pullsDown:NO]; self.profiles.menu.autoenablesItems = NO; self.profiles.target = self; self.profiles.action = @selector(selectProfile:); [panel addSubview:self.profiles];
    self.identity = maage_access_label(panel, @"", NSMakeRect(20, 48, 554, 78), 12, NO);
    self.disconnect = [self button:@"Disconnect" action:@selector(disconnectProfile:) frame:NSMakeRect(274, 10, 126, 30) parent:panel];
    self.connect = [self button:@"Connect & Verify" action:@selector(connectProfile:) frame:NSMakeRect(404, 10, 174, 30) parent:panel];
    maage_access_label(content, @"Scan for Minecraft Accounts", NSMakeRect(32, 227, 370, 24), 13, YES);
    self.rescan = [self button:@"Rescan" action:@selector(scan:) frame:NSMakeRect(516, 222, 106, 31) parent:content];
    NSButton *coming = [NSButton buttonWithTitle:@"Verify Through Minecraft — Coming Soon" target:nil action:nil]; coming.frame = NSMakeRect(28, 165, 594, 43); coming.enabled = NO; [content addSubview:coming];
    self.manual = [self button:@"Choose Launcher Manually" action:@selector(chooseDirectory:) frame:NSMakeRect(28, 116, 244, 32) parent:content];
    self.progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(596, 122, 20, 20)]; self.progress.style = NSProgressIndicatorStyleSpinning; self.progress.displayedWhenStopped = NO; self.progress.accessibilityLabel = @"Account operation in progress"; [content addSubview:self.progress];
    self.status = maage_access_label(content, @"", NSMakeRect(30, 57, 590, 53), 12, NO); self.status.textColor = maage_access_color(.70);
    self.cancel = [self button:@"Cancel" action:@selector(cancelAccess:) frame:NSMakeRect(402, 17, 100, 32) parent:content]; self.cancel.keyEquivalent = @"\e";
    self.next = [self button:@"Continue" action:@selector(continueAccess:) frame:NSMakeRect(512, 17, 110, 32) parent:content]; self.next.keyEquivalent = @"\r";
    [self refresh:nil]; [self.window center]; [self.window makeKeyAndOrderFront:nil];
    self.timer = [NSTimer scheduledTimerWithTimeInterval:.1 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
}
@end
int maage_access_native_run(maage_access *access)
{
    @autoreleasepool {
        [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        MaaGEAccessController *controller = [[MaaGEAccessController alloc] init]; controller.access = access;
        id previous = NSApp.delegate; NSApp.delegate = controller;
        NSMenu *menu = [[NSMenu alloc] init]; NSMenuItem *item = [[NSMenuItem alloc] init]; [menu addItem:item];
        NSMenu *application = [[NSMenu alloc] initWithTitle:@"MaaGE"];
        NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit MaaGE" action:@selector(cancelAccess:) keyEquivalent:@"q"]; quit.target = controller; [application addItem:quit]; item.submenu = application; NSApp.mainMenu = menu;
        [controller show]; [NSApp activateIgnoringOtherApps:YES]; [NSApp run];
        NSApp.delegate = previous; NSApp.mainMenu = nil; controller.window.delegate = nil; [controller.window close]; return controller.result;
    }
}
