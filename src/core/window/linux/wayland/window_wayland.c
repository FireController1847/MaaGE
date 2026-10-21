/* Wayland's generated protocol headers require C99; the exception is local to
 * this adapter. Public interfaces and common window code remain ISO C90. */
#define _POSIX_C_SOURCE 200809L
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "../../window_internal.h"

typedef struct maage_wayland_system {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *shell;
    struct zxdg_decoration_manager_v1 *decorations;
    uint32_t compositor_name, shm_name, shell_name;
    int disconnected;
} maage_wayland_system;
typedef struct maage_wayland_buffer maage_wayland_buffer;
typedef struct maage_wayland_window {
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct zxdg_toplevel_decoration_v1 *decoration;
    maage_wayland_buffer *buffers;
    int configured;
    int visible;
    int pending_width, pending_height;
    int fullscreen;
    maage_window_mode requested_mode;
    uint32_t decoration_mode;
    maage_status error;
    char *title;
} maage_wayland_window;
struct maage_wayland_buffer {
    struct wl_buffer *handle;
    void *memory;
    size_t size;
    maage_wayland_window *owner;
    maage_wayland_buffer *next;
};

static void maage_wayland_buffer_free(maage_wayland_buffer *buffer)
{
    wl_buffer_destroy(buffer->handle);
    munmap(buffer->memory, buffer->size);
    free(buffer);
}
static void maage_wayland_buffer_release(void *data, struct wl_buffer *handle)
{
    maage_wayland_buffer *buffer = data;
    maage_wayland_buffer **link = &buffer->owner->buffers;
    (void)handle;
    while (*link && *link != buffer) link = &(*link)->next;
    if (*link) *link = buffer->next;
    maage_wayland_buffer_free(buffer);
}
static const struct wl_buffer_listener maage_wayland_buffer_listener = {
    .release = maage_wayland_buffer_release
};
/* Shared CPU presentation transport for empty backing and software frames.
 * Submitted storage lives until wl_buffer.release, independently of the caller. */
