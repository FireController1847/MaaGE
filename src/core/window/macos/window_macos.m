#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include "../window_internal.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>

typedef struct maage_cocoa_system {
    NSAutoreleasePool *pool;
    id delegate;
    id previous_delegate;
    NSApplicationPresentationOptions presentation;
} maage_cocoa_system;
typedef struct maage_cocoa_window {
    NSWindow *handle;
    NSView *viewport; /* Stable renderer-owned drawable view; parent content view stays full-sized. */
    id delegate;
    NSRect normal_frame;
    maage_window_mode desired_mode;
    maage_window_mode entered_from_mode;
    int transitioning;
    NSView *bitmap_view;
    double capture_time;
} maage_cocoa_window;

static maage_window_system *maage_cocoa_active;
static maage_status maage_cocoa_mode(maage_window *, maage_window_mode);
static maage_status maage_cocoa_state(maage_window *);
static void maage_cocoa_layout_viewport(maage_window *);
static maage_status maage_cocoa_restore_non_native(maage_window *, maage_window_mode);
static void maage_cocoa_transition_failure(maage_window *, const char *);
static maage_status maage_cocoa_capture(maage_window *, int);
static void maage_cocoa_presentation(maage_window_system *);
@interface MaaGEPlatformWindow : NSWindow
@end
@implementation MaaGEPlatformWindow
- (BOOL)canBecomeKeyWindow { return YES; }
@end

@interface MaaGEViewport : NSView {
@public
    maage_window_cursor cursor_shape;
}
@end
@implementation MaaGEViewport
- (void)resetCursorRects
{
    [super resetCursorRects];
    [self addCursorRect:[self bounds] cursor:cursor_shape == MAAGE_WINDOW_CURSOR_POINTER ?
        [NSCursor pointingHandCursor] : [NSCursor arrowCursor]];
}
@end

@interface MaaGEBitmapView : NSView {
@public
    NSBitmapImageRep *image;
}
@end
@implementation MaaGEBitmapView
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    [image drawInRect:[self bounds]];
}
- (void)dealloc { [image release]; [super dealloc]; }
@end

@interface MaaGEApplicationDelegate : NSObject <NSApplicationDelegate>
@end
@implementation MaaGEApplicationDelegate
- (void)applicationDidResignActive:(NSNotification *)notification
{
    maage_window *window;
    (void)notification;
    if (!maage_cocoa_active) return;
    for (window = maage_cocoa_active->windows; window; window = window->next) {
        maage_window_input_focus(window, 0);
    }
}
- (void)applicationDidBecomeActive:(NSNotification *)notification
{
    (void)notification;
    if (!maage_cocoa_active) return;
    maage_cocoa_presentation(maage_cocoa_active);
}
- (void)applicationDidChangeScreenParameters:(NSNotification *)notification
{
    maage_window *window;
    (void)notification;
    if (!maage_cocoa_active) return;
    for (window = maage_cocoa_active->windows; window; window = window->next) {
        maage_cocoa_layout_viewport(window);
        window->state.resized = 1;
    }
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)application
{
    maage_window *window;
    (void)application;
    for (window = maage_cocoa_active->windows; window; window = window->next) {
        maage_window_input_focus(window, 0);
        window->state.close_requested = 1;
    }
    return NSTerminateCancel;
}
@end

