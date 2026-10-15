#import <AppKit/AppKit.h>
#include "../window_internal.h"
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
    id delegate;
    NSRect normal_frame;
    maage_window_mode desired_mode;
    int transitioning;
    NSView *bitmap_view;
} maage_cocoa_window;

static maage_window_system *maage_cocoa_active;
static maage_window_result maage_cocoa_mode(maage_window *, maage_window_mode);
static maage_window_result maage_cocoa_state(maage_window *);

@interface MaaGEPlatformWindow : NSWindow
@end
@implementation MaaGEPlatformWindow
- (BOOL)canBecomeKeyWindow { return YES; }
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
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)application
{
    maage_window *window;
    (void)application;
    for (window = maage_cocoa_active->windows; window; window = window->next)
        window->state.close_requested = 1;
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
- (void)windowDidResignKey:(NSNotification *)notification
{
    (void)notification;
    owner->input.cancelled = 1;
    owner->input.down = 0;
}
- (void)windowDidResize:(NSNotification *)notification
{
    (void)notification;
    owner->state.resized = 1;
}
- (void)windowDidChangeBackingProperties:(NSNotification *)notification
{
    (void)notification;
    owner->state.resized = 1;
}
- (void)windowDidChangeScreen:(NSNotification *)notification
{
    (void)notification;
    owner->state.resized = 1;
}
- (void)windowWillEnterFullScreen:(NSNotification *)notification
{
    maage_cocoa_window *native = owner->platform;
    (void)notification;
    native->desired_mode = MAAGE_WINDOW_NATIVE_FULLSCREEN;
    native->transitioning = 1;
}
- (void)windowDidEnterFullScreen:(NSNotification *)notification
{
    maage_cocoa_window *native = owner->platform;
    (void)notification;
    native->transitioning = 0;
    owner->state.mode = MAAGE_WINDOW_NATIVE_FULLSCREEN;
    owner->state.resized = 1;
}
- (void)windowWillExitFullScreen:(NSNotification *)notification
{
    maage_cocoa_window *native = owner->platform;
    (void)notification;
    native->transitioning = 1;
    if (native->desired_mode == MAAGE_WINDOW_NATIVE_FULLSCREEN)
        native->desired_mode = MAAGE_WINDOW_WINDOWED;
}
- (void)windowDidExitFullScreen:(NSNotification *)notification
{
    maage_cocoa_window *native = owner->platform;
    (void)notification;
    native->transitioning = 0;
    owner->state.mode = MAAGE_WINDOW_WINDOWED;
    maage_cocoa_mode(owner, native->desired_mode);
    owner->state.resized = 1;
}
- (void)windowDidFailToEnterFullScreen:(NSWindow *)window
{
    maage_cocoa_window *native = owner->platform;
    (void)window;
    native->transitioning = 0;
    native->desired_mode = owner->state.mode;
}
- (void)windowDidFailToExitFullScreen:(NSWindow *)window
{
    maage_cocoa_window *native = owner->platform;
    (void)window;
    native->transitioning = 0;
    native->desired_mode = owner->state.mode;
}
- (NSSize)window:(NSWindow *)window willUseFullScreenContentSize:(NSSize)proposed
{
    if (owner->full_display_content) return [[window screen] frame].size;
    return proposed;
}
- (NSApplicationPresentationOptions)window:(NSWindow *)window
    willUseFullScreenPresentationOptions:(NSApplicationPresentationOptions)options
{
    (void)window;
    if (owner->full_display_content)
        return (options & ~(NSApplicationPresentationHideMenuBar | NSApplicationPresentationHideDock)) |
            NSApplicationPresentationAutoHideMenuBar | NSApplicationPresentationAutoHideDock;
    return options;
}
@end

static NSUInteger maage_cocoa_style(maage_window *window, int decorated)
{
    NSUInteger style = decorated ? (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable) : NSWindowStyleMaskBorderless;
    if (window->full_display_content) style |= NSWindowStyleMaskFullSizeContentView;
    return style;
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

static maage_window_result maage_cocoa_init(maage_window_system *system)
{
    maage_cocoa_system *native;
    if (![NSThread isMainThread]) return MAAGE_WINDOW_WRONG_THREAD;
    if (maage_cocoa_active) return MAAGE_WINDOW_UNAVAILABLE;
    native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_WINDOW_OUT_OF_MEMORY;
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
    return MAAGE_WINDOW_OK;
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
static maage_window_result maage_cocoa_title(maage_window *window, const char *title)
{
    maage_cocoa_window *native = window->platform;
    NSString *text = [NSString stringWithUTF8String:title];
    if (!text) return MAAGE_WINDOW_INVALID_ARGUMENT;
    [native->handle setTitle:text];
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_cocoa_visible(maage_window *window, int visible)
{
    maage_cocoa_window *native = window->platform;
    if (visible) {
        [native->handle makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    } else [native->handle orderOut:nil];
    window->state.visible = visible != 0;
    maage_cocoa_presentation(window->system);
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_cocoa_mode(maage_window *window, maage_window_mode mode)
{
    maage_cocoa_window *native = window->platform;
    NSWindow *handle = native->handle;
    NSScreen *screen = [handle screen] ?: [NSScreen mainScreen];
    NSRect frame;
    if (native->transitioning) return MAAGE_WINDOW_UNSUPPORTED;
    if (mode == MAAGE_WINDOW_NATIVE_FULLSCREEN && ![handle isVisible])
        return MAAGE_WINDOW_UNSUPPORTED;
    if (mode == window->state.mode) return MAAGE_WINDOW_OK;
    native->desired_mode = mode;
    if (window->state.mode == MAAGE_WINDOW_NATIVE_FULLSCREEN) {
        native->transitioning = 1;
        [handle toggleFullScreen:nil];
        return MAAGE_WINDOW_OK;
    }
    if (window->state.mode < MAAGE_WINDOW_BORDERLESS_FULLSCREEN)
        native->normal_frame = [handle frame];
    if (mode == MAAGE_WINDOW_NATIVE_FULLSCREEN) {
        [handle setStyleMask:maage_cocoa_style(window, 1)];
        [handle setFrame:native->normal_frame display:YES];
        [handle setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
        window->state.mode = MAAGE_WINDOW_WINDOWED;
        maage_cocoa_presentation(window->system);
        native->transitioning = 1;
        [handle toggleFullScreen:nil];
    } else {
        [handle setStyleMask:maage_cocoa_style(window, mode == MAAGE_WINDOW_WINDOWED)];
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
        } else frame = native->normal_frame;
        [handle setFrame:frame display:YES];
        window->state.mode = mode;
    }
    maage_cocoa_presentation(window->system);
    window->state.resized = 1;
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_cocoa_create(maage_window *window, const maage_window_config *config)
{
    maage_cocoa_window *native = calloc(1, sizeof(*native));
    MaaGEWindowDelegate *delegate;
    maage_window_result result;
    if (!native) return MAAGE_WINDOW_OUT_OF_MEMORY;
    window->platform = native;
    native->handle = [[MaaGEPlatformWindow alloc] initWithContentRect:
        NSMakeRect(0, 0, config->width, config->height)
        styleMask:maage_cocoa_style(window, 1) backing:NSBackingStoreBuffered defer:NO];
    if (!native->handle) return MAAGE_WINDOW_PLATFORM_ERROR;
    [native->handle setReleasedWhenClosed:NO];
    [native->handle setTitlebarAppearsTransparent:window->full_display_content != 0];
    [native->handle setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
    [native->handle center];
    native->normal_frame = [native->handle frame];
    delegate = [[MaaGEWindowDelegate alloc] init];
    delegate->owner = window;
    native->delegate = delegate;
    [native->handle setDelegate:delegate];
    result = maage_cocoa_title(window, config->title);
    if (result != MAAGE_WINDOW_OK) return result;
    result = maage_cocoa_visible(window, config->visible);
    if (result != MAAGE_WINDOW_OK) return result;
    return maage_cocoa_mode(window, config->mode);
}
static void maage_cocoa_destroy(maage_window *window)
{
    maage_cocoa_window *native = window->platform;
    if (!native) return;
    [native->handle setDelegate:nil];
    [native->handle orderOut:nil];
    [native->handle close];
    [native->handle release];
    [native->delegate release];
    free(native);
    maage_cocoa_presentation(window->system);
}
static maage_window_result maage_cocoa_poll(maage_window_system *system, int timeout_ms)
{
    NSAutoreleasePool *pool;
    NSEvent *event;
    maage_window *window;
    maage_cocoa_window *native;
    maage_input_state *input;
    NSPoint point;
    const char *text;
    int consumed;
    if (![NSThread isMainThread]) return MAAGE_WINDOW_WRONG_THREAD;
    pool = [[NSAutoreleasePool alloc] init];
    event = [NSApp nextEventMatchingMask:NSEventMaskAny
        untilDate:timeout_ms < 0 ? [NSDate distantFuture] :
            [NSDate dateWithTimeIntervalSinceNow:(double)timeout_ms / 1000.0]
        inMode:NSDefaultRunLoopMode dequeue:YES];
    while (event) {
        consumed = 0;
        for (window = system->windows; window; window = window->next) {
            native = window->platform;
            if ([event window] != native->handle || !window->input_enabled) continue;
            input = &window->input;
            input->shift = ([event modifierFlags] & NSEventModifierFlagShift) != 0;
            switch ([event type]) {
            case NSEventTypeLeftMouseDown: input->pressed = 1; input->down = 1; break;
            case NSEventTypeLeftMouseUp: input->released = 1; input->down = 0; break;
            case NSEventTypeScrollWheel: input->scroll += (float)[event scrollingDeltaY]; break;
            case NSEventTypeKeyDown:
                if ([event modifierFlags] & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) break;
                consumed = 1;
                switch ([event keyCode]) {
                case 48: input->key = MAAGE_KEY_TAB; break;
                case 36: input->key = MAAGE_KEY_ENTER; break;
                case 53: input->key = MAAGE_KEY_ESCAPE; break;
                case 51: input->key = MAAGE_KEY_BACKSPACE; break;
                case 123: input->key = MAAGE_KEY_LEFT; break;
                case 124: input->key = MAAGE_KEY_RIGHT; break;
                default:
                    if (!([event modifierFlags] & (NSEventModifierFlagCommand | NSEventModifierFlagControl))) {
                        text = [[event characters] UTF8String];
                        if (text) strncat(input->text, text, sizeof(input->text) - strlen(input->text) - 1);
                    }
                    break;
                }
                break;
            default: break;
            }
        }
        if (!consumed) [NSApp sendEvent:event];
        event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast]
            inMode:NSDefaultRunLoopMode dequeue:YES];
    }
    for (window = system->windows; window; window = window->next) {
        native = window->platform;
        point = [[native->handle contentView] convertPoint:[native->handle mouseLocationOutsideOfEventStream] fromView:nil];
        window->input.x = (float)point.x;
        window->input.y = (float)([[native->handle contentView] bounds].size.height - point.y);
    }
    [NSApp updateWindows];
    [pool drain];
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_cocoa_state(maage_window *window)
{
    maage_cocoa_window *native = window->platform;
    NSView *view = [native->handle contentView];
    NSScreen *screen = [native->handle screen];
    NSRect logical = [view bounds];
    NSRect backing = [view convertRectToBacking:logical];
    NSRect content;
    NSRect safe;
    NSEdgeInsets inset;
    memset(&window->state.safe_area, 0, sizeof(window->state.safe_area));
    window->state.width = (int)logical.size.width;
    window->state.height = (int)logical.size.height;
    window->state.framebuffer_width = (int)backing.size.width;
    window->state.framebuffer_height = (int)backing.size.height;
    window->state.visible = [native->handle isVisible];
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
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_cocoa_resize(maage_window *window, int width, int height)
{
    maage_cocoa_window *native = window->platform;
    if (native->transitioning) return MAAGE_WINDOW_UNSUPPORTED;
    [native->handle setContentSize:NSMakeSize(width, height)];
    window->state.resized = 1;
    return MAAGE_WINDOW_OK;
}
static void maage_cocoa_native(maage_window *window, maage_window_native *output)
{
    maage_cocoa_window *native = window->platform;
    output->handles.cocoa.window = native->handle;
    output->handles.cocoa.view = [native->handle contentView];
}
static maage_window_result maage_cocoa_surface(maage_window *window, int external)
{
    maage_cocoa_window *native = window->platform;
    if (!external && native->bitmap_view) {
        [native->bitmap_view removeFromSuperview];
        [native->bitmap_view release];
        native->bitmap_view = nil;
    }
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_cocoa_pixels(maage_window *window, const maage_pixel_buffer *buffer)
{
    maage_cocoa_window *native = window->platform;
    NSView *parent = [native->handle contentView];
    MaaGEBitmapView *view = (MaaGEBitmapView *)native->bitmap_view;
    NSAutoreleasePool *pool;
    NSBitmapImageRep *image;
    int row;
    if (![NSThread isMainThread]) return MAAGE_WINDOW_WRONG_THREAD;
    if ([native->handle isMiniaturized]) return MAAGE_WINDOW_UNAVAILABLE;
    if (!view) {
        view = [[MaaGEBitmapView alloc] initWithFrame:[parent bounds]];
        if (!view) return MAAGE_WINDOW_OUT_OF_MEMORY;
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
        if (!image) { [pool drain]; return MAAGE_WINDOW_OUT_OF_MEMORY; }
        [view->image release];
        view->image = image;
    }
    for (row = 0; row < buffer->height; ++row)
        memcpy([view->image bitmapData] + row * [view->image bytesPerRow],
            buffer->pixels + (size_t)row * buffer->stride, (size_t)buffer->width * 4);
    [view setNeedsDisplay:YES];
    [view displayIfNeeded];
    [pool drain];
    return MAAGE_WINDOW_OK;
}
const maage_window_backend_api maage_window_cocoa_api = {
    MAAGE_WINDOW_COCOA, maage_cocoa_init, maage_cocoa_shutdown, maage_cocoa_create,
    maage_cocoa_destroy, maage_cocoa_poll, maage_cocoa_state, maage_cocoa_title,
    maage_cocoa_visible, maage_cocoa_resize, maage_cocoa_mode, maage_cocoa_native,
    maage_cocoa_surface, maage_cocoa_pixels
};
