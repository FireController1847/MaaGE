#import <AppKit/AppKit.h>
#include "launcher.h"
#include "maage/launcher_version.h"
#include <string.h>

static NSStackView *maage_ui_stack(BOOL horizontal, CGFloat spacing)
{
    NSStackView *stack = [[NSStackView alloc] init];
    stack.orientation = horizontal ? NSUserInterfaceLayoutOrientationHorizontal : NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = horizontal ? NSLayoutAttributeCenterY : NSLayoutAttributeLeading;
    stack.spacing = spacing; return stack;
}
static NSTextField *maage_ui_label(NSString *text, CGFloat size, BOOL bold)
{
    NSTextField *label = [NSTextField wrappingLabelWithString:text];
    label.font = [NSFont systemFontOfSize:size weight:bold ? NSFontWeightSemibold : NSFontWeightRegular];
    label.textColor = NSColor.labelColor; return label;
}
static void maage_ui_fill(NSView *child, NSView *parent, CGFloat margin)
{
    child.translatesAutoresizingMaskIntoConstraints = NO; [parent addSubview:child];
    [NSLayoutConstraint activateConstraints:@[
        [child.leadingAnchor constraintEqualToAnchor:parent.leadingAnchor constant:margin],
        [child.trailingAnchor constraintEqualToAnchor:parent.trailingAnchor constant:-margin],
        [child.topAnchor constraintEqualToAnchor:parent.topAnchor constant:margin],
        [child.bottomAnchor constraintEqualToAnchor:parent.bottomAnchor constant:-margin]]];
}
@interface MaaGELauncherController : NSObject <NSWindowDelegate, NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, assign) maage_launcher_ui *state;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTableView *sidebar, *installations;
@property(nonatomic, strong) NSView *body;
@property(nonatomic, strong) NSArray<NSView *> *pages;
@property(nonatomic, strong) NSPopUpButton *account, *installation;
@property(nonatomic, strong) NSButton *status, *repair, *verify;
@property(nonatomic, strong) NSTextField *resourceStatus, *accountStatus, *error;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic, strong) NSArray<NSButton *> *installationActions;
@property(nonatomic) unsigned accountRevision;
@property(nonatomic) int result;
@end
@implementation MaaGELauncherController
- (NSButton *)button:(NSString *)title action:(SEL)action
{ return [NSButton buttonWithTitle:title target:self action:action]; }
- (void)finish:(int)result
{
    self.result = result; [self.timer invalidate]; self.timer = nil;
    [self.window orderOut:nil]; [NSApp stop:nil];
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
}
- (void)quit:(id)sender { (void)sender; [self finish:0]; }
- (BOOL)windowShouldClose:(NSWindow *)window { (void)window; [self finish:0]; return NO; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)application
{ (void)application; [self finish:0]; return NSTerminateCancel; }
- (void)manage:(id)sender { (void)sender; if (!self.state->accounts.busy) [self finish:1]; }
- (void)repairAssets:(id)sender
{ (void)sender; if (maage_access_allowed(self.state->access)) [self finish:2]; else [self manage:nil]; }
- (void)verifyAccount:(id)sender
{
    (void)sender;
    if (self.state->account[0]) maage_access_verify_local(self.state->access, self.state->account);
    [self refresh:nil];
}
- (void)openProject:(id)sender { (void)sender; [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@MAAGE_PROJECT_URL]]; }
- (void)showSettings:(id)sender
{
    (void)sender; self.state->page = MAAGE_PAGE_SETTINGS; [self.sidebar deselectAll:nil];
    for (NSInteger i = 0; i < 4; ++i) self.pages[i].hidden = i != 3;
}
- (void)selectAccount:(id)sender
{
    (void)sender;
    NSString *uuid = self.account.selectedItem.representedObject;
    if (uuid) maage_launcher_account_select(self.state, uuid.UTF8String);
    else if (self.account.selectedItem.tag == 1) { [self manage:nil]; return; }
    else if (self.account.selectedItem.tag == 2) [self verifyAccount:nil];
    self.accountRevision = 0;
    [self refresh:nil];
}
- (void)selectInstallation:(id)sender
{ (void)sender; maage_launcher_installation_select(self.state, (unsigned)self.installation.indexOfSelectedItem); [self reloadInstallations]; }
- (NSInteger)numberOfRowsInTableView:(NSTableView *)table
{ return table == self.sidebar ? 3 : self.state->count; }
- (NSView *)tableView:(NSTableView *)table viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row
{
    (void)column;
    if (table == self.sidebar) {
        NSArray *names = @[@"Play", @"News", @"Installations", @"Settings"];
        NSArray *symbols = @[@"play", @"newspaper", @"square.stack", @"gearshape"];
        NSTableCellView *cell = [[NSTableCellView alloc] init];
        NSStackView *line = maage_ui_stack(YES, 10);
        if (@available(macOS 11.0, *)) {
            NSImageView *icon = [NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:symbols[row] accessibilityDescription:names[row]]];
            [icon.widthAnchor constraintEqualToConstant:18].active = YES; [line addArrangedSubview:icon];
        }
        [line addArrangedSubview:maage_ui_label(names[row], 13, NO)]; maage_ui_fill(line, cell, 5); return cell;
    }
    maage_installation *item = &self.state->installations[row];
    NSString *suffix = item->id == self.state->default_id ? @" · Default" : @"";
    NSString *selected = item->id == self.state->selected ? @" · Selected" : @"";
    NSStackView *line = maage_ui_stack(NO, 4);
    [line addArrangedSubview:maage_ui_label([NSString stringWithFormat:@"%s%@%@", item->name, suffix, selected], 14, YES)];
    NSTextField *summary = maage_ui_label(@"Minecraft 1.7.10 resources · Native MaaGE Client", 12, NO);
    summary.textColor = NSColor.secondaryLabelColor; [line addArrangedSubview:summary]; return line;
}
- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
    if (notification.object == self.sidebar) {
        NSInteger index = self.sidebar.selectedRow;
        if (index < 0 || index > 3) return;
        self.state->page = (maage_launcher_page)index;
        for (NSInteger i = 0; i < 4; ++i) self.pages[i].hidden = i != index;
    }
    [self refresh:nil];
}
- (void)reloadInstallations
{
    [self.installation removeAllItems];
    for (unsigned i = 0; i < self.state->count; ++i) {
        [self.installation addItemWithTitle:@(self.state->installations[i].name)];
        if (self.state->installations[i].id == self.state->selected) [self.installation selectItemAtIndex:i];
    }
    [self.installations reloadData]; [self refresh:nil];
}
- (void)editInstallation:(NSButton *)sender
{
    NSInteger row = self.installations.selectedRow;
    BOOL creating = sender.tag != 1;
    if (!creating && row < 0) return;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = sender.tag == 1 ? @"Edit Installation" : sender.tag == 2 ? @"Duplicate Installation" : @"New Installation";
    alert.informativeText = @"Name this MaaGE configuration. The resource baseline is Minecraft 1.7.10. Client startup settings will be available when launching is implemented.";
    NSTextField *name = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 360, 24)];
    name.stringValue = row >= 0 && sender.tag ? @(self.state->installations[row].name) : @"New Installation";
    if (sender.tag == 2 && row >= 0) name.stringValue = [name.stringValue stringByAppendingString:@" Copy"];
    name.accessibilityLabel = @"Installation name"; alert.accessoryView = name;
    [alert addButtonWithTitle:@"Save"]; [alert addButtonWithTitle:@"Cancel"];
    [alert.window setInitialFirstResponder:name];
    if ([alert runModal] == NSAlertFirstButtonReturn) {
        maage_launcher_installation_write(self.state, creating ? -1 : (int)row, name.stringValue.UTF8String);
        [self reloadInstallations];
    }
}
- (void)removeInstallation:(id)sender
{
    (void)sender; NSInteger row = self.installations.selectedRow; if (row < 0) return;
    NSAlert *alert = [[NSAlert alloc] init]; alert.messageText = @"Remove Installation?";
    alert.informativeText = @"Only this MaaGE configuration will be removed. Minecraft accounts and resources are preserved.";
    [alert addButtonWithTitle:@"Remove"]; [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] == NSAlertFirstButtonReturn) { maage_launcher_installation_remove(self.state, (unsigned)row); [self reloadInstallations]; }
}
- (void)defaultInstallation:(id)sender
{ (void)sender; NSInteger row = self.installations.selectedRow; if (row >= 0) { maage_launcher_installation_default(self.state, (unsigned)row); [self reloadInstallations]; } }
- (void)applyAppearance
{
    self.window.appearance = self.state->appearance ? [NSAppearance appearanceNamed:self.state->appearance == 1 ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua] : nil;
    self.window.backgroundColor = NSColor.windowBackgroundColor;
}
- (void)appearanceChanged:(NSPopUpButton *)sender
{
    int previous = self.state->appearance; self.state->appearance = (int)sender.indexOfSelectedItem;
    if (!maage_launcher_ui_save(self.state)) { self.state->appearance = previous; [sender selectItemAtIndex:previous]; }
    [self applyAppearance]; [self refresh:nil];
}
- (void)refresh:(NSTimer *)timer
{
    (void)timer; maage_launcher_ui_refresh(self.state);
    maage_launcher_ui *state = self.state;
    if (self.accountRevision != state->accounts.revision || !self.account.numberOfItems) {
        [self.account removeAllItems];
        for (unsigned i = 0; i < state->accounts.count; ++i) {
            maage_access_profile *p = &state->accounts.profiles[i]; if (!p->connected) continue;
            [self.account addItemWithTitle:[NSString stringWithFormat:@"%s · %s", p->name, maage_access_state_name(p->state)]];
            self.account.lastItem.representedObject = @(p->uuid);
            if (!strcmp(state->account, p->uuid)) [self.account selectItem:self.account.lastItem];
        }
        if (!self.account.numberOfItems) [self.account addItemWithTitle:@"No connected Minecraft account"];
        NSMenuItem *chosen = self.account.selectedItem;
        [self.account.menu addItem:NSMenuItem.separatorItem];
        [self.account addItemWithTitle:@"Manage Connected Accounts…"]; self.account.lastItem.tag = 1;
        [self.account addItemWithTitle:@"Verify Saved Account"]; self.account.lastItem.tag = 2;
        self.account.lastItem.enabled = !state->accounts.busy && state->account[0];
        [self.account selectItem:chosen];
        self.account.autoenablesItems = NO;
        self.accountRevision = state->accounts.revision;
    }
    self.account.enabled = !state->accounts.busy;
    self.accountStatus.stringValue = @(state->account_status);
    self.verify.enabled = !state->accounts.busy && state->account[0];
    self.status.title = @(state->readiness);
    self.status.toolTip = @"Open resource and account settings";
    self.resourceStatus.stringValue = state->progress.stage == MAAGE_SETUP_FAILED ? @(state->progress.error) : @(state->readiness);
    self.repair.enabled = !state->accounts.busy;
    self.repair.title = state->accounts.allowed ? @"Repair Assets…" : @"Verify Minecraft Access…";
    self.error.stringValue = @(state->error);
    for (NSButton *button in self.installationActions) button.enabled = button.tag == 0 || self.installations.selectedRow >= 0;
}
- (NSView *)playPage
{
    NSView *page = [[NSView alloc] init];
    NSStackView *header = maage_ui_stack(YES, 12); header.translatesAutoresizingMaskIntoConstraints = NO;
    [page addSubview:header];
    [header addArrangedSubview:maage_ui_label(@"Play MaaGE", 24, YES)];
    NSView *space = [[NSView alloc] init]; [header addArrangedSubview:space];
    self.account = [[NSPopUpButton alloc] init]; self.account.target = self; self.account.action = @selector(selectAccount:);
    self.account.accessibilityLabel = @"Active Minecraft account";
    [self.account.widthAnchor constraintLessThanOrEqualToConstant:270].active = YES;
    [header addArrangedSubview:self.account];
    [NSLayoutConstraint activateConstraints:@[[header.leadingAnchor constraintEqualToAnchor:page.leadingAnchor], [header.trailingAnchor constraintEqualToAnchor:page.trailingAnchor], [header.topAnchor constraintEqualToAnchor:page.topAnchor]]];
    NSStackView *center = maage_ui_stack(NO, 14); center.alignment = NSLayoutAttributeCenterX;
    NSImageView *icon = [NSImageView imageViewWithImage:NSApp.applicationIconImage];
    [icon.widthAnchor constraintEqualToConstant:48].active = YES; [icon.heightAnchor constraintEqualToConstant:48].active = YES;
    [center addArrangedSubview:icon]; [center addArrangedSubview:maage_ui_label(@"MaaGE", 32, YES)];
    NSTextField *subtitle = maage_ui_label(@"Minecraft as a Game Engine", 14, NO); subtitle.textColor = NSColor.secondaryLabelColor; [center addArrangedSubview:subtitle];
    self.installation = [[NSPopUpButton alloc] init]; self.installation.target = self; self.installation.action = @selector(selectInstallation:);
    self.installation.accessibilityLabel = @"MaaGE installation"; [self.installation.widthAnchor constraintEqualToConstant:260].active = YES;
    [center addArrangedSubview:self.installation];
    NSButton *play = [self button:@"▶  Play" action:NULL]; play.bezelStyle = NSBezelStyleRounded; play.font = [NSFont systemFontOfSize:18 weight:NSFontWeightSemibold];
    play.bezelColor = [NSColor colorWithSRGBRed:.87 green:.19 blue:.62 alpha:1]; play.enabled = NO;
    play.toolTip = @"Client startup is not implemented in this build.";
    [play.widthAnchor constraintEqualToConstant:260].active = YES; [play.heightAnchor constraintEqualToConstant:44].active = YES;
    [center addArrangedSubview:play];
    NSTextField *reason = maage_ui_label(@"Client launching is not available in this build.", 12, NO); reason.textColor = NSColor.secondaryLabelColor; [center addArrangedSubview:reason];
    center.translatesAutoresizingMaskIntoConstraints = NO; [page addSubview:center];
    [NSLayoutConstraint activateConstraints:@[[center.centerXAnchor constraintEqualToAnchor:page.centerXAnchor], [center.centerYAnchor constraintEqualToAnchor:page.centerYAnchor constant:15], [center.topAnchor constraintGreaterThanOrEqualToAnchor:header.bottomAnchor constant:24], [center.bottomAnchor constraintLessThanOrEqualToAnchor:page.bottomAnchor constant:-12]]];
    return page;
}
- (NSView *)newsPage
{
    NSStackView *page = maage_ui_stack(NO, 14);
    [page addArrangedSubview:maage_ui_label(@"News", 24, YES)];
    [page addArrangedSubview:maage_ui_label(@"No news source is configured yet.", 15, NO)];
    [page addArrangedSubview:maage_ui_label(@"Project updates are available in the MaaGE repository.", 13, NO)];
    [page addArrangedSubview:[self button:@"Open MaaGE Project ↗" action:@selector(openProject:)]];
    [page addArrangedSubview:[[NSView alloc] init]]; return page;
}
- (NSView *)installationsPage
{
    NSStackView *page = maage_ui_stack(NO, 14);
    [page addArrangedSubview:maage_ui_label(@"Installations", 24, YES)];
    [page addArrangedSubview:maage_ui_label(@"MaaGE configurations · Minecraft 1.7.10 resource baseline", 13, NO)];
    self.installations = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 640, 300)]; self.installations.headerView = nil;
    self.installations.autoresizingMask = NSViewWidthSizable;
    self.installations.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"installation"]; column.width = 640; [self.installations addTableColumn:column];
    self.installations.delegate = self; self.installations.dataSource = self; self.installations.rowHeight = 55;
    self.installations.accessibilityLabel = @"MaaGE installations";
    NSScrollView *scroll = [[NSScrollView alloc] init]; scroll.hasVerticalScroller = YES; scroll.borderType = NSBezelBorder; scroll.documentView = self.installations;
    [page addArrangedSubview:scroll]; [scroll.widthAnchor constraintEqualToAnchor:page.widthAnchor].active = YES;
    NSStackView *actions = maage_ui_stack(YES, 8); NSMutableArray *buttons = [NSMutableArray array];
    NSArray *titles = @[@"New…", @"Edit…", @"Duplicate…", @"Remove…", @"Set as Default"];
    for (NSInteger i = 0; i < 5; ++i) {
        SEL action = i < 3 ? @selector(editInstallation:) : i == 3 ? @selector(removeInstallation:) : @selector(defaultInstallation:);
        NSButton *button = [self button:titles[i] action:action]; button.tag = i; [actions addArrangedSubview:button]; [buttons addObject:button];
    }
    self.installationActions = buttons; [page addArrangedSubview:actions]; return page;
}
- (NSView *)settingsPage
{
    NSStackView *groups = maage_ui_stack(NO, 12);
    [groups addArrangedSubview:maage_ui_label(@"Settings", 24, YES)];
    [groups addArrangedSubview:maage_ui_label(@"Launcher", 15, YES)];
    [groups addArrangedSubview:maage_ui_label(@"Startup opens Play after account and resource preparation.", 13, NO)];
    self.accountStatus = maage_ui_label(@"", 12, NO); [groups addArrangedSubview:self.accountStatus];
    NSStackView *accounts = maage_ui_stack(YES, 8);
    [accounts addArrangedSubview:[self button:@"Manage Minecraft Accounts…" action:@selector(manage:)]];
    self.verify = [self button:@"Verify Saved Account" action:@selector(verifyAccount:)]; [accounts addArrangedSubview:self.verify]; [groups addArrangedSubview:accounts];
    [groups addArrangedSubview:maage_ui_label(@"Resources", 15, YES)];
    NSTextField *path = maage_ui_label(@(self.state->resources), 12, NO); path.selectable = YES; [path.widthAnchor constraintLessThanOrEqualToConstant:560].active = YES; [groups addArrangedSubview:path];
    self.resourceStatus = maage_ui_label(@"", 13, NO); [groups addArrangedSubview:self.resourceStatus];
    self.repair = [self button:@"Repair Assets…" action:@selector(repairAssets:)]; [groups addArrangedSubview:self.repair];
    [groups addArrangedSubview:maage_ui_label(@"Appearance", 15, YES)];
    NSPopUpButton *appearance = [[NSPopUpButton alloc] init]; [appearance addItemsWithTitles:@[@"System", @"Light", @"Dark"]];
    [appearance selectItemAtIndex:self.state->appearance]; appearance.target = self; appearance.action = @selector(appearanceChanged:); appearance.accessibilityLabel = @"Launcher appearance"; [groups addArrangedSubview:appearance];
    [groups addArrangedSubview:maage_ui_label(@"About MaaGE", 15, YES)];
    [groups addArrangedSubview:maage_ui_label(@"Minecraft as a Game Engine\nLauncher " MAAGE_LAUNCHER_VERSION "\nOriginal code: Apache License 2.0. Minecraft resources are separate.\nNot affiliated with Mojang or Microsoft.", 12, NO)];
    [groups addArrangedSubview:[self button:@"MaaGE Project ↗" action:@selector(openProject:)]];
    NSScrollView *scroll = [[NSScrollView alloc] init]; scroll.hasVerticalScroller = YES; scroll.drawsBackground = NO;
    NSView *document = [[NSView alloc] init]; scroll.documentView = document;
    maage_ui_fill(groups, document, 2);
    document.translatesAutoresizingMaskIntoConstraints = NO;
    [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;
    return scroll;
}
- (void)show
{
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1000, 670) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"MaaGE Launcher"; self.window.minSize = NSMakeSize(940, 640); self.window.releasedWhenClosed = NO; self.window.delegate = self;
    [self applyAppearance];
    NSSplitView *split = [[NSSplitView alloc] init]; split.vertical = YES; split.dividerStyle = NSSplitViewDividerStyleThin;
    maage_ui_fill(split, self.window.contentView, 0);
    NSView *sidebar = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 165, 670)];
    NSVisualEffectView *material = [[NSVisualEffectView alloc] init]; material.material = NSVisualEffectMaterialSidebar; material.blendingMode = NSVisualEffectBlendingModeWithinWindow; maage_ui_fill(material, sidebar, 0);
    self.sidebar = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 140, 120)]; self.sidebar.headerView = nil; self.sidebar.rowHeight = 35;
    self.sidebar.autoresizingMask = NSViewWidthSizable;
    self.sidebar.backgroundColor = NSColor.clearColor; self.sidebar.dataSource = self; self.sidebar.delegate = self;
    if (@available(macOS 11.0, *)) self.sidebar.style = NSTableViewStyleSourceList;
    self.sidebar.accessibilityLabel = @"Launcher navigation";
    [self.sidebar addTableColumn:[[NSTableColumn alloc] initWithIdentifier:@"page"]];
    NSTextField *brand = maage_ui_label(@"MAAGE", 12, YES); brand.textColor = NSColor.secondaryLabelColor;
    NSStackView *nav = maage_ui_stack(NO, 16); [nav addArrangedSubview:brand];
    NSScrollView *navigation = [[NSScrollView alloc] init]; navigation.drawsBackground = NO; navigation.documentView = self.sidebar;
    [nav addArrangedSubview:navigation]; [navigation.widthAnchor constraintEqualToAnchor:nav.widthAnchor].active = YES;
    NSButton *settings = [self button:@"Settings" action:@selector(showSettings:)]; settings.bordered = NO;
    if (@available(macOS 11.0, *)) settings.image = [NSImage imageWithSystemSymbolName:@"gearshape" accessibilityDescription:@"Settings"];
    settings.imagePosition = NSImageLeft; [nav addArrangedSubview:settings];
    maage_ui_fill(nav, sidebar, 12);
    NSView *right = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 835, 670)]; [split addSubview:sidebar]; [split addSubview:right];
    [sidebar.widthAnchor constraintGreaterThanOrEqualToConstant:155].active = YES;
    [sidebar.widthAnchor constraintLessThanOrEqualToConstant:210].active = YES;
    NSStackView *content = maage_ui_stack(NO, 14); maage_ui_fill(content, right, 26);
    self.body = [[NSView alloc] init]; [content addArrangedSubview:self.body]; [self.body.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    self.pages = @[[self playPage], [self newsPage], [self installationsPage], [self settingsPage]];
    for (NSView *page in self.pages) { maage_ui_fill(page, self.body, 0); page.hidden = YES; }
    self.error = maage_ui_label(@"", 12, NO); self.error.textColor = NSColor.systemRedColor; [content addArrangedSubview:self.error];
    NSBox *separator = [[NSBox alloc] init]; separator.boxType = NSBoxSeparator; [content addArrangedSubview:separator]; [separator.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    NSStackView *footer = maage_ui_stack(YES, 12);
    NSTextField *version = maage_ui_label(@"Development build · " MAAGE_LAUNCHER_VERSION, 11, NO); version.textColor = NSColor.secondaryLabelColor;
    [footer addArrangedSubview:version]; [footer addArrangedSubview:[[NSView alloc] init]];
    self.status = [self button:@"" action:@selector(showSettings:)]; self.status.bordered = NO; self.status.font = [NSFont systemFontOfSize:11]; [footer addArrangedSubview:self.status];
    [content addArrangedSubview:footer]; [footer.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    [self.sidebar reloadData];
    if (self.state->page == MAAGE_PAGE_SETTINGS) [self showSettings:nil];
    else [self.sidebar selectRowIndexes:[NSIndexSet indexSetWithIndex:self.state->page] byExtendingSelection:NO];
    [self reloadInstallations]; [self refresh:nil];
    self.timer = [NSTimer scheduledTimerWithTimeInterval:.25 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [self.window center]; [self.window makeKeyAndOrderFront:nil];
}
@end
int maage_launcher_native_run(maage_launcher_ui *state)
{
    @autoreleasepool {
        [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        MaaGELauncherController *controller = [[MaaGELauncherController alloc] init]; controller.state = state;
        id previous = NSApp.delegate; NSApp.delegate = controller;
        NSMenu *menu = [[NSMenu alloc] init]; NSMenuItem *item = [[NSMenuItem alloc] init]; [menu addItem:item];
        NSMenu *application = [[NSMenu alloc] initWithTitle:@"MaaGE"];
        NSMenuItem *settings = [[NSMenuItem alloc] initWithTitle:@"Settings…" action:@selector(showSettings:) keyEquivalent:@","]; settings.target = controller; [application addItem:settings];
        [application addItem:NSMenuItem.separatorItem];
        NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit MaaGE" action:@selector(quit:) keyEquivalent:@"q"]; quit.target = controller; [application addItem:quit]; item.submenu = application;
        NSMenuItem *editItem = [[NSMenuItem alloc] init]; [menu addItem:editItem]; NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"]; editItem.submenu = edit;
        [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"]; [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"]; [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"]; [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
        NSApp.mainMenu = menu; [controller show]; [NSApp activateIgnoringOtherApps:YES]; [NSApp run];
        NSApp.delegate = previous; NSApp.mainMenu = nil; controller.window.delegate = nil; [controller.window close]; return controller.result;
    }
}