@interface MaaGEWindowDelegate : NSObject <NSWindowDelegate> {
@public
    maage_window *owner;
}
@end
@implementation MaaGEWindowDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;
    owner->state.close_requested = 1;
    return NO;
}
- (void)windowDidResize:(NSNotification *)notification
{
    (void)notification;
    maage_cocoa_layout_viewport(owner);
    owner->state.resized = 1;
}
- (void)windowDidBecomeKey:(NSNotification *)notification
{
    (void)notification;
    maage_window_input_focus(owner, 1);
}
- (void)windowDidResignKey:(NSNotification *)notification
{
    (void)notification;
    maage_window_input_focus(owner, 0);
}
- (void)windowDidMiniaturize:(NSNotification *)notification
{
    (void)notification;
    maage_window_input_focus(owner, 0);
}
- (void)windowDidChangeBackingProperties:(NSNotification *)notification
{
    (void)notification;
    maage_cocoa_layout_viewport(owner);
    owner->state.resized = 1;
}
- (void)windowDidChangeScreen:(NSNotification *)notification
{
    (void)notification;
    maage_cocoa_layout_viewport(owner);
    owner->state.resized = 1;
}
- (void)windowWillEnterFullScreen:(NSNotification *)notification
{
    maage_cocoa_window *native = owner->platform;
    (void)notification;
    /* User/OS initiated transitions also arrive here without set_mode(). */
    if (!native->transitioning) {
        native->entered_from_mode = owner->state.mode;
        if (owner->state.mode == MAAGE_WINDOW_WINDOWED)
            native->normal_frame = [native->handle frame];
        native->desired_mode = MAAGE_WINDOW_NATIVE_FULLSCREEN;
        native->transitioning = 1;
    }
    maage_cocoa_layout_viewport(owner);
}
- (void)windowDidEnterFullScreen:(NSNotification *)notification
{
    maage_cocoa_window *native = owner->platform;
    (void)notification;
    native->transitioning = 0;
    native->desired_mode = MAAGE_WINDOW_NATIVE_FULLSCREEN;
    owner->state.mode = MAAGE_WINDOW_NATIVE_FULLSCREEN;
    owner->state.mode_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
    maage_cocoa_layout_viewport(owner);
    owner->state.resized = 1;
}
- (void)windowWillExitFullScreen:(NSNotification *)notification
{
    maage_cocoa_window *native = owner->platform;
    (void)notification;
    if (!native->transitioning) {
        native->desired_mode = MAAGE_WINDOW_WINDOWED;
        native->transitioning = 1;
    }
}
- (void)windowDidExitFullScreen:(NSNotification *)notification
{
    maage_cocoa_window *native = owner->platform;
    maage_window_mode target = native->desired_mode;
    maage_status result;
    (void)notification;
    native->transitioning = 0;
    /* Re-establish ordinary geometry without recording the intermediate
     * AppKit restore frame as the user's saved windowed frame. */
    owner->state.mode = MAAGE_WINDOW_WINDOWED;
    result = maage_cocoa_restore_non_native(owner, target);
    if (maage_status_failed(result)) {
        owner->state.mode_result = result;
        maage_log_status(MAAGE_LOG_WARNING, "maage.window.cocoa", result,
            "Native fullscreen Space exited, but the requested follow-up window mode failed.");
    }
    maage_cocoa_layout_viewport(owner);
    maage_cocoa_presentation(owner->system);
    owner->state.resized = 1;
}
- (void)windowDidFailToEnterFullScreen:(NSWindow *)window
{
    maage_cocoa_window *native = owner->platform;
    maage_window_mode previous = native->entered_from_mode;
    maage_status result;
    (void)window;
    native->transitioning = 0;
    /* AppKit has not entered the Space; restore the original presentation,
     * including Borderless if this request came from the desktop Space. */
    owner->state.mode = MAAGE_WINDOW_WINDOWED;
    result = maage_cocoa_restore_non_native(owner, previous);
    if (maage_status_failed(result))
        maage_log_status(MAAGE_LOG_WARNING, "maage.window.cocoa", result,
            "Unable to restore the previous mode after fullscreen entry failed.");
    maage_cocoa_transition_failure(owner, "AppKit failed to enter a native fullscreen Space.");
    maage_cocoa_layout_viewport(owner);
    maage_cocoa_presentation(owner->system);
    owner->state.resized = 1;
}
- (void)windowDidFailToExitFullScreen:(NSWindow *)window
{
    maage_cocoa_window *native = owner->platform;
    (void)window;
    native->transitioning = 0;
    native->desired_mode = MAAGE_WINDOW_NATIVE_FULLSCREEN;
    owner->state.mode = MAAGE_WINDOW_NATIVE_FULLSCREEN;
    maage_cocoa_transition_failure(owner, "AppKit failed to exit the native fullscreen Space.");
    maage_cocoa_layout_viewport(owner);
    maage_cocoa_presentation(owner->system);
    owner->state.resized = 1;
}
/* Do not replace AppKit's proposed fullscreen size or presentation policy.
 * The drawable subview is constrained to the actual content-layout/screen safe
 * intersection; replacing the fullscreen size with the physical display frame
 * formerly caused the notch to overlap rendering. */
