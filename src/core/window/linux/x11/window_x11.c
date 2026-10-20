#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "../../window_internal.h"

typedef struct maage_x11_system {
    Display *display;
    Atom close_atom;
    Atom protocols;
    Atom state_atom;
    Atom fullscreen;
    Atom motif;
    int has_fullscreen;
    int keycodes[MAAGE_INPUT_KEY_COUNT];
} maage_x11_system;
typedef struct maage_x11_window {
    Window handle;
    int normal_width;
    int normal_height;
    maage_window_mode decoration;
    Cursor hidden_cursor;
    int pointer_x;
    int pointer_y;
    int warp_pending;
    int warp_x;
    int warp_y;
} maage_x11_window;
static maage_window_result maage_x11_capture(maage_window *, int);

static int maage_x11_contains(Display *display, Window window, Atom property, Atom atom)
{
    Atom actual;
    int format;
    unsigned long count, remaining, index;
    unsigned char *data = NULL;
    int found = 0;
    if (XGetWindowProperty(display, window, property, 0, 1024, False, XA_ATOM,
        &actual, &format, &count, &remaining, &data) == Success && format == 32) {
        for (index = 0; index < count; ++index)
            if (((Atom *)data)[index] == atom) found = 1;
    }
    if (data) XFree(data);
    return found;
}
static maage_window_result maage_x11_init(maage_window_system *system)
{
    maage_x11_system *native = (maage_x11_system *)calloc(1, sizeof(*native));
    XkbDescPtr keyboard;
    int code, key;
    Bool detectable;
    static const char names[MAAGE_INPUT_KEY_COUNT][5] = {
        "AD02", "AC01", "AC02", "AC03", "SPCE", "ESC "
    };
    if (!native) return MAAGE_WINDOW_OUT_OF_MEMORY;
    system->platform = native;
    native->display = XOpenDisplay(NULL);
    if (!native->display) return MAAGE_WINDOW_UNAVAILABLE;
    keyboard = XkbGetKeyboard(native->display, XkbAllComponentsMask, XkbUseCoreKbd);
    if (keyboard && keyboard->names && keyboard->names->keys) {
        for (code = keyboard->min_key_code; code <= keyboard->max_key_code; ++code)
            for (key = 0; key < MAAGE_INPUT_KEY_COUNT; ++key)
                if (!memcmp(keyboard->names->keys[code].name, names[key], 4))
                    native->keycodes[key] = code;
    }
    if (keyboard) XkbFreeKeyboard(keyboard, XkbAllComponentsMask, True);
    XkbSetDetectableAutoRepeat(native->display, True, &detectable);
    native->close_atom = XInternAtom(native->display, "WM_DELETE_WINDOW", False);
    native->protocols = XInternAtom(native->display, "WM_PROTOCOLS", False);
    native->state_atom = XInternAtom(native->display, "_NET_WM_STATE", False);
    native->fullscreen = XInternAtom(native->display, "_NET_WM_STATE_FULLSCREEN", False);
    native->motif = XInternAtom(native->display, "_MOTIF_WM_HINTS", False);
    native->has_fullscreen = maage_x11_contains(native->display, DefaultRootWindow(native->display),
        XInternAtom(native->display, "_NET_SUPPORTED", False), native->fullscreen);
    return MAAGE_WINDOW_OK;
}
static void maage_x11_shutdown(maage_window_system *system)
{
    maage_x11_system *native = system->platform;
    if (!native) return;
    if (native->display) XCloseDisplay(native->display);
    free(native);
}
static maage_window_result maage_x11_title(maage_window *window, const char *title)
{
    maage_x11_system *system = window->system->platform;
    maage_x11_window *native = window->platform;
    XChangeProperty(system->display, native->handle,
        XInternAtom(system->display, "_NET_WM_NAME", False),
        XInternAtom(system->display, "UTF8_STRING", False), 8, PropModeReplace,
        (const unsigned char *)title, (int)strlen(title));
    XStoreName(system->display, native->handle, title);
    XFlush(system->display);
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_x11_visible(maage_window *window, int visible)
{
    maage_x11_system *system = window->system->platform;
    maage_x11_window *native = window->platform;
    if (visible) XMapWindow(system->display, native->handle);
    else XUnmapWindow(system->display, native->handle);
    XFlush(system->display);
    window->state.visible = visible != 0;
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_x11_resize(maage_window *window, int width, int height)
{
    maage_x11_system *system = window->system->platform;
    maage_x11_window *native = window->platform;
    XResizeWindow(system->display, native->handle, (unsigned int)width, (unsigned int)height);
    native->normal_width = width;
    native->normal_height = height;
    XFlush(system->display);
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_x11_mode(maage_window *window, maage_window_mode mode)
{
    maage_x11_system *system = window->system->platform;
    maage_x11_window *native = window->platform;
    XEvent event;
    unsigned long hints[5];
    if (mode == MAAGE_WINDOW_NATIVE_FULLSCREEN) return MAAGE_WINDOW_UNSUPPORTED;
    if (mode == MAAGE_WINDOW_BORDERLESS_FULLSCREEN && !system->has_fullscreen)
        return MAAGE_WINDOW_UNSUPPORTED;
    memset(hints, 0, sizeof(hints));
    hints[0] = 2;
    hints[2] = mode == MAAGE_WINDOW_WINDOWED ? 1 : 0;
    XChangeProperty(system->display, native->handle, system->motif, system->motif,
        32, PropModeReplace, (unsigned char *)hints, 5);
    native->decoration = mode == MAAGE_WINDOW_WINDOWED ? MAAGE_WINDOW_WINDOWED : MAAGE_WINDOW_BORDERLESS_WINDOWED;
    if (window->state.visible) {
        memset(&event, 0, sizeof(event));
        event.xclient.type = ClientMessage;
        event.xclient.window = native->handle;
        event.xclient.message_type = system->state_atom;
        event.xclient.format = 32;
        event.xclient.data.l[0] = mode == MAAGE_WINDOW_BORDERLESS_FULLSCREEN ? 1 : 0;
        event.xclient.data.l[1] = (long)system->fullscreen;
        event.xclient.data.l[3] = 1;
        if (!XSendEvent(system->display, DefaultRootWindow(system->display), False,
            SubstructureRedirectMask | SubstructureNotifyMask, &event)) return MAAGE_WINDOW_PLATFORM_ERROR;
    } else {
        if (mode == MAAGE_WINDOW_BORDERLESS_FULLSCREEN)
            XChangeProperty(system->display, native->handle, system->state_atom, XA_ATOM,
                32, PropModeReplace, (unsigned char *)&system->fullscreen, 1);
        else XDeleteProperty(system->display, native->handle, system->state_atom);
    }
    XFlush(system->display);
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_x11_create(maage_window *window, const maage_window_config *config)
{
    maage_x11_system *system = window->system->platform;
    maage_x11_window *native = (maage_x11_window *)calloc(1, sizeof(*native));
    maage_window_result result;
    if (!native) return MAAGE_WINDOW_OUT_OF_MEMORY;
    window->platform = native;
    native->normal_width = config->width;
    native->normal_height = config->height;
    native->handle = XCreateSimpleWindow(system->display, DefaultRootWindow(system->display),
        0, 0, (unsigned int)config->width, (unsigned int)config->height, 0, 0, 0);
    if (!native->handle) return MAAGE_WINDOW_PLATFORM_ERROR;
    XSelectInput(system->display, native->handle, StructureNotifyMask | ExposureMask | PropertyChangeMask |
        KeyPressMask | KeyReleaseMask | FocusChangeMask | PointerMotionMask | ButtonPressMask);
    XSetWMProtocols(system->display, native->handle, &system->close_atom, 1);
    result = maage_x11_title(window, config->title);
    if (result != MAAGE_WINDOW_OK) return result;
    result = maage_x11_mode(window, config->mode);
    if (result != MAAGE_WINDOW_OK) return result;
    return maage_x11_visible(window, config->visible);
}
static void maage_x11_destroy(maage_window *window)
{
    maage_x11_system *system = window->system->platform;
    maage_x11_window *native = window->platform;
    if (!native) return;
    maage_window_input_focus(window, 0);
    if (native->hidden_cursor) XFreeCursor(system->display, native->hidden_cursor);
    if (native->handle) XDestroyWindow(system->display, native->handle);
    XFlush(system->display);
    free(native);
}
static maage_window_result maage_x11_capture(maage_window *window, int capture)
{
    maage_x11_system *system = window->system->platform;
    maage_x11_window *native = window->platform;
    maage_window *other;
    XWindowAttributes attributes;
    Pixmap bitmap;
    XColor color;
    int key;
    static const char empty[1] = {0};
    if (capture == window->input.captured) return MAAGE_WINDOW_OK;
    if (capture) {
        for (key = 0; key < MAAGE_INPUT_KEY_COUNT; ++key)
            if (!system->keycodes[key]) return MAAGE_WINDOW_UNSUPPORTED;
        if (!XGetWindowAttributes(system->display, native->handle, &attributes))
            return MAAGE_WINDOW_PLATFORM_ERROR;
        for (other = window->system->windows; other; other = other->next)
            if (other != window && other->input.captured) maage_window_capture_mouse(other, 0);
        if (!native->hidden_cursor) {
            bitmap = XCreateBitmapFromData(system->display, native->handle, empty, 1, 1);
            if (!bitmap) return MAAGE_WINDOW_PLATFORM_ERROR;
            memset(&color, 0, sizeof(color));
            native->hidden_cursor = XCreatePixmapCursor(system->display, bitmap, bitmap, &color, &color, 0, 0);
            XFreePixmap(system->display, bitmap);
            if (!native->hidden_cursor) return MAAGE_WINDOW_PLATFORM_ERROR;
        }
        if (XGrabPointer(system->display, native->handle, False,
            PointerMotionMask | ButtonPressMask, GrabModeAsync, GrabModeAsync,
            native->handle, native->hidden_cursor, CurrentTime) != GrabSuccess)
            return MAAGE_WINDOW_UNAVAILABLE;
        native->pointer_x = native->warp_x = attributes.width / 2;
        native->pointer_y = native->warp_y = attributes.height / 2;
        native->warp_pending = 1;
        XWarpPointer(system->display, None, native->handle, 0, 0, 0, 0, native->pointer_x, native->pointer_y);
    } else {
        XUngrabPointer(system->display, CurrentTime);
        native->warp_pending = 0;
    }
    XFlush(system->display);
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_x11_poll(maage_window_system *system, int timeout_ms)
{
    maage_x11_system *native = system->platform;
    maage_x11_window *handle;
    maage_window *window;
    struct pollfd fd;
    XEvent event, next;
    XWindowAttributes attributes;
    int result, key;
    if (timeout_ms && !XPending(native->display)) {
        fd.fd = ConnectionNumber(native->display);
        fd.events = POLLIN;
        result = poll(&fd, 1, timeout_ms);
        if (result < 0) return errno == EINTR ? MAAGE_WINDOW_OK : MAAGE_WINDOW_PLATFORM_ERROR;
        if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) return MAAGE_WINDOW_PLATFORM_ERROR;
    }
    while (XPending(native->display)) {
        XNextEvent(native->display, &event);
        for (window = system->windows; window; window = window->next) {
            handle = window->platform;
            if (handle->handle != event.xany.window) continue;
            if (event.type == FocusIn && event.xfocus.mode != NotifyGrab && event.xfocus.mode != NotifyUngrab)
                maage_window_input_focus(window, 1);
            if (event.type == FocusOut && event.xfocus.mode != NotifyGrab && event.xfocus.mode != NotifyUngrab)
                maage_window_input_focus(window, 0);
            if (event.type == UnmapNotify) maage_window_input_focus(window, 0);
            if (event.type == KeyPress || event.type == KeyRelease) {
                /* Servers without detectable repeat emit release/press pairs. */
                if (event.type == KeyRelease && XPending(native->display)) {
                    XPeekEvent(native->display, &next);
                    if (next.type == KeyPress && next.xkey.keycode == event.xkey.keycode &&
                        next.xkey.time == event.xkey.time && next.xkey.window == event.xkey.window) continue;
                }
                for (key = 0; key < MAAGE_INPUT_KEY_COUNT; ++key)
                    if (native->keycodes[key] && native->keycodes[key] == (int)event.xkey.keycode)
                        maage_window_input_key(window, (maage_input_key)key, event.type == KeyPress);
            }
            if (event.type == ButtonPress && event.xbutton.button == Button1) window->input.primary_pressed = 1;
            if (event.type == MotionNotify && window->input.captured) {
                if (handle->warp_pending && event.xmotion.x == handle->warp_x && event.xmotion.y == handle->warp_y)
                    handle->warp_pending = 0;
                else {
                    window->input.mouse_delta_x += event.xmotion.x - handle->pointer_x;
                    window->input.mouse_delta_y += event.xmotion.y - handle->pointer_y;
                }
                handle->pointer_x = event.xmotion.x;
                handle->pointer_y = event.xmotion.y;
            }
            if (event.type == ClientMessage && event.xclient.message_type == native->protocols &&
                (Atom)event.xclient.data.l[0] == native->close_atom) window->state.close_requested = 1;
            if (event.type == ConfigureNotify) window->state.resized = 1;
            if (event.type == DestroyNotify) {
                maage_window_input_focus(window, 0);
                handle->handle = 0;
                window->state.close_requested = 1;
            }
        }
    }
    for (window = system->windows; window; window = window->next) {
        handle = window->platform;
        if (window->input.captured && !handle->warp_pending &&
            XGetWindowAttributes(native->display, handle->handle, &attributes) &&
            (handle->pointer_x != attributes.width / 2 || handle->pointer_y != attributes.height / 2)) {
            handle->warp_x = attributes.width / 2;
            handle->warp_y = attributes.height / 2;
            handle->warp_pending = 1;
            XWarpPointer(native->display, None, handle->handle, 0, 0, 0, 0, handle->warp_x, handle->warp_y);
        }
    }
    XFlush(native->display);
    return MAAGE_WINDOW_OK;
}
static maage_window_result maage_x11_state(maage_window *window)
{
    maage_x11_system *system = window->system->platform;
    maage_x11_window *native = window->platform;
    XWindowAttributes attributes;
    if (!native->handle) return MAAGE_WINDOW_OK;
    if (!XGetWindowAttributes(system->display, native->handle, &attributes)) return MAAGE_WINDOW_PLATFORM_ERROR;
    window->state.width = window->state.framebuffer_width = attributes.width;
    window->state.height = window->state.framebuffer_height = attributes.height;
    window->state.visible = attributes.map_state != IsUnmapped;
    window->state.mode = maage_x11_contains(system->display, native->handle, system->state_atom,
        system->fullscreen) ? MAAGE_WINDOW_BORDERLESS_FULLSCREEN : native->decoration;
    return MAAGE_WINDOW_OK;
}
static void maage_x11_native(maage_window *window, maage_window_native *output)
{
    maage_x11_system *system = window->system->platform;
    maage_x11_window *native = window->platform;
    output->handles.x11.display = system->display;
    output->handles.x11.window = native->handle;
}
static unsigned long maage_x11_channel(unsigned char value, unsigned long mask)
{
    unsigned int shift = 0;
    if (!mask) return 0;
    while (!(mask & 1)) { mask >>= 1; ++shift; }
    return (((unsigned long)value * mask + 127) / 255) << shift;
}
static maage_window_result maage_x11_pixels(maage_window *window, const maage_pixel_buffer *buffer)
{
    maage_x11_system *system = window->system->platform;
    maage_x11_window *native = window->platform;
    XWindowAttributes attributes;
    XImage *image;
    GC gc;
    int x, y;
    unsigned long pixel;
    const unsigned char *source;
    if (!XGetWindowAttributes(system->display, native->handle, &attributes)) return MAAGE_WINDOW_PLATFORM_ERROR;
    if (attributes.visual->class != TrueColor) return MAAGE_WINDOW_UNSUPPORTED;
    image = XCreateImage(system->display, attributes.visual, (unsigned int)attributes.depth, ZPixmap,
        0, NULL, (unsigned int)buffer->width, (unsigned int)buffer->height, 32, 0);
    if (!image) return MAAGE_WINDOW_OUT_OF_MEMORY;
    if (image->bytes_per_line <= 0 || (size_t)image->bytes_per_line > (size_t)-1 / (size_t)buffer->height) {
        XDestroyImage(image);
        return MAAGE_WINDOW_UNSUPPORTED;
    }
    image->data = (char *)calloc((size_t)image->bytes_per_line, (size_t)buffer->height);
    if (!image->data) { XDestroyImage(image); return MAAGE_WINDOW_OUT_OF_MEMORY; }
    for (y = 0; y < buffer->height; ++y) {
        source = buffer->pixels + (size_t)y * buffer->stride;
        for (x = 0; x < buffer->width; ++x) {
            pixel = maage_x11_channel(source[0], image->red_mask) |
                maage_x11_channel(source[1], image->green_mask) | maage_x11_channel(source[2], image->blue_mask);
            XPutPixel(image, x, y, pixel);
            source += 4;
        }
    }
    gc = XCreateGC(system->display, native->handle, 0, NULL);
    if (!gc) { XDestroyImage(image); return MAAGE_WINDOW_PLATFORM_ERROR; }
    XPutImage(system->display, native->handle, gc, image, 0, 0, 0, 0,
        (unsigned int)buffer->width, (unsigned int)buffer->height);
    XFreeGC(system->display, gc);
    XDestroyImage(image);
    XFlush(system->display);
    return MAAGE_WINDOW_OK;
}
const maage_window_backend_api maage_window_x11_api = {
    MAAGE_WINDOW_X11, maage_x11_init, maage_x11_shutdown, maage_x11_create,
    maage_x11_destroy, maage_x11_poll, maage_x11_state, maage_x11_title,
    maage_x11_visible, maage_x11_resize, maage_x11_mode, maage_x11_native, NULL, maage_x11_pixels,
    maage_x11_capture
};
