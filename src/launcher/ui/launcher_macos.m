#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include "launcher.h"
#include "maage/launcher_version.h"
#include "maage/log.h"
#include "maage/time.h"
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
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 1, 2) xRadius:9 yRadius:9] fill];
}
@end
/* Installation rows retain a distinct accent-colored selection while the editor
 * has focus. Selection remains fully native for keyboard and accessibility. */
@interface MaaGELauncherInstallationRow : NSTableRowView
@end
@implementation MaaGELauncherInstallationRow
- (void)drawSelectionInRect:(NSRect)dirtyRect {
    (void) dirtyRect;
    NSColor *accent = NSColor.controlAccentColor;
    NSBezierPath *outline = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 3, 2) xRadius:7 yRadius:7];
    [[accent colorWithAlphaComponent:0.24] setFill];
    [outline fill];
    [[accent colorWithAlphaComponent:0.85] setStroke];
    outline.lineWidth = 1.5;
    [outline stroke];
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
@property(nonatomic, strong) NSTableView *sidebar, *settingsSidebar, *installations, *clients, *accounts;
@property(nonatomic, strong) NSView *body;
@property(nonatomic, strong) NSArray<NSView *> *pages;
@property(nonatomic, strong) NSPopUpButton *account, *installation;
@property(nonatomic, strong) NSButton *status, *repair, *verify, *play, *verifyResources, *retryResources, *cancelResources, *signIn, *deviceSignIn, *signOut, *removeAccountButton, *cancelAccount, *copyCode, *openDevice;
@property(nonatomic, strong) NSTextField *resourceStatus, *accountStatus, *error, *clientStatus, *launchStatus, *activeSummary, *authenticationStatus;
@property(nonatomic, strong) NSProgressIndicator *progress;
@property(nonatomic, strong) NSPopUpButton *appearance;
@property(nonatomic) int displayedAppearance;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic, strong) NSTextField *installationName, *installationArguments, *installationHeading, *installationHint;
@property(nonatomic, strong) NSPopUpButton *installationClient;
@property(nonatomic, strong) NSButton *createInstallationButton, *saveInstallationButton, *duplicateInstallationButton, *removeInstallationButton, *defaultInstallationButton, *activateInstallationButton, *cancelInstallationButton, *folderInstallationButton;
@property(nonatomic) unsigned editedInstallationID, accountRevision, clientRevision;
@property(nonatomic) BOOL hasEditedInstallation, creatingInstallation, updatingInstallationSelection;
@end
@implementation MaaGELauncherController
- (NSButton *)button:(NSString *)title action:(SEL)action {
    return [NSButton buttonWithTitle:title target:self action:action];
}
- (void)finish {
    [self.timer invalidate];
    self.timer = nil;
    [self.window orderOut:nil];
    [NSApp stop:nil];
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
}
- (void)quit:(id)sender {
    (void) sender;
    [self finish];
}
- (BOOL)windowShouldClose:(NSWindow *)window {
    (void) window;
    [self finish];
    return NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)application {
    (void) application;
    [self finish];
    return NSTerminateCancel;
}
- (void)manage:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_ACCOUNTS];
}
- (void)addAccount:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_ACCOUNTS];
    maage_access_sign_in(self.state->access, 0);
    [self refresh:nil];
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
    maage_launcher_resources(self.state, MAAGE_ASSETS_REPAIR);
    [self refresh:nil];
}
- (void)verifyResources:(id)sender {
    (void) sender;
    maage_launcher_resources(self.state, MAAGE_ASSETS_VERIFY);
    [self refresh:nil];
}
- (void)retryResources:(id)sender {
    (void) sender;
    maage_launcher_resources(self.state, self.state->resource_mode);
    [self refresh:nil];
}
- (void)cancelResources:(id)sender {
    (void) sender;
    maage_launcher_resources_cancel(self.state);
    [self refresh:nil];
}
- (void)deviceSignIn:(id)sender {
    (void) sender;
    maage_access_sign_in(self.state->access, 1);
    [self refresh:nil];
}
- (void)cancelAccount:(id)sender {
    (void) sender;
    maage_access_cancel_operation(self.state->access);
    [self refresh:nil];
}
- (void)copyCode:(id)sender {
    (void) sender;
    if (!*self.state->accounts.device_code) return;
    [NSPasteboard.generalPasteboard clearContents];
    [NSPasteboard.generalPasteboard setString:@(self.state->accounts.device_code) forType:NSPasteboardTypeString];
}
- (void)openDevice:(id)sender {
    (void) sender;
    if (*self.state->accounts.device_url) [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@(self.state->accounts.device_url)]];
}
- (void)showInstallations:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_INSTALLATIONS];
}
- (void)verifyAccount:(id)sender {
    (void) sender;
    if (!self.state->accounts.restored) maage_access_restore(self.state->access);
    else if (self.state->account[0]) maage_access_verify_local(self.state->access, self.state->account);
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
        if (self.settingsSidebar.selectedRow != row) [self.settingsSidebar selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    } else {
        [self.settingsSidebar deselectAll:nil];
        if (self.sidebar.selectedRow != (NSInteger) page) [self.sidebar selectRowIndexes:[NSIndexSet indexSetWithIndex:page] byExtendingSelection:NO];
    }
    for (NSUInteger i = 0; i < self.pages.count; ++i)
        self.pages[i].hidden = i != (NSUInteger) page;
}
- (void)showSettings:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_SETTINGS];
}
- (void)showReadiness:(id)sender {
    (void) sender;
    [self activatePage:maage_launcher_attention_page(self.state)];
}
- (void)showAbout:(id)sender {
    (void) sender;
    [self activatePage:MAAGE_PAGE_ABOUT];
}
- (void)selectAccount:(id)sender {
    (void) sender;
    NSString *uuid = self.account.selectedItem.representedObject;
    if (uuid) maage_launcher_account_select(self.state, uuid.UTF8String);
    else if (self.account.selectedItem.tag == 1) [self manage:nil];
    self.accountRevision = 0;
    [self refresh:nil];
}
- (void)selectInstallation:(id)sender {
    (void) sender;
    NSInteger row = self.installation.indexOfSelectedItem;
    if (row < 0 || (unsigned) row >= self.state->count) return;
    if (!maage_status_failed(maage_launcher_installation_select(self.state, (unsigned) row))) {
        self.creatingInstallation = NO;
        self.hasEditedInstallation = YES;
        self.editedInstallationID = self.state->installations[row].id;
        [self reloadInstallations];
    }
    [self refresh:nil];
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)table {
    return table == self.sidebar ? 3 : table == self.settingsSidebar ? 2
                                   : table == self.accounts          ? self.state->accounts.count
                                   : table == self.clients           ? self.state->clients->count
                                                                     : self.state->count;
}
- (NSTableRowView *)tableView:(NSTableView *)table rowViewForRow:(NSInteger)row {
    (void) row;
    if (table == self.sidebar || table == self.settingsSidebar) return [[MaaGELauncherNavigationRow alloc] init];
    return table == self.installations ? [[MaaGELauncherInstallationRow alloc] init] : nil;
}
- (NSView *)tableView:(NSTableView *)table viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    (void) column;
    if (table == self.sidebar || table == self.settingsSidebar) {
        if (table == self.settingsSidebar) row += MAAGE_PAGE_SETTINGS;
        NSArray *names = @[ @"Play", @"Installations", @"Accounts", @"Settings", @"About" ];
        NSArray *symbols = @[ @"play.fill", @"square.stack", @"person.crop.circle", @"gearshape", @"info.circle" ];
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
    if (table == self.accounts) {
        maage_access_profile *profile = &self.state->accounts.profiles[row];
        NSStackView *line = maage_ui_stack(NO, 4);
        [line addArrangedSubview:maage_ui_row_label(@(profile->name), 14, YES)];
        NSTextField *detail = maage_ui_row_label(@(maage_access_state_name(profile->state)), 12, NO);
        detail.textColor = NSColor.secondaryLabelColor;
        [line addArrangedSubview:detail];
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
    if ([column.identifier isEqualToString:@"role"]) {
        NSString *role = item->id == self.state->selected && item->id == self.state->default_id ? @"Active / Default"
                         : item->id == self.state->selected                                     ? @"Active"
                         : item->id == self.state->default_id                                   ? @"Default"
                                                                                                : @"—";
        NSTextField *label = maage_ui_row_label(role, 11, YES);
        label.textColor = NSColor.secondaryLabelColor;
        return maage_ui_row(label, 6);
    }
    NSStackView *line = maage_ui_stack(NO, 3);
    [line addArrangedSubview:maage_ui_row_label(@(item->name), 13, YES)];
    NSTextField *summary = maage_ui_row_label(@(maage_launcher_installation_client(self.state, (unsigned) row)), 11, NO);
    summary.textColor = NSColor.secondaryLabelColor;
    [line addArrangedSubview:summary];
    return maage_ui_row(line, 8);
}
- (void)tableViewSelectionDidChange:(NSNotification *)notification {
    if (notification.object == self.accounts) {
        NSInteger row = self.accounts.selectedRow;
        if (row >= 0 && (unsigned) row < self.state->accounts.count && strcmp(self.state->account, self.state->accounts.profiles[row].uuid)) {
            maage_launcher_account_select(self.state, self.state->accounts.profiles[row].uuid);
            [self refresh:nil];
        }
        return;
    }
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
    if (notification.object == self.installations) {
        if (self.updatingInstallationSelection) return;
        NSInteger row = self.installations.selectedRow;
        if (row >= 0 && (unsigned) row < self.state->count) {
            self.creatingInstallation = NO;
            self.hasEditedInstallation = YES;
            self.editedInstallationID = self.state->installations[row].id;
            [self bindInstallationEditor];
        } else if (!self.creatingInstallation) [self reloadInstallationRoles];
    }
    [self refresh:nil];
}
- (NSInteger)editedInstallationRow {
    if (!self.hasEditedInstallation) return -1;
    for (unsigned i = 0; i < self.state->count; ++i)
        if (self.state->installations[i].id == self.editedInstallationID) return (NSInteger) i;
    return -1;
}
- (NSInteger)activeInstallationRow {
    for (unsigned i = 0; i < self.state->count; ++i)
        if (self.state->installations[i].id == self.state->selected) return (NSInteger) i;
    return self.state->count ? 0 : -1;
}
- (void)installationClientChoices:(NSString *)selectedID {
    [self.installationClient removeAllItems];
    [self.installationClient addItemWithTitle:@"Client selection required"];
    self.installationClient.lastItem.representedObject = @"";
    [self.installationClient selectItemAtIndex:0];
    for (unsigned i = 0; i < self.state->clients->count; ++i) {
        maage_client_build *build = &self.state->clients->builds[i];
        char label[384];
        maage_client_build_label(build, label);
        [self.installationClient addItemWithTitle:@(label)];
        self.installationClient.lastItem.representedObject = @(build->id);
        if ([selectedID isEqualToString:@(build->id)]) [self.installationClient selectItem:self.installationClient.lastItem];
    }
    if (selectedID.length && maage_clients_find(self.state->clients, selectedID.UTF8String) < 0) {
        [self.installationClient addItemWithTitle:@"Referenced Client missing (keep reference)"];
        self.installationClient.lastItem.representedObject = selectedID;
        [self.installationClient selectItem:self.installationClient.lastItem];
    }
}
- (void)updateInstallationEditor {
    NSInteger row = [self editedInstallationRow];
    BOOL editing = !self.creatingInstallation && row >= 0;
    BOOL enabled = self.state->catalog_ready && !self.state->client_job;
    NSString *hint = @"Select a row on the left to edit its configuration.";
    if (self.creatingInstallation) {
        self.installationHeading.stringValue = @"Create installation";
        hint = @"Enter a name and optional Client build, then choose Create.";
    } else if (editing) {
        maage_installation *item = &self.state->installations[row];
        self.installationHeading.stringValue = @"Edit installation";
        if (item->id == self.state->selected && item->id == self.state->default_id) hint = @"Active for Play and Default. Save changes before selecting another row.";
        else if (item->id == self.state->selected) hint = @"Active for Play. Save changes before selecting another row.";
        else if (item->id == self.state->default_id) hint = @"Default installation. Not active for Play.";
        else hint = @"Selected for editing. Choose Make Active for Play to launch this one.";
    } else self.installationHeading.stringValue = @"Select an installation";
    if (![self.installationHint.stringValue isEqualToString:hint]) self.installationHint.stringValue = hint;
    self.createInstallationButton.enabled = enabled && !self.creatingInstallation;
    self.installationName.enabled = enabled && (editing || self.creatingInstallation);
    self.installationClient.enabled = enabled && (editing || self.creatingInstallation);
    self.installationArguments.enabled = enabled && (editing || self.creatingInstallation);
    self.saveInstallationButton.enabled = enabled && (editing || self.creatingInstallation);
    self.saveInstallationButton.title = self.creatingInstallation ? @"Create" : @"Save Changes";
    self.duplicateInstallationButton.enabled = enabled && editing;
    self.removeInstallationButton.enabled = enabled && editing && self.state->count > 1;
    self.defaultInstallationButton.enabled = enabled && editing && self.state->installations[row].id != self.state->default_id;
    self.activateInstallationButton.enabled = enabled && editing && self.state->installations[row].id != self.state->selected;
    self.folderInstallationButton.enabled = editing;
    self.cancelInstallationButton.hidden = !self.creatingInstallation;
    self.removeInstallationButton.hidden = self.creatingInstallation;
}
- (void)bindInstallationEditor {
    if (self.creatingInstallation) return;
    NSInteger row = [self editedInstallationRow];
    if (row >= 0) {
        maage_installation *item = &self.state->installations[row];
        self.installationName.stringValue = @(item->name);
        self.installationArguments.stringValue = @(item->arguments);
        [self installationClientChoices:@(item->client)];
    } else {
        self.installationName.stringValue = @"";
        self.installationArguments.stringValue = @"";
        [self installationClientChoices:@""];
    }
    [self updateInstallationEditor];
}
- (void)reloadInstallationRoles {
    NSInteger row = self.creatingInstallation ? -1 : [self editedInstallationRow];
    self.updatingInstallationSelection = YES;
    [self.installations reloadData];
    if (row >= 0) [self.installations selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    else [self.installations deselectAll:nil];
    self.updatingInstallationSelection = NO;
    NSInteger active = [self activeInstallationRow];
    if (active >= 0) [self.installation selectItemAtIndex:active];
    [self updateInstallationEditor];
}
- (void)reloadInstallations {
    [self.installation removeAllItems];
    for (unsigned i = 0; i < self.state->count; ++i)
        [self.installation addItemWithTitle:@(self.state->installations[i].name)];
    if (!self.creatingInstallation) {
        NSInteger row = [self editedInstallationRow];
        if (row < 0) row = [self activeInstallationRow];
        self.updatingInstallationSelection = YES;
        [self.installations reloadData];
        if (row >= 0) {
            self.hasEditedInstallation = YES;
            self.editedInstallationID = self.state->installations[row].id;
            [self.installations selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
            [self.installations scrollRowToVisible:row];
        } else [self.installations deselectAll:nil];
        self.updatingInstallationSelection = NO;
        [self bindInstallationEditor];
    } else [self.installations reloadData];
    NSInteger active = [self activeInstallationRow];
    if (active >= 0) [self.installation selectItemAtIndex:active];
    [self refresh:nil];
}
- (void)newInstallation:(id)sender {
    (void) sender;
    if (!self.state->catalog_ready || self.state->client_job) return;
    self.creatingInstallation = YES;
    self.updatingInstallationSelection = YES;
    [self.installations deselectAll:nil];
    self.updatingInstallationSelection = NO;
    self.installationName.stringValue = @"New Installation";
    self.installationArguments.stringValue = @"";
    [self installationClientChoices:@""];
    [self updateInstallationEditor];
    [self.window makeFirstResponder:self.installationName];
    [self.installationName selectText:nil];
}
- (void)cancelInstallation:(id)sender {
    (void) sender;
    self.creatingInstallation = NO;
    [self reloadInstallations];
}
- (void)saveInstallation:(id)sender {
    (void) sender;
    NSInteger row = [self editedInstallationRow];
    BOOL creating = self.creatingInstallation;
    if (self.state->client_job || (!creating && row < 0)) return;
    NSString *clientID = self.installationClient.selectedItem.representedObject ?: @"";
    maage_status result = maage_launcher_installation_write(self.state, creating ? -1 : (int) row, self.installationName.stringValue.UTF8String, clientID.UTF8String, self.installationArguments.stringValue.UTF8String);
    if (!maage_status_failed(result)) {
        self.creatingInstallation = NO;
        if (creating && self.state->count) {
            self.hasEditedInstallation = YES;
            self.editedInstallationID = self.state->installations[self.state->count - 1].id;
        }
        [self reloadInstallations];
    }
    [self refresh:nil];
}
- (void)duplicateInstallation:(id)sender {
    (void) sender;
    NSInteger row = [self editedInstallationRow];
    if (self.state->client_job || self.creatingInstallation || row < 0) return;
    maage_installation *item = &self.state->installations[row];
    NSString *copyName = [@(item->name) stringByAppendingString:@" Copy"];
    NSString *clientID = @(item->client), *arguments = @(item->arguments);
    maage_status result = maage_launcher_installation_write(self.state, -1, copyName.UTF8String, clientID.UTF8String, arguments.UTF8String);
    if (!maage_status_failed(result) && self.state->count) {
        self.hasEditedInstallation = YES;
        self.editedInstallationID = self.state->installations[self.state->count - 1].id;
        [self reloadInstallations];
    }
    [self refresh:nil];
}
- (void)activateInstallation:(id)sender {
    NSInteger row = [self editedInstallationRow];
    if (sender == self.installations) {
        row = self.installations.clickedRow;
        if (row < 0 || (unsigned) row >= self.state->count) return;
        if ([self editedInstallationRow] != row) {
            self.editedInstallationID = self.state->installations[row].id;
            self.hasEditedInstallation = YES;
            [self bindInstallationEditor];
        }
    }
    if (self.creatingInstallation || row < 0) return;
    if (!maage_status_failed(maage_launcher_installation_select(self.state, (unsigned) row))) [self reloadInstallationRoles];
    [self refresh:nil];
}
- (void)removeInstallation:(id)sender {
    (void) sender;
    NSInteger row = [self editedInstallationRow];
    if (self.creatingInstallation || row < 0) return;
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Remove Installation?";
    alert.informativeText = @"This instance will be removed from the Launcher and its gameplay folder archived. Worlds and private files remain recoverable; shared resources are preserved.";
    [alert addButtonWithTitle:@"Remove"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] == NSAlertFirstButtonReturn && !maage_status_failed(maage_launcher_installation_remove(self.state, (unsigned) row))) {
        self.hasEditedInstallation = NO;
        [self reloadInstallations];
    }
    [self refresh:nil];
}
- (void)defaultInstallation:(id)sender {
    (void) sender;
    NSInteger row = [self editedInstallationRow];
    if (self.creatingInstallation || row < 0) return;
    if (!maage_status_failed(maage_launcher_installation_default(self.state, (unsigned) row))) [self reloadInstallationRoles];
    [self refresh:nil];
}
- (void)openInstallationFolder:(id)sender {
    (void) sender;
    NSInteger row = [self editedInstallationRow];
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
    if (self.displayedAppearance != state->appearance) {
        self.displayedAppearance = state->appearance;
        [self.appearance selectItemAtIndex:state->appearance];
        [self applyAppearance];
    }
    self.appearance.enabled = state->catalog_ready;
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
        [self.account addItemWithTitle:@"Manage Accounts…"];
        self.account.lastItem.tag = 1;
        [self.account selectItem:chosen];
        self.account.autoenablesItems = NO;
        self.accountRevision = state->accounts.revision;
        [self.accounts reloadData];
        for (unsigned i = 0; i < state->accounts.count; ++i)
            if (!strcmp(state->account, state->accounts.profiles[i].uuid)) {
                [self.accounts selectRowIndexes:[NSIndexSet indexSetWithIndex:i] byExtendingSelection:NO];
                break;
            }
    }
    if (self.clientRevision != state->client_revision) {
        self.clientRevision = state->client_revision;
        [self.clients reloadData];
        if (self.installation.numberOfItems != state->count) [self reloadInstallations];
        else [self reloadInstallationRoles];
        NSString *clientID = self.installationClient.selectedItem.representedObject ?: @"";
        [self installationClientChoices:clientID];
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
        if ([confirmation runModal] == NSAlertFirstButtonReturn) maage_launcher_client_import(state);
        else
            maage_launcher_client_cancel_import(state);
    }
#endif
    self.clientStatus.stringValue = @(state->client_status);
    self.launchStatus.stringValue = @(state->launch_status);
    self.activeSummary.stringValue = @(state->active_summary);
    self.play.enabled = state->can_play;
    self.play.title = state->client_process ? @"Client Running" : state->launch_pending      ? @"Checking Resources…"
                                                              : state->client_operation == 4 ? @"Starting Client…"
                                                                                             : @"Play";
    self.account.enabled = state->accounts.metadata_ready;
    self.accountStatus.stringValue = @(state->account_status);
    self.authenticationStatus.stringValue = @(state->authentication_status);
    BOOL accountAvailable = state->services_ready && !state->accounts.busy;
    self.verify.enabled = accountAvailable && (state->account[0] || !state->accounts.restored);
    self.signIn.enabled = self.deviceSignIn.enabled = accountAvailable && state->accounts.restored;
    self.signOut.enabled = self.removeAccountButton.enabled = accountAvailable && state->accounts.restored && state->account[0];
    self.cancelAccount.enabled = state->accounts.busy;
    self.copyCode.enabled = self.openDevice.enabled = *state->accounts.device_code && state->accounts.device_deadline > maage_time_monotonic_seconds();
    self.status.title = @(state->readiness);
    self.resourceStatus.stringValue = @(state->resource_status);
    BOOL resourceAvailable = !state->progress.busy && !state->client_process && state->client_operation != 4 && !state->launch_pending;
    self.repair.enabled = self.verifyResources.enabled = self.retryResources.enabled = resourceAvailable;
    self.cancelResources.enabled = state->progress.busy || state->resources_pending;
    self.error.stringValue = @(state->error);
    self.progress.hidden = !state->progress.busy && !state->accounts.busy;
    self.progress.indeterminate = !state->progress.busy || state->progress.percent < 0;
    if (!self.progress.hidden && self.progress.indeterminate) [self.progress startAnimation:nil];
    else {
        [self.progress stopAnimation:nil];
        self.progress.doubleValue = state->progress.percent >= 0 ? state->progress.percent : 0;
    }
    [self updateInstallationEditor];
}
- (NSView *)playPage {
    NSView *page = [[NSView alloc] init];
    NSStackView *form = maage_ui_stack(NO, 16);
    [form addArrangedSubview:maage_ui_label(@"Play", 30, YES)];
    NSTextField *intro = maage_ui_label(@"Choose your installation and jump into MaaGE.", 14, NO);
    intro.textColor = NSColor.secondaryLabelColor;
    [form addArrangedSubview:intro];
    [form setCustomSpacing:30 afterView:intro];
    [form addArrangedSubview:maage_ui_label(@"Installation", 12, YES)];
    self.installation = [[NSPopUpButton alloc] init];
    self.installation.target = self;
    self.installation.action = @selector(selectInstallation:);
    self.installation.accessibilityLabel = @"Installation for Play";
    [form addArrangedSubview:self.installation];
    self.clientStatus = maage_ui_label(@"Loading Client builds…", 12, NO);
    self.clientStatus.textColor = NSColor.secondaryLabelColor;
    [form addArrangedSubview:self.clientStatus];
    self.launchStatus = maage_ui_label(@"Starting Launcher services…", 13, NO);
    [form addArrangedSubview:self.launchStatus];
    self.play = [self button:@"Play" action:@selector(playClient:)];
    self.play.bezelStyle = NSBezelStyleRounded;
    self.play.font = [NSFont systemFontOfSize:20 weight:NSFontWeightSemibold];
    self.play.bezelColor = [NSColor colorWithSRGBRed:.87 green:.19 blue:.62 alpha:1];
    self.play.enabled = NO;
    self.play.keyEquivalent = @"\r";
    [self.play.widthAnchor constraintEqualToConstant:260].active = YES;
    [self.play.heightAnchor constraintEqualToConstant:48].active = YES;
    [form addArrangedSubview:self.play];
    NSStackView *shortcuts = maage_ui_stack(YES, 8);
    [shortcuts addArrangedSubview:[self button:@"Installations" action:@selector(showInstallations:)]];
    [shortcuts addArrangedSubview:[self button:@"Accounts" action:@selector(manage:)]];
    [shortcuts addArrangedSubview:[self button:@"Resources" action:@selector(showSettings:)]];
    [form addArrangedSubview:shortcuts];
    form.translatesAutoresizingMaskIntoConstraints = NO;
    [page addSubview:form];
    [NSLayoutConstraint activateConstraints:@[
        [form.leadingAnchor constraintEqualToAnchor:page.leadingAnchor
                                           constant:12],
        [form.trailingAnchor constraintEqualToAnchor:page.trailingAnchor
                                            constant:-12],
        [form.centerYAnchor constraintEqualToAnchor:page.centerYAnchor],
        [form.topAnchor constraintGreaterThanOrEqualToAnchor:page.topAnchor
                                                    constant:12],
        [form.bottomAnchor constraintLessThanOrEqualToAnchor:page.bottomAnchor
                                                    constant:-12],
        [self.installation.widthAnchor constraintEqualToAnchor:form.widthAnchor],
        [self.clientStatus.widthAnchor constraintEqualToAnchor:form.widthAnchor],
        [self.launchStatus.widthAnchor constraintEqualToAnchor:form.widthAnchor]
    ]];
    return page;
}
- (NSView *)accountsPage {
    NSStackView *groups = maage_ui_stack(NO, 14);
    [groups addArrangedSubview:maage_ui_label(@"Accounts", 24, YES)];
    [groups addArrangedSubview:maage_ui_label(@"Select a Minecraft profile. Only verified Java Edition access enables Play.", 13, NO)];
    self.accounts = [[NSTableView alloc] init];
    self.accounts.headerView = nil;
    self.accounts.rowHeight = 54;
    self.accounts.delegate = self;
    self.accounts.dataSource = self;
    self.accounts.accessibilityLabel = @"Minecraft accounts";
    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"profile"];
    column.width = 560;
    [self.accounts addTableColumn:column];
    self.accounts.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    NSScrollView *list = [[NSScrollView alloc] init];
    list.hasVerticalScroller = YES;
    list.documentView = self.accounts;
    [list.heightAnchor constraintEqualToConstant:130].active = YES;
    [list.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    [groups addArrangedSubview:list];
    self.accountStatus = maage_ui_label(@"", 13, NO);
    [self.accountStatus.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    [groups addArrangedSubview:self.accountStatus];
    NSStackView *actions = maage_ui_stack(YES, 8);
    self.signIn = [self button:@"Sign In with Microsoft" action:@selector(addAccount:)];
    self.deviceSignIn = [self button:@"Use Device Code" action:@selector(deviceSignIn:)];
    self.verify = [self button:@"Refresh / Retry" action:@selector(verifyAccount:)];
    [actions addArrangedSubview:self.signIn];
    [actions addArrangedSubview:self.deviceSignIn];
    [actions addArrangedSubview:self.verify];
    [groups addArrangedSubview:actions];
    actions = maage_ui_stack(YES, 8);
    self.signOut = [self button:@"Sign Out" action:@selector(signOutAccount:)];
    self.removeAccountButton = [self button:@"Remove Account" action:@selector(removeAccount:)];
    self.cancelAccount = [self button:@"Cancel Sign In" action:@selector(cancelAccount:)];
    [actions addArrangedSubview:self.signOut];
    [actions addArrangedSubview:self.removeAccountButton];
    [actions addArrangedSubview:self.cancelAccount];
    [groups addArrangedSubview:actions];
    self.authenticationStatus = maage_ui_label(@"", 13, NO);
    self.authenticationStatus.selectable = YES;
    [self.authenticationStatus.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    [groups addArrangedSubview:self.authenticationStatus];
    actions = maage_ui_stack(YES, 8);
    self.copyCode = [self button:@"Copy Code" action:@selector(copyCode:)];
    self.openDevice = [self button:@"Open Microsoft Sign In" action:@selector(openDevice:)];
    [actions addArrangedSubview:self.copyCode];
    [actions addArrangedSubview:self.openDevice];
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
    [clientScroll.heightAnchor constraintEqualToConstant:82].active = YES;
    NSStackView *clientActions = maage_ui_stack(YES, 8);
#if MAAGE_LAUNCHER_DEVELOPMENT
    [clientActions addArrangedSubview:[self button:@"Import Local Client…" action:@selector(importClient:)]];
    NSButton *replace = [self button:@"Replace Client…" action:@selector(importClient:)];
    replace.tag = 1;
    [clientActions addArrangedSubview:replace];
#endif
    [clientActions addArrangedSubview:[self button:@"Remove Client…" action:@selector(removeClient:)]];
    [page addArrangedSubview:clientActions];

    NSStackView *workspace = maage_ui_stack(YES, 18);
    workspace.alignment = NSLayoutAttributeTop;
    NSStackView *left = maage_ui_stack(NO, 10);
    NSStackView *listHeading = maage_ui_stack(YES, 10);
    [listHeading addArrangedSubview:maage_ui_label(@"Installations", 17, YES)];
    [listHeading addArrangedSubview:[[NSView alloc] init]];
    self.createInstallationButton = [self button:@"+ New" action:@selector(newInstallation:)];
    [listHeading addArrangedSubview:self.createInstallationButton];
    [left addArrangedSubview:listHeading];
    [listHeading.widthAnchor constraintEqualToAnchor:left.widthAnchor].active = YES;
    NSTextField *selectionHint = maage_ui_label(@"Select a row to edit · Double-click to activate", 11, NO);
    selectionHint.textColor = NSColor.secondaryLabelColor;
    [left addArrangedSubview:selectionHint];
    [selectionHint.widthAnchor constraintEqualToAnchor:left.widthAnchor].active = YES;
    self.installations = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 350, 300)];
    self.installations.autoresizingMask = NSViewWidthSizable;
    self.installations.columnAutoresizingStyle = NSTableViewFirstColumnOnlyAutoresizingStyle;
    self.installations.allowsEmptySelection = YES;
    self.installations.usesAlternatingRowBackgroundColors = YES;
    self.installations.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;
    self.installations.rowHeight = 46;
    self.installations.intercellSpacing = NSMakeSize(0, 3);
    self.installations.delegate = self;
    self.installations.dataSource = self;
    self.installations.target = self;
    self.installations.doubleAction = @selector(activateInstallation:);
    self.installations.accessibilityLabel = @"Installations — select a row to edit";
    NSTableColumn *nameColumn = [[NSTableColumn alloc] initWithIdentifier:@"installation"];
    nameColumn.title = @"Installation";
    nameColumn.width = 210;
    [self.installations addTableColumn:nameColumn];
    NSTableColumn *roleColumn = [[NSTableColumn alloc] initWithIdentifier:@"role"];
    roleColumn.title = @"Role";
    roleColumn.width = 124;
    [self.installations addTableColumn:roleColumn];
    NSScrollView *listScroll = [[NSScrollView alloc] init];
    listScroll.hasVerticalScroller = YES;
    listScroll.borderType = NSBezelBorder;
    listScroll.documentView = self.installations;
    [left addArrangedSubview:listScroll];
    [listScroll.widthAnchor constraintEqualToAnchor:left.widthAnchor].active = YES;
    [listScroll.heightAnchor constraintEqualToConstant:364].active = YES;
    [workspace addArrangedSubview:left];

    NSStackView *editor = maage_ui_stack(NO, 9);
    self.installationHeading = maage_ui_label(@"Select an installation", 17, YES);
    [editor addArrangedSubview:self.installationHeading];
    self.installationHint = maage_ui_label(@"Select a row on the left to edit its configuration.", 12, NO);
    self.installationHint.textColor = NSColor.secondaryLabelColor;
    [editor addArrangedSubview:self.installationHint];
    [self.installationHint.widthAnchor constraintEqualToAnchor:editor.widthAnchor].active = YES;
    [editor setCustomSpacing:17 afterView:self.installationHint];
    [editor addArrangedSubview:maage_ui_label(@"Installation name", 12, YES)];
    self.installationName = [[NSTextField alloc] init];
    self.installationName.accessibilityLabel = @"Installation name";
    [editor addArrangedSubview:self.installationName];
    [editor addArrangedSubview:maage_ui_label(@"Client build", 12, YES)];
    self.installationClient = [[NSPopUpButton alloc] init];
    self.installationClient.accessibilityLabel = @"Client build";
    [editor addArrangedSubview:self.installationClient];
    [editor addArrangedSubview:maage_ui_label(@"Client CLI arguments", 12, YES)];
    self.installationArguments = [[NSTextField alloc] init];
    self.installationArguments.accessibilityLabel = @"Client CLI arguments";
    [editor addArrangedSubview:self.installationArguments];
    NSTextField *argumentsHint = maage_ui_label(@"Quote values containing spaces.", 11, NO);
    argumentsHint.textColor = NSColor.secondaryLabelColor;
    [editor addArrangedSubview:argumentsHint];

    NSStackView *primaryActions = maage_ui_stack(YES, 7);
    self.saveInstallationButton = [self button:@"Save Changes" action:@selector(saveInstallation:)];
    self.duplicateInstallationButton = [self button:@"Duplicate" action:@selector(duplicateInstallation:)];
    self.removeInstallationButton = [self button:@"Remove" action:@selector(removeInstallation:)];
    self.cancelInstallationButton = [self button:@"Cancel" action:@selector(cancelInstallation:)];
    [primaryActions addArrangedSubview:self.saveInstallationButton];
    [primaryActions addArrangedSubview:self.duplicateInstallationButton];
    [primaryActions addArrangedSubview:self.removeInstallationButton];
    [primaryActions addArrangedSubview:self.cancelInstallationButton];
    [editor addArrangedSubview:primaryActions];

    NSStackView *roleActions = maage_ui_stack(YES, 7);
    self.defaultInstallationButton = [self button:@"Set as Default" action:@selector(defaultInstallation:)];
    self.activateInstallationButton = [self button:@"Make Active for Play" action:@selector(activateInstallation:)];
    [roleActions addArrangedSubview:self.defaultInstallationButton];
    [roleActions addArrangedSubview:self.activateInstallationButton];
    [editor addArrangedSubview:roleActions];
    self.folderInstallationButton = [self button:@"Open installation folder" action:@selector(openInstallationFolder:)];
    [editor addArrangedSubview:self.folderInstallationButton];
    [workspace addArrangedSubview:editor];
    [left.widthAnchor constraintEqualToAnchor:workspace.widthAnchor multiplier:0.44].active = YES;
    [editor.widthAnchor constraintEqualToAnchor:workspace.widthAnchor multiplier:0.56 constant:-18].active = YES;
    [self.installationName.widthAnchor constraintEqualToAnchor:editor.widthAnchor].active = YES;
    [self.installationClient.widthAnchor constraintEqualToAnchor:editor.widthAnchor].active = YES;
    [self.installationArguments.widthAnchor constraintEqualToAnchor:editor.widthAnchor].active = YES;
    [page addArrangedSubview:workspace];
    [workspace.widthAnchor constraintEqualToAnchor:page.widthAnchor].active = YES;

    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    NSView *document = [[NSView alloc] init];
    scroll.documentView = document;
    maage_ui_fill(page, document, 2);
    document.translatesAutoresizingMaskIntoConstraints = NO;
    [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active = YES;
    return scroll;
}
#if MAAGE_LAUNCHER_DEVELOPMENT
- (void)importClient:(id)sender {
    BOOL replacing = [sender tag] == 1;
    NSInteger row = self.clients.selectedRow;
    if (self.state->client_job || (replacing && row < 0)) return;
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = replacing ? @"Replace Client" : @"Import Local Client";
    panel.message = @"Choose maage-client with its adjacent maage-client.json manifest. Import copies only those two files without running the Client.";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.treatsFilePackagesAsDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] == NSModalResponseOK) {
        if (replacing) maage_launcher_client_replace(self.state, (unsigned) row, panel.URL.path.UTF8String);
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
    [groups addArrangedSubview:maage_ui_label(@"Accounts and resources prepare in the background while you use the Launcher.", 13, NO)];
    [groups addArrangedSubview:maage_ui_label(@"Resources", 15, YES)];
    NSTextField *path = maage_ui_label(@(self.state->resources), 12, NO);
    path.selectable = YES;
    [path.widthAnchor constraintLessThanOrEqualToConstant:560].active = YES;
    [groups addArrangedSubview:path];
    self.resourceStatus = maage_ui_label(@"", 13, NO);
    [groups addArrangedSubview:self.resourceStatus];
    [self.resourceStatus.widthAnchor constraintEqualToAnchor:groups.widthAnchor].active = YES;
    NSStackView *maintenance = maage_ui_stack(YES, 8);
    self.verifyResources = [self button:@"Verify Resources" action:@selector(verifyResources:)];
    self.repair = [self button:@"Repair Resources" action:@selector(repairAssets:)];
    [maintenance addArrangedSubview:self.verifyResources];
    [maintenance addArrangedSubview:self.repair];
    [groups addArrangedSubview:maintenance];
    maintenance = maage_ui_stack(YES, 8);
    self.retryResources = [self button:@"Retry" action:@selector(retryResources:)];
    self.cancelResources = [self button:@"Cancel" action:@selector(cancelResources:)];
    [maintenance addArrangedSubview:self.retryResources];
    [maintenance addArrangedSubview:self.cancelResources];
    [groups addArrangedSubview:maintenance];
    [groups addArrangedSubview:maage_ui_label(@"Verify checks file contents. Repair also restores missing or damaged resources.", 12, NO)];
    [groups addArrangedSubview:maage_ui_label(@"Appearance", 15, YES)];
    NSPopUpButton *appearance = [[NSPopUpButton alloc] init];
    self.appearance = appearance;
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
    self.window.minSize = NSMakeSize(860, 680);
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
    NSStackView *context = maage_ui_stack(YES, 14);
    self.activeSummary = maage_ui_row_label(@"Starting Launcher…", 12, YES);
    [context addArrangedSubview:self.activeSummary];
    [self.activeSummary setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    [context addArrangedSubview:[[NSView alloc] init]];
    self.account = [[NSPopUpButton alloc] init];
    self.account.target = self;
    self.account.action = @selector(selectAccount:);
    self.account.accessibilityLabel = @"Active Minecraft account";
    [self.account.widthAnchor constraintLessThanOrEqualToConstant:270].active = YES;
    [context addArrangedSubview:self.account];
    [content addArrangedSubview:context];
    [context.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    self.body = [[NSView alloc] init];
    [content addArrangedSubview:self.body];
    [self.body.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
    self.pages = @[ [self playPage], [self installationsPage], [self accountsPage], [self settingsPage], [self aboutPage] ];
    for (NSView *page in self.pages) {
        maage_ui_fill(page, self.body, 0);
        page.hidden = YES;
    }
    self.progress = [[NSProgressIndicator alloc] init];
    self.progress.style = NSProgressIndicatorStyleBar;
    self.progress.minValue = 0;
    self.progress.maxValue = 100;
    self.progress.accessibilityLabel = @"Background preparation progress";
    [content addArrangedSubview:self.progress];
    [self.progress.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
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
    self.status = [self button:@"" action:@selector(showReadiness:)];
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
    [self.window displayIfNeeded];
    maage_launcher_ui_presented(self.state);
}
@end
maage_status maage_launcher_native_run(maage_launcher_ui *state) {
    if (!state) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
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
        double windowStarted = maage_time_monotonic_seconds();
        [controller show];
        MAAGE_LOG_DEBUG_EVENT("maage.launcher.startup", maage_log_debug("maage.launcher.startup", "AppKit main window creation/presentation: %.3f s.", maage_time_monotonic_seconds() - windowStarted));
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
        NSApp.delegate = previous;
        NSApp.mainMenu = nil;
        controller.window.delegate = nil;
        [controller.window close];
        return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    }
}
