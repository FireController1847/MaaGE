#include "window_internal.h"
#include <stdlib.h>
#include <string.h>

void maage_window_config_init(maage_window_config *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->title = "MaaGE";
    config->width = 1000;
    config->height = 650;
    config->visible = 1;
    config->mode = MAAGE_WINDOW_WINDOWED;
}

const char *maage_window_result_string(maage_window_result result)
{
    switch (result) {
    case MAAGE_WINDOW_OK: return "success";
    case MAAGE_WINDOW_INVALID_ARGUMENT: return "invalid argument";
    case MAAGE_WINDOW_OUT_OF_MEMORY: return "out of memory";
    case MAAGE_WINDOW_UNAVAILABLE: return "native window system unavailable";
    case MAAGE_WINDOW_UNSUPPORTED: return "operation unsupported by this backend";
    case MAAGE_WINDOW_WRONG_THREAD: return "windowing requires the initialization/main thread";
    default: return "native window-system failure";
    }
}

static maage_window_result maage_window_try_backend(maage_window_system *system,
    const maage_window_backend_api *api)
{
    maage_window_result result;
    system->api = api;
    result = api->init(system);
    if (result != MAAGE_WINDOW_OK) {
        api->shutdown(system);
        system->platform = NULL;
        system->api = NULL;
    }
    return result;
}

maage_window_result maage_window_system_create(maage_window_backend backend,
    maage_window_system **output)
{
    maage_window_system *system;
    maage_window_result result;
    if (!output) return MAAGE_WINDOW_INVALID_ARGUMENT;
    *output = NULL;
    system = (maage_window_system *)calloc(1, sizeof(*system));
    if (!system) return MAAGE_WINDOW_OUT_OF_MEMORY;
    result = MAAGE_WINDOW_UNSUPPORTED;
#if defined(MAAGE_WINDOW_HAS_WIN32)
    if (backend == MAAGE_WINDOW_AUTO || backend == MAAGE_WINDOW_WIN32)
        result = maage_window_try_backend(system, &maage_window_win32_api);
#elif defined(MAAGE_WINDOW_HAS_COCOA)
    if (backend == MAAGE_WINDOW_AUTO || backend == MAAGE_WINDOW_COCOA)
        result = maage_window_try_backend(system, &maage_window_cocoa_api);
#else
#if defined(MAAGE_WINDOW_HAS_WAYLAND)
    if (backend == MAAGE_WINDOW_WAYLAND || (backend == MAAGE_WINDOW_AUTO &&
        (getenv("WAYLAND_DISPLAY") || getenv("WAYLAND_SOCKET"))))
        result = maage_window_try_backend(system, &maage_window_wayland_api);
#endif
#if defined(MAAGE_WINDOW_HAS_X11)
    if (backend == MAAGE_WINDOW_X11 || (backend == MAAGE_WINDOW_AUTO && !system->api))
        result = maage_window_try_backend(system, &maage_window_x11_api);
#endif
#endif
    if (result != MAAGE_WINDOW_OK) { free(system); return result; }
    *output = system;
    return MAAGE_WINDOW_OK;
}

void maage_window_system_destroy(maage_window_system *system)
{
    if (!system) return;
    while (system->windows) maage_window_destroy(system->windows);
    system->api->shutdown(system);
    free(system);
}

maage_window_result maage_window_create(maage_window_system *system,
    const maage_window_config *config, maage_window **output)
{
    maage_window *window;
    maage_window_result result;
    if (!output) return MAAGE_WINDOW_INVALID_ARGUMENT;
    *output = NULL;
    if (!system || !config || !config->title || config->width <= 0 ||
        config->height <= 0 || config->mode < MAAGE_WINDOW_WINDOWED ||
        config->mode > MAAGE_WINDOW_NATIVE_FULLSCREEN) return MAAGE_WINDOW_INVALID_ARGUMENT;
    window = (maage_window *)calloc(1, sizeof(*window));
    if (!window) return MAAGE_WINDOW_OUT_OF_MEMORY;
    window->system = system;
    window->full_display_content = config->full_display_content;
    window->state.width = config->width;
    window->state.height = config->height;
    window->next = system->windows;
    system->windows = window;
    result = system->api->create(window, config);
    if (result != MAAGE_WINDOW_OK) {
        system->windows = window->next;
        system->api->destroy(window);
        free(window);
        return result;
    }
    *output = window;
    return MAAGE_WINDOW_OK;
}

void maage_window_destroy(maage_window *window)
{
    maage_window **link;
    if (!window) return;
    link = &window->system->windows;
    while (*link && *link != window) link = &(*link)->next;
    if (*link) *link = window->next;
    window->system->api->destroy(window);
    free(window);
}

maage_window_result maage_window_poll_events(maage_window_system *system, int wait)
{
    if (!system) return MAAGE_WINDOW_INVALID_ARGUMENT;
    return system->api->poll(system, wait ? -1 : 0);
}

maage_window_result maage_window_wait_events(maage_window_system *system, int timeout_ms)
{
    if (!system || timeout_ms < 0) return MAAGE_WINDOW_INVALID_ARGUMENT;
    return system->api->poll(system, timeout_ms);
}