static maage_status maage_wayland_backing(maage_window *window, const maage_pixel_buffer *pixels)
{
    maage_wayland_system *system = window->system->platform;
    maage_wayland_window *native = window->platform;
    struct wl_shm_pool *pool;
    maage_wayland_buffer *buffer;
    char name[] = "/tmp/maage-window-XXXXXX";
    int fd, width = window->state.width, height = window->state.height;
    size_t size;
    int x, y, pending = 0;
    const unsigned char *source;
    uint32_t *target;
    if ((window->surface_owned && !pixels) || !native->visible || !native->configured)
        return pixels ? MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0) : MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    if (pixels) {
        for (buffer = native->buffers; buffer; buffer = buffer->next) ++pending;
        if (pending >= 3) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    }
    if (width > INT_MAX / 4 || height > INT_MAX / (width * 4)) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    size = (size_t)width * 4 * (size_t)height;
    fd = mkstemp(name);
    if (fd < 0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    unlink(name);
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0); }
    buffer = calloc(1, sizeof(*buffer));
    if (!buffer) { close(fd); return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0); }
    buffer->memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer->memory == MAP_FAILED) { close(fd); free(buffer); return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0); }
    memset(buffer->memory, 0, size);
    if (pixels) {
        for (y = 0; y < height; ++y) {
            source = pixels->pixels + (size_t)y * pixels->stride;
            target = (uint32_t *)buffer->memory + (size_t)y * (size_t)width;
            for (x = 0; x < width; ++x) {
                target[x] = ((uint32_t)source[0] << 16) | ((uint32_t)source[1] << 8) | source[2];
                source += 4;
            }
        }
    }
    pool = wl_shm_create_pool(system->shm, fd, (int)size);
    close(fd);
    if (!pool) { munmap(buffer->memory, size); free(buffer); return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0); }
    buffer->handle = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    if (!buffer->handle) { munmap(buffer->memory, size); free(buffer); return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0); }
    buffer->size = size;
    buffer->owner = native;
    buffer->next = native->buffers;
    native->buffers = buffer;
    wl_buffer_add_listener(buffer->handle, &maage_wayland_buffer_listener, buffer);
    wl_surface_attach(native->surface, buffer->handle, 0, 0);
    wl_surface_damage(native->surface, 0, 0, width, height);
    wl_surface_commit(native->surface);
    if (wl_display_flush(system->display) < 0 && errno != EAGAIN) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
}
static void maage_wayland_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(shell, serial);
}
static const struct xdg_wm_base_listener maage_wayland_shell_listener = {
    .ping = maage_wayland_ping
};
static void maage_wayland_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    maage_wayland_system *native = data;
    if (!strcmp(interface, "wl_compositor")) {
        native->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < 3 ? version : 3);
        native->compositor_name = name;
    } else if (!strcmp(interface, "wl_shm")) {
        native->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        native->shm_name = name;
    } else if (!strcmp(interface, "xdg_wm_base")) {
        native->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        native->shell_name = name;
        xdg_wm_base_add_listener(native->shell, &maage_wayland_shell_listener, native);
    } else if (!strcmp(interface, "zxdg_decoration_manager_v1"))
        native->decorations = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
}
static void maage_wayland_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    maage_wayland_system *native = data;
    (void)registry;
    if (name == native->compositor_name || name == native->shm_name || name == native->shell_name)
        native->disconnected = 1;
}
static const struct wl_registry_listener maage_wayland_registry_listener = {
    .global = maage_wayland_global, .global_remove = maage_wayland_global_remove
};
static maage_status maage_wayland_init(maage_window_system *system)
{
    maage_wayland_system *native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    system->platform = native;
    native->display = wl_display_connect(NULL);
    if (!native->display) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    native->registry = wl_display_get_registry(native->display);
    if (!native->registry) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    wl_registry_add_listener(native->registry, &maage_wayland_registry_listener, native);
    if (wl_display_roundtrip(native->display) < 0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    if (!native->compositor || !native->shell || !native->shm) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
}
static void maage_wayland_shutdown(maage_window_system *system)
{
    maage_wayland_system *native = system->platform;
    if (!native) return;
    if (native->decorations) zxdg_decoration_manager_v1_destroy(native->decorations);
    if (native->shell) xdg_wm_base_destroy(native->shell);
    if (native->shm) wl_shm_destroy(native->shm);
    if (native->compositor) wl_compositor_destroy(native->compositor);
    if (native->registry) wl_registry_destroy(native->registry);
    if (native->display) wl_display_disconnect(native->display);
    free(native);
}
static void maage_wayland_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
    int32_t width, int32_t height, struct wl_array *states)
{
    maage_window *window = data;
    maage_wayland_window *native = window->platform;
    uint32_t *state;
    (void)toplevel;
    native->pending_width = width > 0 ? width : window->state.width;
    native->pending_height = height > 0 ? height : window->state.height;
    native->fullscreen = 0;
    wl_array_for_each(state, states)
        if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN) native->fullscreen = 1;
}
static void maage_wayland_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    maage_window *window = data;
    (void)toplevel;
    window->state.close_requested = 1;
}
static const struct xdg_toplevel_listener maage_wayland_toplevel_listener = {
    .configure = maage_wayland_toplevel_configure, .close = maage_wayland_toplevel_close
};
static void maage_wayland_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    maage_window *window = data;
    maage_wayland_window *native = window->platform;
    xdg_surface_ack_configure(surface, serial);
    native->configured = 1;
    window->state.width = native->pending_width;
    window->state.height = native->pending_height;
    window->state.framebuffer_width = window->state.width;
    window->state.framebuffer_height = window->state.height;
    window->state.mode = native->fullscreen ? MAAGE_WINDOW_BORDERLESS_FULLSCREEN :
        (native->decoration && native->decoration_mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE ?
        MAAGE_WINDOW_BORDERLESS_WINDOWED : MAAGE_WINDOW_WINDOWED);
    window->state.resized = 1;
    if (maage_status_code(native->error) == MAAGE_STATUS_SUCCESS)
        native->error = maage_wayland_backing(window, NULL);
}
static const struct xdg_surface_listener maage_wayland_surface_listener = {
    .configure = maage_wayland_configure
};
static void maage_wayland_decoration_configure(void *data,
    struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode)
{
    maage_window *window = data;
    maage_wayland_window *native = window->platform;
    (void)decoration;
    native->decoration_mode = mode;
    /* A compositor may reject the request. Report that explicitly to the poller. */
    if (native->requested_mode == MAAGE_WINDOW_BORDERLESS_WINDOWED &&
        mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE)
        native->error = MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
}
static const struct zxdg_toplevel_decoration_v1_listener maage_wayland_decoration_listener = {
    .configure = maage_wayland_decoration_configure
};
static maage_status maage_wayland_title(maage_window *window, const char *title)
{
    maage_wayland_window *native = window->platform;
    char *copy = malloc(strlen(title) + 1);
    if (!copy) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    strcpy(copy, title);
    free(native->title);
    native->title = copy;
    xdg_toplevel_set_title(native->toplevel, title);
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
}
static maage_status maage_wayland_mode(maage_window *window, maage_window_mode mode)
{
    maage_wayland_window *native = window->platform;
    if (mode == MAAGE_WINDOW_NATIVE_FULLSCREEN) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    if (mode == MAAGE_WINDOW_BORDERLESS_WINDOWED && !native->decoration)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    native->requested_mode = mode;
    if (native->decoration) zxdg_toplevel_decoration_v1_set_mode(native->decoration,
        mode == MAAGE_WINDOW_WINDOWED ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE :
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    if (mode == MAAGE_WINDOW_BORDERLESS_FULLSCREEN) xdg_toplevel_set_fullscreen(native->toplevel, NULL);
    else xdg_toplevel_unset_fullscreen(native->toplevel);
    if (native->configured) wl_surface_commit(native->surface);
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
}
static maage_status maage_wayland_visible(maage_window *window, int visible)
{
    maage_wayland_window *native = window->platform;
    maage_status result;
    visible = visible != 0;
    if (native->visible == visible) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    native->visible = visible;
    if (!visible) {
        wl_surface_attach(native->surface, NULL, 0, 0);
        wl_surface_commit(native->surface);
        native->configured = 0;
    } else {
        /* A null-buffer commit starts the xdg-shell configure handshake. */
        xdg_toplevel_set_title(native->toplevel, native->title);
        xdg_toplevel_set_app_id(native->toplevel, "org.maage.window");
        result = maage_wayland_mode(window, native->requested_mode);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        wl_surface_commit(native->surface);
    }
    window->state.visible = visible;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
}
static maage_status maage_wayland_create(maage_window *window, const maage_window_config *config)
{
    maage_wayland_system *system = window->system->platform;
    maage_wayland_window *native = calloc(1, sizeof(*native));
    maage_status result;
    if (!native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    window->platform = native;
    native->pending_width = config->width;
    native->pending_height = config->height;
    native->requested_mode = config->mode;
    native->surface = wl_compositor_create_surface(system->compositor);
    if (!native->surface) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    native->xdg_surface = xdg_wm_base_get_xdg_surface(system->shell, native->surface);
    if (!native->xdg_surface) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    xdg_surface_add_listener(native->xdg_surface, &maage_wayland_surface_listener, window);
    native->toplevel = xdg_surface_get_toplevel(native->xdg_surface);
    if (!native->toplevel) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    xdg_toplevel_add_listener(native->toplevel, &maage_wayland_toplevel_listener, window);
    xdg_toplevel_set_app_id(native->toplevel, "org.maage.window");
    if (system->decorations) {
        native->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(system->decorations, native->toplevel);
        if (!native->decoration) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
        zxdg_toplevel_decoration_v1_add_listener(native->decoration, &maage_wayland_decoration_listener, window);
    }
    result = maage_wayland_title(window, config->title);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_wayland_mode(window, config->mode);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    return maage_wayland_visible(window, config->visible);
}
static void maage_wayland_destroy(maage_window *window)
{
    maage_wayland_window *native = window->platform;
    maage_wayland_buffer *next;
    if (!native) return;
    if (native->decoration) zxdg_toplevel_decoration_v1_destroy(native->decoration);
    if (native->toplevel) xdg_toplevel_destroy(native->toplevel);
    if (native->xdg_surface) xdg_surface_destroy(native->xdg_surface);
    if (native->surface) wl_surface_destroy(native->surface);
    while (native->buffers) {
        next = native->buffers->next;
        maage_wayland_buffer_free(native->buffers);
        native->buffers = next;
    }
    free(native->title);
    free(native);
}
static maage_status maage_wayland_poll(maage_window_system *system, int timeout_ms)
{
    maage_wayland_system *native = system->platform;
    maage_window *window;
    maage_wayland_window *handle;
    struct pollfd fd;
    int ready, flushed, pending;
    if (native->disconnected) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
    pending = wl_display_dispatch_pending(native->display);
    if (pending < 0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
    /* Do not block after callbacks have already reported close/resize. */
    for (window = system->windows; window; window = window->next)
        if (window->state.close_requested || window->state.resized) timeout_ms = 0;
    while (wl_display_prepare_read(native->display) != 0)
        if (wl_display_dispatch_pending(native->display) < 0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
    fd.fd = wl_display_get_fd(native->display);
    fd.events = POLLIN;
    flushed = wl_display_flush(native->display);
    if (flushed < 0) {
        if (errno != EAGAIN) { wl_display_cancel_read(native->display); return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0); }
        fd.events |= POLLOUT;
    }
    ready = poll(&fd, 1, timeout_ms);
    if (ready < 0 || (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        wl_display_cancel_read(native->display);
        return ready < 0 && errno == EINTR ? MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0) : MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
    }
    if (ready && (fd.revents & POLLIN)) {
        if (wl_display_read_events(native->display) < 0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
    } else wl_display_cancel_read(native->display);
    if (ready && (fd.revents & POLLOUT) && wl_display_flush(native->display) < 0 && errno != EAGAIN)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
    if (wl_display_dispatch_pending(native->display) < 0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
    for (window = system->windows; window; window = window->next) {
        handle = window->platform;
        if (maage_status_code(handle->error) != MAAGE_STATUS_SUCCESS) return handle->error;
    }
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
}
static maage_status maage_wayland_state(maage_window *window)
{
    maage_wayland_window *native = window->platform;
    return native->error;
}
static maage_status maage_wayland_resize(maage_window *window, int width, int height)
{
    /* xdg-shell has no general client-driven toplevel resize request. A fixed
     * min/max size would change the public resizable-window contract. */
    (void)window; (void)width; (void)height;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_RESIZE, 0);
}
static void maage_wayland_native(maage_window *window, maage_window_native *output)
{
    maage_wayland_system *system = window->system->platform;
    maage_wayland_window *native = window->platform;
    output->handles.wayland.display = system->display;
    output->handles.wayland.surface = native->surface;
}
static maage_status maage_wayland_surface(maage_window *window, int external)
{
    maage_wayland_window *native = window->platform;
    if (external) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    native->error = maage_wayland_backing(window, NULL);
    return native->error;
}
static maage_status maage_wayland_pixels(maage_window *window, const maage_pixel_buffer *pixels)
{
    return maage_wayland_backing(window, pixels);
}
const maage_window_backend_api maage_window_wayland_api = {
    MAAGE_WINDOW_WAYLAND, maage_wayland_init, maage_wayland_shutdown, maage_wayland_create,
    maage_wayland_destroy, maage_wayland_poll, maage_wayland_state, maage_wayland_title,
    maage_wayland_visible, maage_wayland_resize, maage_wayland_mode, maage_wayland_native,
    maage_wayland_surface, maage_wayland_pixels, NULL
};
