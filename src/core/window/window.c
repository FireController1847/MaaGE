#include "window_internal.h"
#include <stdlib.h>
#include <string.h>

maage_status maage_window_config_init(maage_window_config *config)
{
    if (!config) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    memset(config, 0, sizeof(*config));
    config->title = "MaaGE";
    config->width = 1000;
    config->height = 650;
    config->visible = 1;
    config->mode = MAAGE_WINDOW_WINDOWED;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}



static maage_status maage_window_try_backend(maage_window_system *system,
    const maage_window_backend_api *api, maage_status_trace *trace)
{
    maage_status result;
    system->api = api;
    result = api->init(system);
    if (maage_status_code(result) == MAAGE_STATUS_NOOP)
        result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    if (maage_status_failed(result)) {
        api->shutdown(system);
        system->platform = NULL;
        system->api = NULL;
        MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE);
    }
    return result;
}

maage_status maage_window_system_create(maage_window_backend backend,
    maage_window_system **output, maage_status_trace *trace)
{
    maage_window_system *system;
    maage_status result;
    int recovered = 0;
    if (!output) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 1), MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE);
    *output = NULL;
    system = (maage_window_system *)calloc(1, sizeof(*system));
    if (!system) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE);
    result = MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
#if defined(MAAGE_WINDOW_HAS_WIN32)
    if (backend == MAAGE_WINDOW_AUTO || backend == MAAGE_WINDOW_WIN32)
        result = maage_window_try_backend(system, &maage_window_win32_api, trace);
#elif defined(MAAGE_WINDOW_HAS_COCOA)
    if (backend == MAAGE_WINDOW_AUTO || backend == MAAGE_WINDOW_COCOA)
        result = maage_window_try_backend(system, &maage_window_cocoa_api, trace);
#else
#if defined(MAAGE_WINDOW_HAS_WAYLAND)
    if (backend == MAAGE_WINDOW_WAYLAND || (backend == MAAGE_WINDOW_AUTO &&
        (getenv("WAYLAND_DISPLAY") || getenv("WAYLAND_SOCKET"))))
    {
        result = maage_window_try_backend(system, &maage_window_wayland_api, trace);
        recovered = maage_status_failed(result);
    }
#endif
#if defined(MAAGE_WINDOW_HAS_X11)
    if (backend == MAAGE_WINDOW_X11 || (backend == MAAGE_WINDOW_AUTO && !system->api))
        result = maage_window_try_backend(system, &maage_window_x11_api, trace);
#endif
#endif
    if (maage_status_failed(result)) {
        free(system);
        return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE);
    }
    *output = system;
    if (recovered) {
        maage_status_trace_recover(trace, result, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, __FILE__, __LINE__);
        return maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    }
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
}

maage_status maage_window_system_destroy(maage_window_system *system)
{
    maage_status result;
    if (!system) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    while (system->windows) {
        result = maage_window_destroy(system->windows);
        if (maage_status_failed(result)) return result;
    }
    system->api->shutdown(system);
    free(system);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

maage_status maage_window_create(maage_window_system *system,
    const maage_window_config *config, maage_window **output, maage_status_trace *trace)
{
    maage_window *window;
    maage_status result;
    if (!output) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_CREATE, 2), MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_CREATE);
    *output = NULL;
    if (!system || !config || !config->title || config->width <= 0 ||
        config->height <= 0 || config->mode < MAAGE_WINDOW_WINDOWED ||
        config->mode > MAAGE_WINDOW_NATIVE_FULLSCREEN) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_CREATE, !system ? 0 : 1), MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_CREATE);
    window = (maage_window *)calloc(1, sizeof(*window));
    if (!window) return MAAGE_STATUS_PROPAGATE(trace, MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_CREATE, 0), MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_CREATE);
    window->system = system;
    window->full_display_content = config->full_display_content;
    window->state.width = config->width;
    window->state.height = config->height;
    window->next = system->windows;
    system->windows = window;
    result = system->api->create(window, config);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
        system->windows = window->next;
        system->api->destroy(window);
        free(window);
        return MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_CREATE);
    }
    *output = window;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_CREATE, 0);
}

maage_status maage_window_destroy(maage_window *window)
{
    maage_window **link;
    if (!window) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    link = &window->system->windows;
    while (*link && *link != window) link = &(*link)->next;
    if (*link) *link = window->next;
    window->system->api->destroy(window);
    free(window);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}

maage_status maage_window_poll_events(maage_window_system *system, int wait)
{
    if (!system) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_POLL, 0);
    return system->api->poll(system, wait ? -1 : 0);
}

maage_status maage_window_wait_events(maage_window_system *system, int timeout_ms)
{
    if (!system || timeout_ms < 0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_WAIT, 0);
    return system->api->poll(system, timeout_ms);
}

maage_status maage_window_get_state(maage_window *window, maage_window_state *state)
{
    maage_status result;
    if (state) memset(state, 0, sizeof(*state));
    if (!window || !state) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, 0);
    result = window->system->api->state(window);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    *state = window->state;
    window->state.resized = 0;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, 0);
}

void maage_window_input_character(maage_window *window, unsigned int character)
{
    size_t length;
    if (!window || !window->input.focused) return;
    if (character == 127) character = 8;
    if (character != 8 && (character < 32 || character > 126)) return;
    length = strlen(window->input.text);
    if (length + 1 < sizeof(window->input.text)) {
        window->input.text[length] = (char)character;
        window->input.text[length + 1] = 0;
    }
}
void maage_window_input_key(maage_window *window, maage_input_key key, int down)
{
    if (key < 0 || key >= MAAGE_INPUT_KEY_COUNT) return;
    if (down && !window->input.keys[key]) window->input.pressed[key] = 1;
    window->input.keys[key] = down != 0;
}