@end

static NSUInteger maage_cocoa_style(maage_window *window, int decorated)
{
    NSUInteger style = decorated ? (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable) : NSWindowStyleMaskBorderless;
    /* A normal titled window must reserve its native title bar. Only the
     * borderless surface may use the full display/notch region. */
    if (window->full_display_content && !decorated)
        style |= NSWindowStyleMaskFullSizeContentView;
    return style;
}

/* A persistent child view is returned to Metal; moving or resizing it never
 * replaces the renderer's NSView/CAMetalLayer handle during a Space transition.
 * Both rectangles below use global AppKit screen points before intersection,
 * so an inset already taken by AppKit cannot be applied a second time. */
static void maage_cocoa_layout_viewport(maage_window *window)
{
    maage_cocoa_window *native = window->platform;
    NSWindow *handle;
    NSView *root;
    NSScreen *screen;
    NSRect target, visible, layout, safe, window_rect;
    if (!native || !native->handle || !native->viewport) return;
    handle = native->handle;
    root = [handle contentView];
    screen = [handle screen];
    target = [root bounds];
    if (screen && (([handle styleMask] & NSWindowStyleMaskFullScreen) ||
        (native->transitioning && native->desired_mode == MAAGE_WINDOW_NATIVE_FULLSCREEN))) {
        visible = [handle convertRectToScreen:[root convertRect:target toView:nil]];
        safe = [screen frame];
        if (@available(macOS 12.0, *)) {
            NSEdgeInsets inset = [screen safeAreaInsets];
            safe.origin.x += inset.left;
            safe.origin.y += inset.bottom;
            safe.size.width -= inset.left + inset.right;
            safe.size.height -= inset.top + inset.bottom;
        }
        visible = NSIntersectionRect(visible, safe);
        if ([handle styleMask] & NSWindowStyleMaskFullScreen) {
            layout = [handle convertRectToScreen:[handle contentLayoutRect]];
            if (!NSIsEmptyRect(layout))
                visible = NSIntersectionRect(visible, layout);
        }
        if (!NSIsEmptyRect(visible)) {
            window_rect = [handle convertRectFromScreen:visible];
            target = [root convertRect:window_rect fromView:nil];
            /* Stay inside the unobscured rectangle and on backing-pixel
             * boundaries, avoiding fractional drawable/viewport dimensions. */
            target = [root backingAlignedRect:target options:NSAlignAllEdgesInward];
        }
    }
    if (!NSEqualRects([native->viewport frame], target)) {
        [native->viewport setFrame:target];
        window->state.resized = 1;
    }
}

/* Only non-native modes belong here: the user's last ordinary window frame
 * is captured when LEAVING Windowed, never during a fullscreen exit callback. */
static maage_status maage_cocoa_restore_non_native(maage_window *window, maage_window_mode mode)
{
    maage_cocoa_window *native = window->platform;
    NSWindow *handle = native->handle;
    NSScreen *screen = [handle screen] ?: [NSScreen mainScreen];
    NSRect frame = native->normal_frame;
    if (mode == MAAGE_WINDOW_BORDERLESS_FULLSCREEN) {
        frame = [screen frame];
        if (!window->full_display_content) {
            if (@available(macOS 12.0, *)) {
                NSEdgeInsets inset = [screen safeAreaInsets];
                frame.origin.x += inset.left;
                frame.origin.y += inset.bottom;
                frame.size.width -= inset.left + inset.right;
                frame.size.height -= inset.top + inset.bottom;
            }
        }
    }
    [handle setStyleMask:maage_cocoa_style(window, mode == MAAGE_WINDOW_WINDOWED)];
    [handle setTitlebarAppearsTransparent:mode != MAAGE_WINDOW_WINDOWED];
    [handle setFrame:frame display:YES];
    window->state.mode = mode;
    native->desired_mode = mode;
    window->state.mode_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
    maage_cocoa_layout_viewport(window);
    window->state.resized = 1;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
}

