#import <AppKit/AppKit.h>
#include "launcher.h"
#include "maage/launcher_version.h"
#include <string.h>

static NSStackView *maage_ui_stack(BOOL horizontal, CGFloat spacing) {
    NSStackView *stack = [[NSStackView alloc] init];
    stack.orientation = horizontal ? NSUserInterfaceLayoutOrientationHorizontal : NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = horizontal ? NSLayoutAttributeCenterY : NSLayoutAttributeLeading;
    stack.spacing = spacing;
    return stack;
}
static NSTextField *maage_ui_label(NSString *text, CGFloat size, BOOL bold) {
    NSTextField *label = [NSTextField wrappingLabelWithString:text];
    label.font = [NSFont systemFontOfSize:size weight:bold ? NSFontWeightSemibold : NSFontWeightRegular];
    label.textColor = NSColor.labelColor;
    return label;
}
static void maage_ui_fill(NSView *child, NSView *parent, CGFloat margin) {
    child.translatesAutoresizingMaskIntoConstraints = NO;
    [parent addSubview:child];
    [NSLayoutConstraint activateConstraints:@[
        [child.leadingAnchor constraintEqualToAnchor:parent.leadingAnchor
                                            constant:margin],
        [child.trailingAnchor constraintEqualToAnchor:parent.trailingAnchor
                                             constant:-margin],
        [child.topAnchor constraintEqualToAnchor:parent.topAnchor
                                        constant:margin],
        [child.bottomAnchor constraintEqualToAnchor:parent.bottomAnchor
                                           constant:-margin]
    ]];
}
static NSTextField *maage_ui_row_label(NSString *text, CGFloat size, BOOL bold) {
    NSTextField *label = [NSTextField labelWithString:text];
    label.font = [NSFont systemFontOfSize:size weight:bold ? NSFontWeightSemibold : NSFontWeightRegular];
    label.lineBreakMode = NSLineBreakByTruncatingTail;
    [label setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationVertical];
    return label;
}
static NSTableCellView *maage_ui_row(NSView *content, CGFloat inset) {
    NSTableCellView *cell = [[NSTableCellView alloc] init];
    content.translatesAutoresizingMaskIntoConstraints = NO;
    [cell addSubview:content];
    [NSLayoutConstraint activateConstraints:@[
        [content.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor
                                              constant:inset],
        [content.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor
                                               constant:-inset],
        [content.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        [content.topAnchor constraintGreaterThanOrEqualToAnchor:cell.topAnchor
                                                       constant:4],
        [content.bottomAnchor constraintLessThanOrEqualToAnchor:cell.bottomAnchor
                                                       constant:-4]
    ]];
    return cell;
}

/* Native navigation with a gentle MaaGE-tinted, rounded selection. */
@interface MaaGELauncherNavigationRow : NSTableRowView
@end
@implementation MaaGELauncherNavigationRow
- (void)drawSelectionInRect:(NSRect)dirtyRect {
    (void) dirtyRect;
    [[NSColor colorWithSRGBRed:0.86 green:0.19 blue:0.61 alpha:0.20] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 1, 2)
                                     xRadius:9
                                     yRadius:9] fill];
}
@end
static NSTableView *maage_ui_navigation_table(void) {
    NSTableView *table = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 160, 150)];
    table.headerView = nil;
    table.rowHeight = 36;
    table.intercellSpacing = NSMakeSize(0, 4);
    table.autoresizingMask = NSViewWidthSizable;
    table.backgroundColor = NSColor.clearColor;
    table.focusRingType = NSFocusRingTypeNone;
    table.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;
    if (@available(macOS 11.0, *)) table.style = NSTableViewStyleSourceList;
    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"page"];
    column.width = 160;
    table.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    [table addTableColumn:column];
    return table;
}
@interface MaaGELauncherController : NSObject <NSWindowDelegate, NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, assign) maage_launcher_ui *state;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTableView *sidebar, *settingsSidebar, *installations, *clients;
@property(nonatomic, strong) NSView *body;
@property(nonatomic, strong) NSArray<NSView *> *pages;
@property(nonatomic, strong) NSPopUpButton *account, *installation;
@property(nonatomic, strong) NSButton *status, *repair, *verify, *play;
@property(nonatomic, strong) NSTextField *resourceStatus, *accountStatus, *error, *clientStatus;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic, strong) NSArray<NSButton *> *installationActions;
@property(nonatomic) unsigned accountRevision, clientRevision;
@property(nonatomic) int result;
@end
@implementation MaaGELauncherController
- (NSButton *)button:(NSString *)title action:(SEL)action {
    return [NSButton buttonWithTitle:title target:self action:action];
}
- (void)finish:(int)result {
    if (self.state->client_job) {
        NSBeep();
        return;
    }
    self.result = result;
    [self.timer invalidate];
    self.timer = nil;
    [self.window orderOut:nil];
    [NSApp stop:nil];
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
}
- (void)quit:(id)sender {
    (void) sender;
    [self finish:0];
}
- (BOOL)windowShouldClose:(NSWindow *)window {
    (void) window;
    [self finish:0];
    return NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)application {
    (void) application;
    [self finish:0];
    return NSTerminateCancel;
}
- (void)manage:(id)sender {
    (void) sender;
    if (!self.state->accounts.busy) [self finish:1];
}
- (void)addAccount:(id)sender {
    (void) sender;
    if (!self.state->client_job && (!maage_status_failed(maage_access_sign_in(self.state->access, 0)))) [self finish:1];
}
- (void)signOutAccount:(id)sender {
    (void) sender;
    if (*self.state->account) maage_access_sign_out(self.state->access, self.state->account);
    [self refresh:nil];
}
- (void)removeAccount:(id)sender {
    (void) sender;
    if (*self.state->account) maage_access_disconnect(self.state->access, self.state->account);
    [self refresh:nil];
}
- (void)repairAssets:(id)sender {
    (void) sender;
    if (maage_access_allowed(self.state->access))
        [self finish:2];
    else
        [self manage:nil];
}
- (void)verifyAccount:(id)sender {
    (void) sender;
    if (self.state->account[0]) maage_access_verify_local(self.state->access, self.state->account);
    [self refresh:nil];
}
- (void)playClient:(id)sender {
    (void) sender;
    maage_launcher_client_play(self.state);
    [self refresh:nil];
}
- (void)openProject:(id)sender {
    (void) sender;
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@MAAGE_PROJECT_URL]];
}
- (void)activatePage:(maage_launcher_page)page {
    self.state->page = page;
    if (page >= MAAGE_PAGE_SETTINGS) {
        NSInteger row = page == MAAGE_PAGE_ABOUT ? 1 : 0;
        [self.sidebar deselectAll:nil];
        if (self.settingsSidebar.selectedRow != row)
            [self.settingsSidebar selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    } else {
        [self.settingsSidebar deselectAll:nil];
        if (self.sidebar.selectedRow != (NSInteger) page)
            [self.sidebar selectRowIndexes:[NSIndexSet indexSetWithIndex:page] byExtendingSelection:NO];
    }
    for (NSUInteger i = 0; i < self.pages.count; ++i)
        self.pages[i].hidden = i != (NSUInteger) page;
}
- (void)showSettings:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_SETTINGS];
}
- (void)showAbout:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_ABOUT];
}
- (void)selectAccount:(id)sender {
    (void) sender;
    NSString *uuid = self.account.selectedItem.representedObject;
    if (uuid)
        maage_launcher_account_select(self.state, uuid.UTF8String);
    else if (self.account.selectedItem.tag == 1) {
        [self manage:nil];
        return;
    } else if (self.account.selectedItem.tag == 2)
        [self verifyAccount:nil];
    else if (self.account.selectedItem.tag == 3) {
        [self addAccount:nil];
        return;
    } else if (self.account.selectedItem.tag == 4)
        [self signOutAccount:nil];
    else if (self.account.selectedItem.tag == 5)
        [self removeAccount:nil];
    self.accountRevision = 0;
    [self refresh:nil];
}
- (void)selectInstallation:(id)sender {
    (void) sender;
    maage_launcher_installation_select(self.state, (unsigned) self.installation.indexOfSelectedItem);
    [self reloadInstallations];
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)table {
    return table == self.sidebar ? 3 : table == self.settingsSidebar ? 2
                                   : table == self.clients           ? self.state->clients->count
                                                                     : self.state->count;
}
- (NSTableRowView *)tableView:(NSTableView *)table rowViewForRow:(NSInteger)row {
    (void) row;
    return table == self.sidebar || table == self.settingsSidebar
               ? [[MaaGELauncherNavigationRow alloc] init]
               : nil;
}
- (NSView *)tableView:(NSTableView *)table viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    (void) column;
    if (table == self.sidebar || table == self.settingsSidebar) {
        if (table == self.settingsSidebar) row += MAAGE_PAGE_SETTINGS;
        NSArray *names = @[ @"Play", @"News", @"Installations", @"Settings", @"About" ];
        NSArray *symbols = @[ @"play", @"newspaper", @"square.stack", @"gearshape", @"info.circle" ];
        NSStackView *line = maage_ui_stack(YES, 11);
        if (@available(macOS 11.0, *)) {
            NSImageView *icon = [NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:symbols[row] accessibilityDescription:names[row]]];
            icon.contentTintColor = NSColor.secondaryLabelColor;
            [icon.widthAnchor constraintEqualToConstant:18].active = YES;
            [line addArrangedSubview:icon];
        }
        [line addArrangedSubview:maage_ui_row_label(names[row], 13, NO)];
        return maage_ui_row(line, 10);
    }
    if (table == self.clients) {
        maage_client_build *build = &self.state->clients->builds[row];
        char title[384];
        maage_client_build_label(build, title);
        NSStackView *line = maage_ui_stack(NO, 3);
        [line addArrangedSubview:maage_ui_row_label(@(title), 12, YES)];
        [line addArrangedSubview:maage_ui_row_label([NSString stringWithFormat:@"%s · %s · %.12s", build->platform, build->architecture, build->id], 11, NO)];
        return maage_ui_row(line, 6);
    }
    maage_installation *item = &self.state->installations[row];
    NSString *suffix = item->id == self.state->default_id ? @" · Default" : @"";
    NSString *selected = item->id == self.state->selected ? @" · Selected" : @"";
    NSStackView *line = maage_ui_stack(NO, 4);
    [line addArrangedSubview:maage_ui_row_label([NSString stringWithFormat:@"%s%@%@", item->name, suffix, selected], 14, YES)];
    NSTextField *summary = maage_ui_row_label(@(maage_launcher_installation_client(self.state, (unsigned) row)), 12, NO);
    summary.textColor = NSColor.secondaryLabelColor;
    [line addArrangedSubview:summary];
    return maage_ui_row(line, 6);
}
- (void)tableViewSelectionDidChange:(NSNotification *)notification {
    if (notification.object == self.settingsSidebar) {
        NSInteger row = self.settingsSidebar.selectedRow;
        if (row >= 0) [self activatePage:(maage_launcher_page) (MAAGE_PAGE_SETTINGS + row)];
        return;
    }
    if (notification.object == self.sidebar) {
        NSInteger row = self.sidebar.selectedRow;
        if (row < 0 || row >= MAAGE_PAGE_SETTINGS) return;
        [self activatePage:(maage_launcher_page) row];
    }
    [self refresh:nil];
}
- (void)reloadInstallations {
    [self.installation removeAllItems];
    for (unsigned i = 0; i < self.state->count; ++i) {
        [self.installation addItemWithTitle:@(self.state->installations[i].name)];
        if (self.state->installations[i].id == self.state->selected) [self.installation selectItemAtIndex:i];
    }
    [self.installations reloadData];
    [self refresh:nil];
}
- (void)editInstallation:(NSButton *)sender {
    NSInteger row = self.installations.selectedRow;
    BOOL creating = sender.tag != 1;
    if (!creating && row < 0) return;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = sender.tag == 1 ? @"Edit Installation" : sender.tag == 2 ? @"Duplicate Installation"
                                                                                 : @"New Installation";
    alert.informativeText = @"Choose a managed Client build for this configuration. Minecraft 1.7.10 resources are shared.";
    NSTextField *name = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 360, 24)];
    name.stringValue = row >= 0 && sender.tag ? @(self.state->installations[row].name) : @"New Installation";
    if (sender.tag == 2 && row >= 0) name.stringValue = [name.stringValue stringByAppendingString:@" Copy"];
    name.accessibilityLabel = @"Installation name";
    /* NSAlert sizes its accessory from its frame. An unframed stack collapses
     * here even when its arranged controls were constructed with frames. */
    NSView *editor = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 420, 200)];
    NSTextField *nameLabel = [NSTextField labelWithString:@"Installation name"];
    nameLabel.frame = NSMakeRect(0, 180, 420, 16);
    [editor addSubview:nameLabel];
    name.frame = NSMakeRect(0, 150, 420, 24);
    [editor addSubview:name];
    NSTextField *clientLabel = [NSTextField labelWithString:@"Client build"];
    clientLabel.frame = NSMakeRect(0, 122, 420, 18);
    [editor addSubview:clientLabel];
    NSPopUpButton *client = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 90, 420, 26)];
    [client addItemWithTitle:@"Client selection required"];
    client.lastItem.representedObject = @"";
    const char *selectedClient = row >= 0 && sender.tag ? self.state->installations[row].client : "";
    for (unsigned i = 0; i < self.state->clients->count; ++i) {
        maage_client_build *build = &self.state->clients->builds[i];
        char label[384];
        maage_client_build_label(build, label);
        [client addItemWithTitle:@(label)];
        client.lastItem.representedObject = @(build->id);
        if (!strcmp(build->id, selectedClient)) [client selectItem:client.lastItem];
    }
    if (*selectedClient && maage_clients_find(self.state->clients, selectedClient) < 0) {
        [client addItemWithTitle:@"Referenced Client missing (keep reference)"];
        client.lastItem.representedObject = @(selectedClient);
        [client selectItem:client.lastItem];
    }
    client.accessibilityLabel = @"Client build";
    [editor addSubview:client];
    alert.accessoryView = editor;
    NSTextField *argumentsLabel = [NSTextField labelWithString:@"Client CLI arguments"];
    argumentsLabel.frame = NSMakeRect(0, 62, 420, 18);
    [editor addSubview:argumentsLabel];
    NSTextField *arguments = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 32, 420, 24)];
    arguments.stringValue = row >= 0 && sender.tag ? @(self.state->installations[row].arguments) : @"";
    arguments.accessibilityLabel = @"Client CLI arguments";
    [editor addSubview:arguments];
    NSTextField *hint = [NSTextField labelWithString:@"Separate arguments with spaces; quote values containing spaces."];
    hint.font = [NSFont systemFontOfSize:11];
    hint.frame = NSMakeRect(0, 6, 420, 18);
    [editor addSubview:hint];
    [alert addButtonWithTitle:@"Save"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert.window setInitialFirstResponder:name];
    while ([alert runModal] == NSAlertFirstButtonReturn) {
        if (!maage_status_failed(maage_launcher_installation_write(self.state, creating ? -1 : (int) row,
                                                                   name.stringValue.UTF8String, [client.selectedItem.representedObject UTF8String], arguments.stringValue.UTF8String))) {
            [self reloadInstallations];
            break;
        }
        alert.informativeText = @(self.state->error);
    }
}
- (void)removeInstallation:(id)sender {
    (void) sender;
    NSInteger row = self.installations.selectedRow;
    if (row < 0) return;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Remove Installation?";
    alert.informativeText = @"This instance will be removed from the Launcher and its gameplay folder archived. Worlds and private files remain recoverable; shared resources are preserved.";
    [alert addButtonWithTitle:@"Remove"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] == NSAlertFirstButtonReturn) {
        maage_launcher_installation_remove(self.state, (unsigned) row);
        [self reloadInstallations];
    }
}
- (void)defaultInstallation:(id)sender {
    (void) sender;
    NSInteger row = self.installations.selectedRow;
    if (row >= 0) {
        maage_launcher_installation_default(self.state, (unsigned) row);
        [self reloadInstallations];
    }
}
- (void)openInstallationFolder:(id)sender {
    (void) sender;
    NSInteger row = self.installations.selectedRow;
    char path[MAAGE_PATH_CAPACITY];
    if (row < 0) return;
    if ((!maage_status_failed(maage_launcher_installation_directory(self.state, (unsigned) row, path))) &&
        ![NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:@(path) isDirectory:YES]])
        strcpy(self.state->error, "Cannot open the installation folder in Finder.");
    [self refresh:nil];
}
- (void)applyAppearance {
    self.window.appearance = self.state->appearance ? [NSAppearance appearanceNamed:self.state->appearance == 1 ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua] : nil;
    self.window.backgroundColor = NSColor.windowBackgroundColor;
}
- (void)appearanceChanged:(NSPopUpButton *)sender {
    int previous = self.state->appearance;
    self.state->appearance = (int) sender.indexOfSelectedItem;
    if (maage_status_failed(maage_launcher_ui_save(self.state))) {
        self.state->appearance = previous;
        [sender selectItemAtIndex:previous];
    }
    [self applyAppearance];
    [self refresh:nil];
}
- (void)refresh:(NSTimer *)timer {
    (void) timer;
    maage_launcher_ui_refresh(self.state);
    maage_launcher_ui *state = self.state;
    if (self.accountRevision != state->accounts.revision || !self.account.numberOfItems) {
        [self.account removeAllItems];
        for (unsigned i = 0; i < state->accounts.count; ++i) {
            maage_access_profile *p = &state->accounts.profiles[i];
            if (!p->connected) continue;
            [self.account addItemWithTitle:[NSString stringWithFormat:@"%s · %s", p->name, maage_access_state_name(p->state)]];
            self.account.lastItem.representedObject = @(p->uuid);
            if (!strcmp(state->account, p->uuid)) [self.account selectItem:self.account.lastItem];
        }
        if (!self.account.numberOfItems) [self.account addItemWithTitle:@"No connected Minecraft account"];
        NSMenuItem *chosen = self.account.selectedItem;
        [self.account.menu addItem:NSMenuItem.separatorItem];
        [self.account addItemWithTitle:@"Add Microsoft Account…"];
        self.account.lastItem.tag = 3;
        [self.account addItemWithTitle:@"Manage Microsoft Accounts…"];
        self.account.lastItem.tag = 1;
        [self.account addItemWithTitle:@"Refresh / Reverify Account"];
        self.account.lastItem.tag = 2;
        self.account.lastItem.enabled = !state->accounts.busy && state->account[0];
        [self.account addItemWithTitle:@"Sign Out"];
        self.account.lastItem.tag = 4;
        self.account.lastItem.enabled = !state->accounts.busy && state->account[0];
        [self.account addItemWithTitle:@"Remove Account"];
        self.account.lastItem.tag = 5;
        self.account.lastItem.enabled = !state->accounts.busy && state->account[0];
        [self.account selectItem:chosen];
        self.account.autoenablesItems = NO;
        self.accountRevision = state->accounts.revision;
    }
    if (self.clientRevision != state->client_revision) {
        self.clientRevision = state->client_revision;
        [self.clients reloadData];
        [self.installations reloadData];
    }
#if MAAGE_LAUNCHER_DEVELOPMENT
    if (state->client_confirmation) {
        state->client_confirmation = 0;
        maage_client_build *build = &state->client_candidate.build;
        NSAlert *confirmation = [[NSAlert alloc] init];
        confirmation.messageText = *state->replacing_client ? @"Replace Client?" : @"Import Local Client?";
        confirmation.informativeText = [NSString stringWithFormat:@"Client %s + %s\n%s · %s · %s\n\n%@\n\nImport copies the entire package. Only import code you trust. The original remains unchanged.%@", build->version, build->build, build->build_type, build->platform, build->architecture, @(state->client_candidate.source), *state->replacing_client ? @"\n\nAll installations using the selected Client will switch to this build, then the old managed package will be removed." : @""];
        [confirmation addButtonWithTitle:*state->replacing_client ? @"Replace" : @"Import"];
        [confirmation addButtonWithTitle:@"Cancel"];
        if ([confirmation runModal] == NSAlertFirstButtonReturn)
            maage_launcher_client_import(state);
        else
            maage_launcher_client_cancel_import(state);
    }
#endif
    self.clientStatus.stringValue = @(state->client_status);
    self.play.enabled = state->can_play;
    self.play.title = state->client_process ? @"Client Running" : @"▶  Play";
    self.account.enabled = !state->accounts.busy;
    self.accountStatus.stringValue = @(state->account_status);
    self.verify.enabled = !state->accounts.busy && state->account[0];
    self.status.title = @(state->readiness);
    self.status.toolTip = @"Open resource and account settings";
    self.resourceStatus.stringValue = state->progress.stage == MAAGE_SETUP_FAILED ? @(state->progress.error) : @(state->readiness);
    self.repair.enabled = !state->accounts.busy;
    self.repair.title = state->accounts.allowed ? @"Repair Assets…" : @"Verify Minecraft Access…";
    self.error.stringValue = @(state->error);
    for (NSButton *button in self.installationActions)
        button.enabled = button.tag == 0 || self.installations.selectedRow >= 0;
}
- (NSView *)playPage {
    NSView *page = [[NSView alloc] init];
    NSStackView *header = maage_ui_stack(YES, 12);
    header.translatesAutoresizingMaskIntoConstraints = NO;
    [page addSubview:header];
    [header addArrangedSubview:maage_ui_label(@"Play MaaGE", 24, YES)];
    NSView *space = [[NSView alloc] init];
    [header addArrangedSubview:space];
    self.account = [[NSPopUpButton alloc] init];
    self.account.target = self;
    self.account.action = @selector(selectAccount:);
    self.account.accessibilityLabel = @"Active Minecraft account";
    [self.account.widthAnchor constraintLessThanOrEqualToConstant:270].active = YES;
    [header addArrangedSubview:self.account];
    [NSLayoutConstraint activateConstraints:@[ [header.leadingAnchor constraintEqualToAnchor:page.leadingAnchor], [header.trailingAnchor constraintEqualToAnchor:page.trailingAnchor], [header.topAnchor constraintEqualToAnchor:page.topAnchor] ]];
    NSStackView *center = maage_ui_stack(NO, 14);
    center.alignment = NSLayoutAttributeCenterX;
    NSImageView *icon = [NSImageView imageViewWithImage:NSApp.applicationIconImage];
    [icon.widthAnchor constraintEqualToConstant:48].active = YES;
    [icon.heightAnchor constraintEqualToConstant:48].active = YES;
    [center addArrangedSubview:icon];
    [center addArrangedSubview:maage_ui_label(@"MaaGE", 32, YES)];
    NSTextField *subtitle = maage_ui_label(@"Minecraft as a Game Engine", 14, NO);
    subtitle.textColor = NSColor.secondaryLabelColor;
    [center addArrangedSubview:subtitle];
    self.installation = [[NSPopUpButton alloc] init];
    self.installation.target = self;
    self.installation.action = @selector(selectInstallation:);
    self.installation.accessibilityLabel = @"MaaGE installation";
    [self.installation.widthAnchor constraintEqualToConstant:260].active = YES;
    [center addArrangedSubview:self.installation];
    NSButton *play = [self button:@"▶  Play" action:@selector(playClient:)];
    self.play = play;
    play.bezelStyle = NSBezelStyleRounded;
    play.font = [NSFont systemFontOfSize:18 weight:NSFontWeightSemibold];
    play.bezelColor = [NSColor colorWithSRGBRed:.87 green:.19 blue:.62 alpha:1];
    play.enabled = NO;
    play.toolTip = @"Start the Client selected by this installation as an independent process.";
    [play.widthAnchor constraintEqualToConstant:260].active = YES;
    [play.heightAnchor constraintEqualToConstant:44].active = YES;
    [center addArrangedSubview:play];
    self.clientStatus = maage_ui_label(@"", 12, NO);
    self.clientStatus.textColor = NSColor.secondaryLabelColor;
    [self.clientStatus.widthAnchor constraintLessThanOrEqualToConstant:560].active = YES;
    [center addArrangedSubview:self.clientStatus];
    center.translatesAutoresizingMaskIntoConstraints = NO;
    [page addSubview:center];
    [NSLayoutConstraint activateConstraints:@[ [center.centerXAnchor constraintEqualToAnchor:page.centerXAnchor], [center.centerYAnchor constraintEqualToAnchor:page.centerYAnchor constant:15], [center.topAnchor constraintGreaterThanOrEqualToAnchor:header.bottomAnchor constant:24], [center.bottomAnchor constraintLessThanOrEqualToAnchor:page.bottomAnchor constant:-12] ]];
    return page;
}
- (NSView *)newsPage {
    NSStackView *page = maage_ui_stack(NO, 14);
    [page addArrangedSubview:maage_ui_label(@"News", 24, YES)];
    [page addArrangedSubview:maage_ui_label(@"No announcements yet", 17, YES)];
    [page addArrangedSubview:maage_ui_label(@"News will appear here when a source is configured. For now, follow development on GitHub.", 13, NO)];
    [page addArrangedSubview:[self button:@"Open MaaGE Project ↗" action:@selector(openProject:)]];
    [page addArrangedSubview:[[NSView alloc] init]];
    return page;
}
- (NSView *)installationsPage {
    NSStackView *page = maage_ui_stack(NO, 14);
    [page addArrangedSubview:maage_ui_label(@"Installations", 24, YES)];
    [page addArrangedSubview:maage_ui_label(@"Installed Clients", 15, YES)];
    self.clients = [[NSTableView alloc] init];
    self.clients.headerView = nil;
    NSTableColumn *clientColumn = [[NSTableColumn alloc] initWithIdentifier:@"client"];
    clientColumn.width = 640;
    [self.clients addTableColumn:clientColumn];
    self.clients.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    self.clients.delegate = self;
    self.clients.dataSource = self;
    self.clients.rowHeight = 45;
    self.clients.accessibilityLabel = @"Installed Client builds";
    NSScrollView *clientScroll = [[NSScrollView alloc] init];
    clientScroll.hasVerticalScroller = YES;
    clientScroll.borderType = NSBezelBorder;
    clientScroll.documentView = self.clients;
    [page addArrangedSubview:clientScroll];
    [clientScroll.widthAnchor constraintEqualToAnchor:page.widthAnchor].active = YES;
    [clientScroll.heightAnchor constraintEqualToConstant:110].active = YES;
    NSStackView *clientActions = maage_ui_stack(YES, 8);
#if MAAGE_LAUNCHER_DEVELOPMENT
    [clientActions addArrangedSubview:[self button:@"Import Local Client…" action:@selector(importClient:)]];
    NSButton *replace = [self button:@"Replace Client…" action:@selector(importClient:)];
    replace.tag = 1;
    [clientActions addArrangedSubview:replace];
#endif
    [clientActions addArrangedSubview:[self button:@"Remove Client…" action:@selector(removeClient:)]];
    [page addArrangedSubview:clientActions];
    [page addArrangedSubview:maage_ui_label(@"MaaGE Installations · shared Minecraft 1.7.10 resources", 15, YES)];
    self.installations = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 640, 300)];
    self.installations.headerView = nil;
    self.installations.autoresizingMask = NSViewWidthSizable;
    self.installations.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"installation"];
    column.width = 640;
    [self.installations addTableColumn:column];
    self.installations.delegate = self;
    self.installations.dataSource = self;
    self.installations.rowHeight = 55;
    self.installations.accessibilityLabel = @"MaaGE installations";
    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    scroll.documentView = self.installations;
    [page addArrangedSubview:scroll];
    [scroll.widthAnchor constraintEqualToAnchor:page.widthAnchor].active = YES;
    NSStackView *actions = maage_ui_stack(YES, 8);
    NSMutableArray *buttons = [NSMutableArray array];
    NSArray *titles = @[ @"New…", @"Edit…", @"Duplicate…", @"Remove…", @"Set as Default" ];
    for (NSInteger i = 0; i < 5; ++i) {
        SEL action = i < 3 ? @selector(editInstallation:) : i == 3 ? @selector(removeInstallation:)
                                                                   : @selector(defaultInstallation:);
        NSButton *button = [self button:titles[i] action:action];
        button.tag = i;
        [actions addArrangedSubview:button];
        [buttons addObject:button];
    }
    [page addArrangedSubview:actions];
    NSButton *openFolder = [self button:@"Open installation folder" action:@selector(openInstallationFolder:)];
    openFolder.tag = 5;
    [buttons addObject:openFolder];
    [page addArrangedSubview:openFolder];
    self.installationActions = buttons;
    return page;
}
#if MAAGE_LAUNCHER_DEVELOPMENT
- (void)importClient:(id)sender {
    BOOL replacing = [sender tag] == 1;
    NSInteger row = self.clients.selectedRow;
    if (self.state->client_job || (replacing && row < 0)) return;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = replacing ? @"Replace Client" : @"Import Local Client";
    panel.message = @"Choose maage-client with its adjacent maage-client.json manifest. Import copies its package and runtime dependencies without running it.";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.treatsFilePackagesAsDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] == NSModalResponseOK) {
        if (replacing)
            maage_launcher_client_replace(self.state, (unsigned) row, panel.URL.path.UTF8String);
        else
            maage_launcher_client_probe(self.state, panel.URL.path.UTF8String);
    }
}
#endif
- (void)removeClient:(id)sender {
    (void) sender;
    NSInteger row = self.clients.selectedRow;
    if (row < 0 || self.state->client_job) return;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Remove Managed Client?";
    alert.informativeText = @"Unused local Client files will be deleted. Developer originals and shared Minecraft resources are preserved. Builds referenced by installations cannot be removed.";
    [alert addButtonWithTitle:@"Remove"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] == NSAlertFirstButtonReturn) maage_launcher_client_remove(self.state, (unsigned) row);
}
- (NSView *)settingsPage {
    NSStackView *groups = maage_ui_stack(NO, 12);
    [groups addArrangedSubview:maage_ui_label(@"Settings", 24, YES)];
    [groups addArrangedSubview:maage_ui_label(@"Launcher", 15, YES)];
    [groups addArrangedSubview:maage_ui_label(@"Startup opens Play after account and resource preparation.", 13, NO)];
    self.accountStatus = maage_ui_label(@"", 12, NO);
    [groups addArrangedSubview:self.accountStatus];
    NSStackView *accounts = maage_ui_stack(YES, 8);
    [accounts addArrangedSubview:[self button:@"Manage Minecraft Accounts…" action:@selector(manage:)]];
    self.verify = [self button:@"Refresh / Reverify Account" action:@selector(verifyAccount:)];
    [accounts addArrangedSubview:self.verify];
    [groups addArrangedSubview:accounts];
    [groups addArrangedSubview:maage_ui_label(@"Resources", 15, YES)];
    NSTextField *path = maage_ui_label(@(self.state->resources), 12, NO);
    path.selectable = YES;
    [path.widthAnchor constraintLessThanOrEqualToConstant:560].active = YES;
    [groups addArrangedSubview:path];
    self.resourceStatus = maage_ui_label(@"", 13, NO);
    [groups addArrangedSubview:self.resourceStatus];
    self.repair = [self button:@"Repair Assets…" action:@selector(repairAssets:)];
    [groups addArrangedSubview:self.repair];
    [groups addArrangedSubview:maage_ui_label(@"Appearance", 15, YES)];
    NSPopUpButton *appearance = [[NSPopUpButton alloc] init];
    [appearance addItemsWithTitles:@[ @"System", @"Light", @"Dark" ]];
    [appearance selectItemAtIndex:self.state->appearance];
    appearance.target = self;
    appearance.action = @selector(appearanceChanged:);
    appearance.accessibilityLabel = @"Launcher appearance";
    [groups addArrangedSubview:appearance];
    [groups addArrangedSubview:maage_ui_label(@"About", 15, YES)];
    [groups addArrangedSubview:maage_ui_label(@"Version, project information, and license notices.", 12, NO)];
    [groups addArrangedSubview:[self button:@"About MaaGE…" action:@selector(showAbout:)]];
    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    NSView *document = [[NSView alloc] init];
    scroll.documentView = document;
    maage_ui_fill(groups, document, 2);
    document.translatesAutoresizingMaskIntoConstraints = NO;
    [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;
    return scroll;
}
- (NSView *)aboutPage {
    NSStackView *groups = maage_ui_stack(NO, 18);
    [groups addArrangedSubview:maage_ui_label(@"About MaaGE", 24, YES)];
    NSStackView *identity = maage_ui_stack(YES, 16);
    NSImageView *icon = [NSImageView imageViewWithImage:NSApp.applicationIconImage];
    [icon.widthAnchor constraintEqualToConstant:64].active = YES;
    [icon.heightAnchor constraintEqualToConstant:64].active = YES;
    [identity addArrangedSubview:icon];
    NSStackView *name = maage_ui_stack(NO, 5);
    [name addArrangedSubview:maage_ui_label(@"MaaGE", 28, YES)];
    NSTextField *subtitle = maage_ui_label(@"Minecraft as a Game Engine", 14, NO);
    subtitle.textColor = NSColor.secondaryLabelColor;
    [name addArrangedSubview:subtitle];
    [identity addArrangedSubview:name];
    [groups addArrangedSubview:identity];
    NSBox *separator = [[NSBox alloc] init];
    separator.boxType = NSBoxSeparator;
    [groups addArrangedSubview:separator];
    [separator.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    [groups addArrangedSubview:maage_ui_label(@"Launcher build", 16, YES)];
    NSTextField *version = maage_ui_label(@"Version " MAAGE_LAUNCHER_SEMANTIC_VERSION, 13, NO);
    NSTextField *revision = maage_ui_label(@"Git revision " MAAGE_LAUNCHER_GIT_HASH, 12, NO);
    version.selectable = YES;
    revision.selectable = YES;
    revision.textColor = NSColor.secondaryLabelColor;
    [groups addArrangedSubview:version];
    [groups addArrangedSubview:revision];
    NSBox *licenseSeparator = [[NSBox alloc] init];
    licenseSeparator.boxType = NSBoxSeparator;
    [groups addArrangedSubview:licenseSeparator];
    [licenseSeparator.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    [groups addArrangedSubview:maage_ui_label(@"Open source & attribution", 16, YES)];
    [groups addArrangedSubview:maage_ui_label(@"MaaGE's original source code is licensed under the Apache License, Version 2.0. Minecraft game resources have separate ownership and licensing.", 13, NO)];
    NSTextField *notice = maage_ui_label(@"MaaGE is an independent project and is not affiliated with or endorsed by Mojang Studios or Microsoft.", 12, NO);
    notice.textColor = NSColor.secondaryLabelColor;
    [groups addArrangedSubview:notice];
    NSStackView *actions = maage_ui_stack(YES, 10);
    [actions addArrangedSubview:[self button:@"View MaaGE on GitHub ↗" action:@selector(openProject:)]];
    [actions addArrangedSubview:[self button:@"Back to Settings" action:@selector(showSettings:)]];
    [groups addArrangedSubview:actions];
    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    NSView *document = [[NSView alloc] init];
    scroll.documentView = document;
    maage_ui_fill(groups, document, 2);
    document.translatesAutoresizingMaskIntoConstraints = NO;
    [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;
    return scroll;
}
- (void)show {
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1000, 670) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"MaaGE Launcher";
    self.window.minSize = NSMakeSize(940, 640);
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    [self applyAppearance];
    /* Replace the stark split-view divider with an integrated, fixed-width sidebar. */
    NSView *shell = [[NSView alloc] init];
    maage_ui_fill(shell, self.window.contentView, 0);
    NSView *sidebar = [[NSView alloc] init];
    NSView *right = [[NSView alloc] init];
    sidebar.translatesAutoresizingMaskIntoConstraints = NO;
    right.translatesAutoresizingMaskIntoConstraints = NO;
    [shell addSubview:sidebar];
    [shell addSubview:right];
    [NSLayoutConstraint activateConstraints:@[
        [sidebar.leadingAnchor constraintEqualToAnchor:shell.leadingAnchor],
        [sidebar.topAnchor constraintEqualToAnchor:shell.topAnchor],
        [sidebar.bottomAnchor constraintEqualToAnchor:shell.bottomAnchor],
        [sidebar.widthAnchor constraintEqualToConstant:194],
        [right.leadingAnchor constraintEqualToAnchor:sidebar.trailingAnchor],
        [right.trailingAnchor constraintEqualToAnchor:shell.trailingAnchor],
        [right.topAnchor constraintEqualToAnchor:shell.topAnchor],
        [right.bottomAnchor constraintEqualToAnchor:shell.bottomAnchor]
    ]];
    NSVisualEffectView *material = [[NSVisualEffectView alloc] init];
    material.material = NSVisualEffectMaterialSidebar;
    material.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    maage_ui_fill(material, sidebar, 0);
    self.sidebar = maage_ui_navigation_table();
    self.sidebar.dataSource = self;
    self.sidebar.delegate = self;
    self.sidebar.accessibilityLabel = @"Launcher navigation";

    NSView *mark = [[NSView alloc] init];
    mark.wantsLayer = YES;
    mark.layer.backgroundColor = [NSColor colorWithSRGBRed:0.86 green:0.19 blue:0.61 alpha:1].CGColor;
    mark.layer.cornerRadius = 9;
    [mark.widthAnchor constraintEqualToConstant:34].active = YES;
    [mark.heightAnchor constraintEqualToConstant:34].active = YES;
    NSTextField *monogram = maage_ui_row_label(@"M", 17, YES);
    monogram.textColor = NSColor.whiteColor;
    monogram.translatesAutoresizingMaskIntoConstraints = NO;
    [mark addSubview:monogram];
    [NSLayoutConstraint activateConstraints:@[
        [monogram.centerXAnchor constraintEqualToAnchor:mark.centerXAnchor],
        [monogram.centerYAnchor constraintEqualToAnchor:mark.centerYAnchor]
    ]];
    NSStackView *brandText = maage_ui_stack(NO, 2);
    [brandText addArrangedSubview:maage_ui_row_label(@"MaaGE", 16, YES)];
    NSTextField *descriptor = maage_ui_row_label(@"LAUNCHER", 10, YES);
    descriptor.textColor = NSColor.tertiaryLabelColor;
    [brandText addArrangedSubview:descriptor];
    NSStackView *brand = maage_ui_stack(YES, 10);
    [brand addArrangedSubview:mark];
    [brand addArrangedSubview:brandText];

    NSStackView *nav = maage_ui_stack(NO, 9);
    [nav addArrangedSubview:brand];
    [nav setCustomSpacing:26 afterView:brand];
    NSTextField *primaryTitle = maage_ui_row_label(@"MAIN", 10, YES);
    primaryTitle.textColor = NSColor.tertiaryLabelColor;
    [nav addArrangedSubview:primaryTitle];
    NSScrollView *navigation = [[NSScrollView alloc] init];
    navigation.drawsBackground = NO;
    navigation.documentView = self.sidebar;
    navigation.hasVerticalScroller = NO;
    navigation.hasHorizontalScroller = NO;
    [nav addArrangedSubview:navigation];
    [navigation.widthAnchor constraintEqualToAnchor:nav.widthAnchor].active = YES;
    self.settingsSidebar = maage_ui_navigation_table();
    self.settingsSidebar.dataSource = self;
    self.settingsSidebar.delegate = self;
    self.settingsSidebar.accessibilityLabel = @"Settings navigation";
    NSTextField *secondaryTitle = maage_ui_row_label(@"GENERAL", 10, YES);
    secondaryTitle.textColor = NSColor.tertiaryLabelColor;
    [nav addArrangedSubview:secondaryTitle];
    NSScrollView *settings = [[NSScrollView alloc] init];
    settings.drawsBackground = NO;
    settings.documentView = self.settingsSidebar;
    settings.hasVerticalScroller = NO;
    settings.hasHorizontalScroller = NO;
    [nav addArrangedSubview:settings];
    [settings.widthAnchor constraintEqualToAnchor:nav.widthAnchor].active = YES;
    [settings.heightAnchor constraintEqualToConstant:90].active = YES;
    maage_ui_fill(nav, sidebar, 18);
    NSStackView *content = maage_ui_stack(NO, 14);
    maage_ui_fill(content, right, 26);
    self.body = [[NSView alloc] init];
    [content addArrangedSubview:self.body];
    [self.body.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    self.pages = @[ [self playPage], [self newsPage], [self installationsPage], [self settingsPage], [self aboutPage] ];
    for (NSView *page in self.pages) {
        maage_ui_fill(page, self.body, 0);
        page.hidden = YES;
    }
    self.error = maage_ui_label(@"", 12, NO);
    self.error.textColor = NSColor.systemRedColor;
    [content addArrangedSubview:self.error];
    NSBox *separator = [[NSBox alloc] init];
    separator.boxType = NSBoxSeparator;
    [content addArrangedSubview:separator];
    [separator.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    NSStackView *footer = maage_ui_stack(YES, 12);
    NSTextField *version = maage_ui_label(@"Development build · " MAAGE_LAUNCHER_VERSION, 11, NO);
    version.textColor = NSColor.secondaryLabelColor;
    [footer addArrangedSubview:version];
    [footer addArrangedSubview:[[NSView alloc] init]];
    self.status = [self button:@"" action:@selector(showSettings:)];
    self.status.bordered = NO;
    self.status.font = [NSFont systemFontOfSize:11];
    [footer addArrangedSubview:self.status];
    [content addArrangedSubview:footer];
    [footer.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    [self.sidebar reloadData];
    [self.settingsSidebar reloadData];
    [self activatePage:self.state->page];
    [self reloadInstallations];
    [self refresh:nil];
    self.timer = [NSTimer scheduledTimerWithTimeInterval:.25 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
}
@end
maage_status maage_launcher_native_run(maage_launcher_ui *state, int *action) {
    if (!action) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    *action = 0;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        MaaGELauncherController *controller = [[MaaGELauncherController alloc] init];
        controller.state = state;
        id previous = NSApp.delegate;
        NSApp.delegate = controller;
        NSMenu *menu = [[NSMenu alloc] init];
        NSMenuItem *item = [[NSMenuItem alloc] init];
        [menu addItem:item];
        NSMenu *application = [[NSMenu alloc] initWithTitle:@"MaaGE"];
        NSMenuItem *about = [[NSMenuItem alloc] initWithTitle:@"About MaaGE" action:@selector(showAbout:) keyEquivalent:@""];
        about.target = controller;
        [application addItem:about];
        [application addItem:NSMenuItem.separatorItem];
        NSMenuItem *settings = [[NSMenuItem alloc] initWithTitle:@"Settings…" action:@selector(showSettings:) keyEquivalent:@","];
        settings.target = controller;
        [application addItem:settings];
        [application addItem:NSMenuItem.separatorItem];
        NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit MaaGE" action:@selector(quit:) keyEquivalent:@"q"];
        quit.target = controller;
        [application addItem:quit];
        item.submenu = application;
        NSMenuItem *editItem = [[NSMenuItem alloc] init];
        [menu addItem:editItem];
        NSMenu *edit = [[NSMenu alloc] initWithTitle:@"Edit"];
        editItem.submenu = edit;
        [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
        [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
        [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
        [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
        NSApp.mainMenu = menu;
        [controller show];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
        NSApp.delegate = previous;
        NSApp.mainMenu = nil;
        controller.window.delegate = nil;
        [controller.window close];
        *action = controller.result;
        return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    }
}