void maage_window_input_focus(maage_window *window, int focused)
{
    if (!focused) {
        if (window->input.captured) maage_window_capture_mouse(window, 0);
        memset(&window->input, 0, sizeof(window->input));
    }
    window->input.focused = focused != 0;
}

maage_status maage_window_get_input(maage_window *window, maage_input_snapshot *input)
{
    maage_status result;
    if (input) memset(input, 0, sizeof(*input));
    if (!window || !input) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, 0);
    if (!window->system->api->capture) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, 0);
    result = window->system->api->state(window);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    *input = window->input;
    memset(window->input.pressed, 0, sizeof(window->input.pressed));
    window->input.primary_pressed = 0;
    window->input.secondary_pressed = 0;
    window->input.scroll_y = 0;
    window->input.text[0] = 0;
    window->input.mouse_delta_x = window->input.mouse_delta_y = 0.0;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, 0);
}

maage_status maage_window_set_cursor(maage_window *window, maage_window_cursor cursor)
{
    maage_status result;
    if (!window || (cursor != MAAGE_WINDOW_CURSOR_ARROW && cursor != MAAGE_WINDOW_CURSOR_POINTER))
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    if (window->cursor == cursor)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    if (!window->system->api->cursor)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    result = window->system->api->cursor(window, cursor);
    if (maage_status_is_success(result)) window->cursor = cursor;
    return result;
}

maage_status maage_window_capture_mouse(maage_window *window, int capture)
{
    maage_status result;
    if (!window) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
    if (!window->system->api->capture) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
    capture = capture != 0;
    if (capture && !window->input.focused) return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
    result = window->system->api->capture(window, capture);
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
        window->input.captured = capture;
        window->input.mouse_delta_x = window->input.mouse_delta_y = 0.0;
    }
    return result;
}

maage_status maage_window_acquire_surface(maage_window *window, maage_window_native *native)
{
    maage_status result;
    if (native) memset(native, 0, sizeof(*native));
    if (!window || !native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
    if (window->surface_owned) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_ACQUIRE, 0);
    result = maage_window_get_native(window, native);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    window->surface_owned = 1;
    if (window->system->api->surface) {
        result = window->system->api->surface(window, 1);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) window->surface_owned = 0;
    }
    return result;
}

maage_status maage_window_release_surface(maage_window *window)
{
    if (!window || !window->surface_owned) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_RELEASE, 0);
    window->surface_owned = 0;
    if (window->system->api->surface) return window->system->api->surface(window, 0);
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_RELEASE, 0);
}

maage_status maage_window_present_pixels(maage_window *window, const maage_pixel_buffer *buffer)
{
    size_t row;
    if (!window || !window->surface_owned || !buffer || !buffer->pixels ||
        buffer->width <= 0 || buffer->height <= 0 || (size_t)buffer->width > (size_t)-1 / 4)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0);
    row = (size_t)buffer->width * 4;
    if (buffer->stride < row || buffer->stride > (size_t)-1 / (size_t)buffer->height)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0);
    if (!window->system->api->pixels) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0);
    if (buffer->width != window->state.framebuffer_width || buffer->height != window->state.framebuffer_height)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_PRESENT, 0);
    return window->system->api->pixels(window, buffer);
}

maage_status maage_window_set_title(maage_window *window, const char *title)
{
    if (!window || !title) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    return window->system->api->title(window, title);
}
maage_status maage_window_set_icon(maage_window *window, const maage_window_icon *icon)
{
    size_t i, row;
    if (!window || !icon || (!icon->native_path && !icon->image_count) ||
        (icon->native_path && !*icon->native_path) || (icon->image_count && !icon->images))
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    for (i = 0; i < icon->image_count; ++i) {
        const maage_pixel_buffer *image = &icon->images[i];
        if (!image->pixels || image->width <= 0 || image->height <= 0 ||
            (size_t)image->width > (size_t)-1 / 4)
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
        row = (size_t)image->width * 4;
        if (image->stride < row || image->stride > (size_t)-1 / (size_t)image->height)
            return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    }
    if (!window->system->api->icon)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    return window->system->api->icon(window, icon);
}
maage_status maage_window_set_visible(maage_window *window, int visible)
{
    maage_status result;
    if (!window) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    result = window->system->api->visible(window, visible);
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS && !visible) maage_window_input_focus(window, 0);
    return result;
}
maage_status maage_window_resize(maage_window *window, int width, int height)
{
    if (!window || width <= 0 || height <= 0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_RESIZE, 0);
    if (window->state.mode >= MAAGE_WINDOW_BORDERLESS_FULLSCREEN) return MAAGE_STATUS_VALUE(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_RESIZE, 0);
    return window->system->api->resize(window, width, height);
}
maage_status maage_window_set_mode(maage_window *window, maage_window_mode mode)
{
    if (!window || mode < MAAGE_WINDOW_WINDOWED || mode > MAAGE_WINDOW_NATIVE_FULLSCREEN)
        return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_SET, 0);
    return window->system->api->mode(window, mode);
}
maage_status maage_window_get_native(maage_window *window, maage_window_native *native)
{
    if (native) memset(native, 0, sizeof(*native));
    if (!window || !native) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, 0);
    native->backend = window->system->api->kind;
    window->system->api->native(window, native);
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_QUERY, 0);
}