/* A failed asynchronous toggle is recoverable and remains observable through
 * get_state(). Reporting a failed poll would terminate the current Client loop,
 * so record the outcome on state and log it once at the callback boundary. */
static void maage_cocoa_transition_failure(maage_window *window, const char *message)
{
    window->state.mode_result = maage_status_make(MAAGE_STATUS_ERROR,
        MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    maage_log_status(MAAGE_LOG_WARNING, "maage.window.cocoa", window->state.mode_result, "%s", message);
}

/* Custom fullscreen may cover the screen frame. Native fullscreen remains
 * subject to AppKit's Spaces and display policies, even with full-size content. */
static void maage_cocoa_presentation(maage_window_system *system)
{
    maage_window *window;
    maage_cocoa_system *native = system->platform;
    maage_cocoa_window *handle;
    for (window = system->windows; window; window = window->next) {
        handle = window->platform;
        if (handle && (handle->transitioning || window->state.mode == MAAGE_WINDOW_NATIVE_FULLSCREEN))
            return;
    }
    for (window = system->windows; window; window = window->next) {
        if (window->state.visible && window->state.mode == MAAGE_WINDOW_BORDERLESS_FULLSCREEN) {
            [NSApp setPresentationOptions:NSApplicationPresentationAutoHideDock |
                NSApplicationPresentationAutoHideMenuBar];
            return;
        }
    }
    [NSApp setPresentationOptions:native->presentation];
}

static maage_status maage_cocoa_init(maage_window_system *system)
{
    maage_cocoa_system *native;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, MAAGE_DETAIL_WRONG_THREAD);
    if (maage_cocoa_active) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    system->platform = native;
    native->pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    native->presentation = [NSApp presentationOptions];
    native->previous_delegate = [[NSApp delegate] retain];
    native->delegate = [[MaaGEApplicationDelegate alloc] init];
    [NSApp setDelegate:native->delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    maage_cocoa_active = system;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
}
static void maage_cocoa_shutdown(maage_window_system *system)
{
    maage_cocoa_system *native = system->platform;
    if (!native) return;
    [NSApp setPresentationOptions:native->presentation];
    [NSApp setDelegate:native->previous_delegate];
    [native->previous_delegate release];
    [native->delegate release];
    [native->pool drain];
    maage_cocoa_active = NULL;
    free(native);
}
static maage_status maage_cocoa_title(maage_window *window, const char *title)
{
    maage_cocoa_window *native = window->platform;
    NSString *text = [NSString stringWithUTF8String:title];
    if (!text) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    [native->handle setTitle:text];
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
}
static maage_status maage_cocoa_visible(maage_window *window, int visible)
{
    maage_cocoa_window *native = window->platform;
    if (visible) {
        [native->handle makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    } else [native->handle orderOut:nil];
    window->state.visible = visible != 0;
    maage_cocoa_presentation(window->system);
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
}
static maage_status maage_cocoa_mode(maage_window *window, maage_window_mode mode)
{
    maage_cocoa_window *native = window->platform;
    NSWindow *handle = native->handle;
    maage_cocoa_system *system = window->system->platform;
    maage_status result;
    maage_window_mode previous = window->state.mode;
    if (![NSThread isMainThread])
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, MAAGE_DETAIL_WRONG_THREAD);
    if (native->transitioning)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    /* Creation expects SUCCESS even when the requested initial mode is already
     * Windowed. Preserve the existing synchronous no-change contract. */
    if (mode == previous)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    if (mode == MAAGE_WINDOW_NATIVE_FULLSCREEN && ![handle isVisible])
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    if (previous == MAAGE_WINDOW_NATIVE_FULLSCREEN) {
        native->desired_mode = mode;
        native->transitioning = 1;
        [handle toggleFullScreen:nil];
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    }
    if (previous == MAAGE_WINDOW_WINDOWED)
        native->normal_frame = [handle frame];
    if (mode == MAAGE_WINDOW_NATIVE_FULLSCREEN) {
        native->entered_from_mode = previous;
        native->desired_mode = mode;
        /* Disable full-size titlebar content for the native fullscreen Space;
         * the stable drawable child is additionally clipped to the safe layout. */
        [handle setStyleMask:maage_cocoa_style(window, 1) & ~NSWindowStyleMaskFullSizeContentView];
        [handle setFrame:native->normal_frame display:YES];
        [handle setCollectionBehavior:[handle collectionBehavior] | NSWindowCollectionBehaviorFullScreenPrimary];
        [NSApp setPresentationOptions:system->presentation];
        native->transitioning = 1;
        [handle toggleFullScreen:nil];
        window->state.resized = 1;
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    }
    result = maage_cocoa_restore_non_native(window, mode);
    maage_cocoa_presentation(window->system);
    return result;
}
static maage_status maage_cocoa_create(maage_window *window, const maage_window_config *config)
{
    maage_cocoa_window *native = calloc(1, sizeof(*native));
    MaaGEWindowDelegate *delegate;
    maage_status result;
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    window->platform = native;
    native->handle = [[MaaGEPlatformWindow alloc] initWithContentRect:
        NSMakeRect(0, 0, config->width, config->height)
        styleMask:maage_cocoa_style(window, 1) backing:NSBackingStoreBuffered defer:NO];
    if (!native->handle) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    [native->handle setReleasedWhenClosed:NO];
    [native->handle setAcceptsMouseMovedEvents:YES];
    /* The NSWindow's root contentView may extend behind titlebar/notch areas.
     * The independently sized drawable subview remains stable for all modes. */
    native->viewport = [[MaaGEViewport alloc] initWithFrame:[[native->handle contentView] bounds]];
    if (!native->viewport)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    [[native->handle contentView] addSubview:native->viewport];
    [native->viewport release];
    window->state.mode_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
    [native->handle setTitlebarAppearsTransparent:NO];
    [native->handle setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
    [native->handle center];
    native->normal_frame = [native->handle frame];
    delegate = [[MaaGEWindowDelegate alloc] init];
    delegate->owner = window;
    native->delegate = delegate;
    [native->handle setDelegate:delegate];
    result = maage_cocoa_title(window, config->title);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_cocoa_visible(window, config->visible);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    return maage_cocoa_mode(window, config->mode);
}
static void maage_cocoa_destroy(maage_window *window)
{
    maage_cocoa_window *native = window->platform;
    if (!native) return;
    maage_window_input_focus(window, 0);
    [native->handle setDelegate:nil];
    [native->handle orderOut:nil];
    [native->handle close];
    [native->handle release];
    [native->delegate release];
    free(native);
    maage_cocoa_presentation(window->system);
}
static maage_status maage_cocoa_cursor(maage_window *window, maage_window_cursor cursor)
{
    maage_cocoa_window *native = window->platform;
    MaaGEViewport *view = (MaaGEViewport *)native->viewport;
    NSPoint point;
    view->cursor_shape = cursor;
    [native->handle invalidateCursorRectsForView:view];
    point = [view convertPoint:[native->handle mouseLocationOutsideOfEventStream] fromView:nil];
    if (!window->input.captured && [native->handle isKeyWindow] && NSPointInRect(point, [view bounds]))
        [(cursor == MAAGE_WINDOW_CURSOR_POINTER ? [NSCursor pointingHandCursor] : [NSCursor arrowCursor]) set];
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
}

static maage_status maage_cocoa_capture(maage_window *window, int capture)
{
    maage_cocoa_window *native = window->platform;
    maage_window *other;
    NSView *view;
    NSRect content;
    CGPoint point;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, MAAGE_DETAIL_WRONG_THREAD);
    if (capture == window->input.captured) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
    if (capture) {
        if (![native->handle isKeyWindow] || ![NSApp isActive]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
        for (other = window->system->windows; other; other = other->next)
            if (other != window && other->input.captured) maage_window_capture_mouse(other, 0);
        if (CGAssociateMouseAndMouseCursorPosition(0) != kCGErrorSuccess)
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
        /* Freeze the native cursor inside this viewport even when activation
         * happened through the Dock or keyboard with the pointer elsewhere. */
        view = native->viewport;
        content = [native->handle convertRectToScreen:[view convertRect:[view bounds] toView:nil]];
        {
            NSRect primary = [[[NSScreen screens] objectAtIndex:0] frame];
            point.x = NSMidX(content) - NSMinX(primary);
            point.y = NSMaxY(primary) - NSMidY(content);
        }
        if (CGWarpMouseCursorPosition(point) != kCGErrorSuccess) {
            CGAssociateMouseAndMouseCursorPosition(1);
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
        }
        native->capture_time = [[NSProcessInfo processInfo] systemUptime];
        [native->handle setAcceptsMouseMovedEvents:YES];
        [NSCursor hide];
    } else {
        CGAssociateMouseAndMouseCursorPosition(1);
        [NSCursor unhide];
    }
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
}
static int maage_cocoa_input_event(maage_window_system *system, NSEvent *event)
{
    maage_window *window;
    maage_cocoa_window *native;
    maage_input_key key;
    int text_event = 0;
    NSEventType type = [event type];
    for (window = system->windows; window; window = window->next) {
        native = window->platform;
        if (!native || native->handle != [event window]) continue;
        if (type == NSEventTypeFlagsChanged) {
            maage_window_input_key(window, MAAGE_INPUT_KEY_SHIFT,
                ([event modifierFlags] & NSEventModifierFlagShift) != 0);
            maage_window_input_key(window, MAAGE_INPUT_KEY_CONTROL,
                ([event modifierFlags] & NSEventModifierFlagControl) != 0);
            maage_window_input_key(window, MAAGE_INPUT_KEY_LEFT_CONTROL,
                ([event modifierFlags] & NX_DEVICELCTLKEYMASK) != 0);
        }
        if (type == NSEventTypeKeyDown || type == NSEventTypeKeyUp) {
            if (type == NSEventTypeKeyDown && ([event modifierFlags] & NSEventModifierFlagCommand)) return 0;
            if (type == NSEventTypeKeyDown && !window->input.captured) {
                NSString *characters = [event characters];
                NSUInteger n;
                for (n = 0; n < [characters length]; ++n) {
                    unsigned int character = [characters characterAtIndex:n];
                    maage_window_input_character(window, character);
                    if (character == 8 || character == 127 || (character >= 32 && character <= 126))
                        text_event = 1;
                }
            }
            switch ([event keyCode]) {
            case 13: key = MAAGE_INPUT_KEY_W; break;
            case 0: key = MAAGE_INPUT_KEY_A; break;
            case 1: key = MAAGE_INPUT_KEY_S; break;
            case 2: key = MAAGE_INPUT_KEY_D; break;
            case 49: key = MAAGE_INPUT_KEY_SPACE; break;
            case 53: key = MAAGE_INPUT_KEY_ESCAPE; break;
            case 96: key = MAAGE_INPUT_KEY_F5; break;
            case 99: key = MAAGE_INPUT_KEY_F3; break;
            case 103: key = MAAGE_INPUT_KEY_F11; break;
            case 36: case 76: key = MAAGE_INPUT_KEY_ENTER; break;
            case 48: key = MAAGE_INPUT_KEY_TAB; break;
            case 126: key = MAAGE_INPUT_KEY_UP; break;
            case 125: key = MAAGE_INPUT_KEY_DOWN; break;
            case 123: key = MAAGE_INPUT_KEY_LEFT; break;
            case 124: key = MAAGE_INPUT_KEY_RIGHT; break;
            case 14: key = MAAGE_INPUT_KEY_E; break;
            case 12: key = MAAGE_INPUT_KEY_Q; break;
            case 18: key = MAAGE_INPUT_KEY_1; break;
            case 19: key = MAAGE_INPUT_KEY_2; break;
            case 20: key = MAAGE_INPUT_KEY_3; break;
            case 21: key = MAAGE_INPUT_KEY_4; break;
            case 23: key = MAAGE_INPUT_KEY_5; break;
            case 22: key = MAAGE_INPUT_KEY_6; break;
            case 26: key = MAAGE_INPUT_KEY_7; break;
            case 28: key = MAAGE_INPUT_KEY_8; break;
            case 25: key = MAAGE_INPUT_KEY_9; break;
            default: return text_event;
            }
            maage_window_input_key(window, key, type == NSEventTypeKeyDown);
            return 1;
        }
        if (type == NSEventTypeMouseMoved || type == NSEventTypeLeftMouseDown ||
            type == NSEventTypeLeftMouseUp || type == NSEventTypeLeftMouseDragged ||
            type == NSEventTypeRightMouseDown || type == NSEventTypeRightMouseUp ||
            type == NSEventTypeRightMouseDragged) {
            NSView *view = native->viewport;
            NSPoint point = [view convertPoint:[event locationInWindow] fromView:nil];
            NSRect bounds = [view bounds];
            window->input.mouse_x = point.x - NSMinX(bounds);
            window->input.mouse_y = [view isFlipped] ? point.y - NSMinY(bounds) : NSMaxY(bounds) - point.y;
            if (type == NSEventTypeLeftMouseDown && NSPointInRect(point, bounds))
                window->input.primary_pressed = window->input.primary_down = 1;
            if (type == NSEventTypeLeftMouseUp) window->input.primary_down = 0;
            if (type == NSEventTypeRightMouseDown && NSPointInRect(point, bounds))
                window->input.secondary_pressed = window->input.secondary_down = 1;
            if (type == NSEventTypeRightMouseUp) window->input.secondary_down = 0;
        }
        if (type == NSEventTypeScrollWheel) window->input.scroll_y += [event scrollingDeltaY];
        if (window->input.captured && [event timestamp] >= native->capture_time && (type == NSEventTypeMouseMoved ||
            type == NSEventTypeLeftMouseDragged || type == NSEventTypeRightMouseDragged ||
            type == NSEventTypeOtherMouseDragged)) {
            window->input.mouse_delta_x += [event deltaX];
            window->input.mouse_delta_y += [event deltaY];
        }
        break;
    }
    return 0;
}
static maage_status maage_cocoa_poll(maage_window_system *system, int timeout_ms)
{
    NSAutoreleasePool *pool;
    NSEvent *event;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, MAAGE_DETAIL_WRONG_THREAD);
    pool = [[NSAutoreleasePool alloc] init];
    event = [NSApp nextEventMatchingMask:NSEventMaskAny
        untilDate:timeout_ms < 0 ? [NSDate distantFuture] :
            [NSDate dateWithTimeIntervalSinceNow:(double)timeout_ms / 1000.0]
        inMode:NSDefaultRunLoopMode dequeue:YES];
    while (event) {
        if (!maage_cocoa_input_event(system, event)) [NSApp sendEvent:event];
        event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast]
            inMode:NSDefaultRunLoopMode dequeue:YES];
    }
    [NSApp updateWindows];
    [pool drain];
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
}
static maage_status maage_cocoa_state(maage_window *window)
{
    maage_cocoa_window *native = window->platform;
    NSView *view;
    NSScreen *screen;
    NSRect logical;
    NSRect backing;
    NSRect content;
    NSRect safe;
    NSEdgeInsets inset;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, MAAGE_DETAIL_WRONG_THREAD);
    maage_cocoa_layout_viewport(window);
    view = native->viewport;
    screen = [native->handle screen];
    logical = [view bounds];
    backing = [view convertRectToBacking:logical];
    maage_window_input_focus(window, [native->handle isKeyWindow] && [NSApp isActive]);
    memset(&window->state.safe_area, 0, sizeof(window->state.safe_area));
    if (window->state.width != (int)logical.size.width ||
        window->state.height != (int)logical.size.height ||
        window->state.framebuffer_width != (int)backing.size.width ||
        window->state.framebuffer_height != (int)backing.size.height)
        window->state.resized = 1;
    window->state.width = (int)logical.size.width;
    window->state.height = (int)logical.size.height;
    window->state.framebuffer_width = (int)backing.size.width;
    window->state.framebuffer_height = (int)backing.size.height;
    window->state.visible = [native->handle isVisible];
    window->state.mode_transitioning = native->transitioning;
    if (screen) {
        if (@available(macOS 12.0, *)) {
            inset = [screen safeAreaInsets];
            safe = [screen frame];
            safe.origin.x += inset.left;
            safe.origin.y += inset.bottom;
            safe.size.width -= inset.left + inset.right;
            safe.size.height -= inset.top + inset.bottom;
            content = [native->handle convertRectToScreen:[view convertRect:logical toView:nil]];
            window->state.safe_area.top = MAX(0, NSMaxY(content) - NSMaxY(safe));
            window->state.safe_area.bottom = MAX(0, NSMinY(safe) - NSMinY(content));
            window->state.safe_area.left = MAX(0, NSMinX(safe) - NSMinX(content));
            window->state.safe_area.right = MAX(0, NSMaxX(content) - NSMaxX(safe));
        }
    }
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, 0);
}
static maage_status maage_cocoa_resize(maage_window *window, int width, int height)
{
    maage_cocoa_window *native = window->platform;
    if (native->transitioning) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_RESIZE, 0);
    [native->handle setContentSize:NSMakeSize(width, height)];
    window->state.resized = 1;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_RESIZE, 0);
}
static void maage_cocoa_native(maage_window *window, maage_window_native *output)
{
    maage_cocoa_window *native = window->platform;
    output->handles.cocoa.window = native->handle;
    output->handles.cocoa.view = native->viewport;
}
static maage_status maage_cocoa_surface(maage_window *window, int external)
{
    maage_cocoa_window *native = window->platform;
    if (!external && native->bitmap_view) {
        [native->bitmap_view removeFromSuperview];
        [native->bitmap_view release];
        native->bitmap_view = nil;
    }
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
}
static maage_status maage_cocoa_pixels(maage_window *window, const maage_pixel_buffer *buffer)
{
    maage_cocoa_window *native = window->platform;
    NSView *parent = native->viewport;
    MaaGEBitmapView *view = (MaaGEBitmapView *)native->bitmap_view;
    NSAutoreleasePool *pool;
    NSBitmapImageRep *image;
    int row;
    if (![NSThread isMainThread]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, MAAGE_DETAIL_WRONG_THREAD);
    if ([native->handle isMiniaturized]) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0);
    if (!view) {
        view = [[MaaGEBitmapView alloc] initWithFrame:[parent bounds]];
        if (!view) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0);
        [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [parent addSubview:view];
        native->bitmap_view = view;
    }
    pool = [[NSAutoreleasePool alloc] init];
    if (!view->image || [view->image pixelsWide] != buffer->width || [view->image pixelsHigh] != buffer->height) {
        image = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:buffer->width
            pixelsHigh:buffer->height bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
            colorSpaceName:NSDeviceRGBColorSpace bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
            bytesPerRow:0 bitsPerPixel:32];
        if (!image) { [pool drain]; return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0); }
        [view->image release];
        view->image = image;
    }
    for (row = 0; row < buffer->height; ++row)
        memcpy([view->image bitmapData] + row * [view->image bytesPerRow],
            buffer->pixels + (size_t)row * buffer->stride, (size_t)buffer->width * 4);
    [view setNeedsDisplay:YES];
    [view displayIfNeeded];
    [pool drain];
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0);
}
const maage_window_backend_api maage_window_cocoa_api = {
    MAAGE_WINDOW_COCOA, maage_cocoa_init, maage_cocoa_shutdown, maage_cocoa_create,
    maage_cocoa_destroy, maage_cocoa_poll, maage_cocoa_state, maage_cocoa_title,
    maage_cocoa_visible, maage_cocoa_resize, maage_cocoa_mode, maage_cocoa_native,
    maage_cocoa_surface, maage_cocoa_pixels, maage_cocoa_capture, maage_cocoa_cursor
};
