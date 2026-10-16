#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include "setup.h"
#include <string.h>

static NSColor *maage_setup_color(CGFloat value)
{ return [NSColor colorWithSRGBRed:value green:value blue:value alpha:1]; }
@interface MaaGESetupProgress : NSProgressIndicator
@end
@implementation MaaGESetupProgress
- (void)drawRect:(NSRect)dirty
{
    if (self.indeterminate) { [super drawRect:dirty]; return; }
    [maage_setup_color(0.24) setFill];
    [[NSBezierPath bezierPathWithRoundedRect:self.bounds xRadius:4 yRadius:4] fill];
    NSRect fill = self.bounds;
    fill.size.width *= self.doubleValue / 100.0;
    [[NSColor colorWithSRGBRed:0.87 green:0.19 blue:0.62 alpha:1] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:fill xRadius:4 yRadius:4] fill];
}
@end
@interface MaaGESetupController : NSObject <NSWindowDelegate, NSApplicationDelegate>
@property(nonatomic, assign) maage_assets *assets;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTextField *heading, *subtitle, *directories, *metadata, *resources, *client, *reconstruction, *completion, *percent, *count, *detail;
@property(nonatomic, strong) MaaGESetupProgress *progress;
@property(nonatomic, strong) NSButton *cancel, *next;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic) BOOL closing;
@property(nonatomic) int result;
@end
static NSTextField *maage_setup_label(NSView *parent, NSString *value, NSRect rect, CGFloat size, BOOL bold)
{
    NSTextField *label = [NSTextField wrappingLabelWithString:value];
    label.frame = rect;
    label.font = bold ? [NSFont systemFontOfSize:size weight:NSFontWeightSemibold] : [NSFont systemFontOfSize:size];
    label.textColor = maage_setup_color(0.90);
    [parent addSubview:label]; return label;
}
@implementation MaaGESetupController
- (void)finish:(int)result
{
    self.result = result;
    [self.timer invalidate]; self.timer = nil;
    [self.window orderOut:nil];
    [NSApp stop:nil];
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
}
- (void)cancelSetup:(id)sender
{
    (void)sender;
    self.closing = YES;
    maage_assets_cancel(self.assets);
    self.cancel.enabled = NO; self.next.enabled = NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)application
{ (void)application; [self cancelSetup:nil]; return NSTerminateCancel; }
- (BOOL)windowShouldClose:(NSWindow *)sender
{ (void)sender; [self cancelSetup:nil]; return NO; }
- (void)continueSetup:(id)sender
{
    maage_setup_progress p;
    (void)sender;
    maage_assets_snapshot(self.assets, &p);
    if (!maage_assets_access_allowed(self.assets) && (p.stage == MAAGE_SETUP_FAILED || p.stage == MAAGE_SETUP_COMPLETE)) [self finish:2];
    else if (p.stage == MAAGE_SETUP_COMPLETE && maage_assets_access_allowed(self.assets)) [self finish:1];
    else if (p.stage == MAAGE_SETUP_FAILED) { maage_assets_retry(self.assets); [self refresh:nil]; }
}
- (void)refresh:(NSTimer *)timer
{
    maage_setup_progress p;
    maage_setup_text t;
    (void)timer;
    maage_assets_snapshot(self.assets, &p); maage_setup_describe(&p, &t);
    if (p.stage == MAAGE_SETUP_COMPLETE && !maage_assets_access_allowed(self.assets)) {
        p.stage = MAAGE_SETUP_FAILED; p.needs_setup = 1;
        strcpy(p.error, "Minecraft access expired. Choose Verify Access and verify again. Existing resources are preserved.");
        maage_setup_describe(&p, &t);
    }
    if (self.closing && (p.stage == MAAGE_SETUP_COMPLETE || p.stage == MAAGE_SETUP_FAILED || p.stage == MAAGE_SETUP_CANCELLED)) { [self finish:0]; return; }
    if (p.stage == MAAGE_SETUP_COMPLETE && !p.needs_setup && maage_assets_access_allowed(self.assets)) { [self finish:1]; return; }
    self.heading.stringValue = @(t.heading); self.subtitle.stringValue = @(t.subtitle);
    self.directories.stringValue = @(t.directories); self.metadata.stringValue = @(t.metadata);
    self.resources.stringValue = @(t.resources); self.client.stringValue = @(t.client);
    self.reconstruction.stringValue = @(t.reconstruction); self.completion.stringValue = @(t.completion); self.count.stringValue = @(t.count); self.detail.stringValue = @(t.detail);
    self.percent.frame = NSMakeRect(484, t.complete ? 73 : p.stage == MAAGE_SETUP_CLIENT_ARCHIVE ? 135 : p.objects_ready ? 104 : 166, 60, 23);
    self.percent.stringValue = p.percent < 0 ? @"" : [NSString stringWithFormat:@"%d%%", p.percent];
    self.progress.indeterminate = p.percent < 0;
    if (p.percent < 0) [self.progress startAnimation:nil];
    else { [self.progress stopAnimation:nil]; self.progress.doubleValue = p.percent; }
    [self.progress setNeedsDisplay:YES];
    if (t.failed && !maage_assets_access_allowed(self.assets)) t.button = "Verify Access";
    self.next.title = @(t.button); self.next.enabled = t.actionable && !self.closing && (!t.complete || maage_assets_access_allowed(self.assets));
    if (!self.window.visible && (p.needs_setup || t.failed)) [self.window makeKeyAndOrderFront:nil];
}
- (void)show
{
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 620, 561)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"MaaGE Launcher — Initial setup";
    self.window.releasedWhenClosed = NO; self.window.delegate = self;
    self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    self.window.backgroundColor = maage_setup_color(0.11);
    NSView *content = self.window.contentView;
    maage_setup_label(content, @"MaaGE Launcher", NSMakeRect(28, 518, 360, 22), 14, YES);
    NSTextField *initial = maage_setup_label(content, @"Initial setup", NSMakeRect(474, 518, 118, 22), 12, NO);
    initial.alignment = NSTextAlignmentRight; initial.textColor = maage_setup_color(0.62);
    NSTextField *icon = maage_setup_label(content, @"↓", NSMakeRect(30, 446, 38, 43), 32, NO);
    icon.textColor = [NSColor colorWithSRGBRed:0.91 green:0.29 blue:0.68 alpha:1];
    self.heading = maage_setup_label(content, @"Preparing MaaGE", NSMakeRect(78, 461, 500, 32), 25, YES);
    self.subtitle = maage_setup_label(content, @"", NSMakeRect(79, 418, 480, 38), 13, NO);
    self.subtitle.textColor = maage_setup_color(0.69);
    NSView *panel = [[NSView alloc] initWithFrame:NSMakeRect(28, 139, 564, 267)];
    panel.wantsLayer = YES; panel.layer.backgroundColor = maage_setup_color(0.16).CGColor; panel.layer.cornerRadius = 8;
    [content addSubview:panel];
    self.directories = maage_setup_label(panel, @"", NSMakeRect(20, 228, 520, 22), 13, NO);
    self.metadata = maage_setup_label(panel, @"", NSMakeRect(20, 197, 520, 22), 13, NO);
    self.resources = maage_setup_label(panel, @"", NSMakeRect(20, 166, 465, 23), 13, YES);
    self.client = maage_setup_label(panel, @"", NSMakeRect(20, 135, 465, 23), 13, NO);
    self.reconstruction = maage_setup_label(panel, @"", NSMakeRect(20, 104, 465, 23), 13, NO);
    self.completion = maage_setup_label(panel, @"", NSMakeRect(20, 73, 465, 23), 13, NO);
    self.percent = maage_setup_label(panel, @"", NSMakeRect(484, 135, 60, 23), 13, YES);
    self.percent.alignment = NSTextAlignmentRight;
    self.progress = [[MaaGESetupProgress alloc] initWithFrame:NSMakeRect(22, 48, 520, 8)];
    self.progress.style = NSProgressIndicatorStyleBar; self.progress.minValue = 0; self.progress.maxValue = 100;
    self.progress.accessibilityLabel = @"Resource setup progress";
    [panel addSubview:self.progress];
    self.count = maage_setup_label(panel, @"", NSMakeRect(22, 14, 520, 23), 12, NO);
    self.count.textColor = maage_setup_color(0.66);
    self.detail = maage_setup_label(content, @"", NSMakeRect(30, 67, 560, 62), 12, NO);
    self.detail.textColor = maage_setup_color(0.70);
    self.cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelSetup:)];
    self.cancel.frame = NSMakeRect(378, 23, 100, 32); self.cancel.keyEquivalent = @"\e";
    self.next = [NSButton buttonWithTitle:@"Continue" target:self action:@selector(continueSetup:)];
    self.next.frame = NSMakeRect(488, 23, 104, 32); self.next.keyEquivalent = @"\r"; self.next.enabled = NO;
    [content addSubview:self.cancel]; [content addSubview:self.next];
    [self.window center];
    self.timer = [NSTimer scheduledTimerWithTimeInterval:0.1 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
}
@end
int maage_setup_native_run(maage_assets *assets)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        MaaGESetupController *controller = [[MaaGESetupController alloc] init];
        controller.assets = assets;
        id previousDelegate = NSApp.delegate;
        NSApp.delegate = controller;
        NSMenu *menu = [[NSMenu alloc] init];
        NSMenuItem *item = [[NSMenuItem alloc] init]; [menu addItem:item];
        NSMenu *application = [[NSMenu alloc] initWithTitle:@"MaaGE"];
        NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit MaaGE" action:@selector(cancelSetup:) keyEquivalent:@"q"];
        quit.target = controller; [application addItem:quit]; item.submenu = application; NSApp.mainMenu = menu;
        [controller show]; [NSApp activateIgnoringOtherApps:YES]; [NSApp run];
        NSApp.delegate = previousDelegate; NSApp.mainMenu = nil; controller.window.delegate = nil; [controller.window close];
        return controller.result;
    }
}