maage_window_result maage_window_get_state(maage_window *window, maage_window_state *state)
{
    maage_window_result result;
    if (!window || !state) return MAAGE_WINDOW_INVALID_ARGUMENT;
    result = window->system->api->state(window);
    if (result != MAAGE_WINDOW_OK) return result;
    *state = window->state;
    window->state.resized = 0;
    return MAAGE_WINDOW_OK;
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

maage_window_result maage_window_get_input(maage_window *window, maage_input_snapshot *input)
{
    maage_window_result result;
    if (!window || !input) return MAAGE_WINDOW_INVALID_ARGUMENT;
    memset(input, 0, sizeof(*input));
    if (!window->system->api->capture) return MAAGE_WINDOW_UNSUPPORTED;
    result = window->system->api->state(window);
    if (result != MAAGE_WINDOW_OK) return result;
    *input = window->input;
    memset(window->input.pressed, 0, sizeof(window->input.pressed));
    window->input.primary_pressed = 0;
    window->input.mouse_delta_x = window->input.mouse_delta_y = 0.0;
    return MAAGE_WINDOW_OK;
}

maage_window_result maage_window_capture_mouse(maage_window *window, int capture)
{
    maage_window_result result;
    if (!window) return MAAGE_WINDOW_INVALID_ARGUMENT;
    if (!window->system->api->capture) return MAAGE_WINDOW_UNSUPPORTED;
    capture = capture != 0;
    if (capture && !window->input.focused) return MAAGE_WINDOW_UNAVAILABLE;
    result = window->system->api->capture(window, capture);
    if (result == MAAGE_WINDOW_OK) {
        window->input.captured = capture;
        window->input.mouse_delta_x = window->input.mouse_delta_y = 0.0;
    }
    return result;
}

maage_window_result maage_window_acquire_surface(maage_window *window, maage_window_native *native)
{
    maage_window_result result;
    if (!window || !native) return MAAGE_WINDOW_INVALID_ARGUMENT;
    if (window->surface_owned) return MAAGE_WINDOW_UNSUPPORTED;
    result = maage_window_get_native(window, native);
    if (result != MAAGE_WINDOW_OK) return result;
    window->surface_owned = 1;
    if (window->system->api->surface) {
        result = window->system->api->surface(window, 1);
        if (result != MAAGE_WINDOW_OK) window->surface_owned = 0;
    }
    return result;
}

maage_window_result maage_window_release_surface(maage_window *window)
{
    if (!window || !window->surface_owned) return MAAGE_WINDOW_INVALID_ARGUMENT;
    window->surface_owned = 0;
    if (window->system->api->surface) return window->system->api->surface(window, 0);
    return MAAGE_WINDOW_OK;
}

maage_window_result maage_window_present_pixels(maage_window *window, const maage_pixel_buffer *buffer)
{
    size_t row;
    if (!window || !window->surface_owned || !buffer || !buffer->pixels ||
        buffer->width <= 0 || buffer->height <= 0 || (size_t)buffer->width > (size_t)-1 / 4)
        return MAAGE_WINDOW_INVALID_ARGUMENT;
    row = (size_t)buffer->width * 4;
    if (buffer->stride < row || buffer->stride > (size_t)-1 / (size_t)buffer->height)
        return MAAGE_WINDOW_INVALID_ARGUMENT;
    if (!window->system->api->pixels) return MAAGE_WINDOW_UNSUPPORTED;
    if (buffer->width != window->state.framebuffer_width || buffer->height != window->state.framebuffer_height)
        return MAAGE_WINDOW_UNAVAILABLE;
    return window->system->api->pixels(window, buffer);
}

maage_window_result maage_window_set_title(maage_window *window, const char *title)
{
    if (!window || !title) return MAAGE_WINDOW_INVALID_ARGUMENT;
    return window->system->api->title(window, title);
}
maage_window_result maage_window_set_visible(maage_window *window, int visible)
{
    maage_window_result result;
    if (!window) return MAAGE_WINDOW_INVALID_ARGUMENT;
    result = window->system->api->visible(window, visible);
    if (result == MAAGE_WINDOW_OK && !visible) maage_window_input_focus(window, 0);
    return result;
}
maage_window_result maage_window_resize(maage_window *window, int width, int height)
{
    if (!window || width <= 0 || height <= 0) return MAAGE_WINDOW_INVALID_ARGUMENT;
    if (window->state.mode >= MAAGE_WINDOW_BORDERLESS_FULLSCREEN) return MAAGE_WINDOW_UNSUPPORTED;
    return window->system->api->resize(window, width, height);
}
maage_window_result maage_window_set_mode(maage_window *window, maage_window_mode mode)
{
    if (!window || mode < MAAGE_WINDOW_WINDOWED || mode > MAAGE_WINDOW_NATIVE_FULLSCREEN)
        return MAAGE_WINDOW_INVALID_ARGUMENT;
    return window->system->api->mode(window, mode);
}
maage_window_result maage_window_get_native(maage_window *window, maage_window_native *native)
{
    if (!window || !native) return MAAGE_WINDOW_INVALID_ARGUMENT;
    memset(native, 0, sizeof(*native));
    native->backend = window->system->api->kind;
    window->system->api->native(window, native);
    return MAAGE_WINDOW_OK;
}
