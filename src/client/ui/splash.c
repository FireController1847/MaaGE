#include "splash.h"
#include "maage/image.h"
#include "maage/minecraft_assets.h"
#include "maage/storage.h"
#include "maage/log.h"
#include "maage/time.h"
#include <string.h>

static maage_status maage_client_splash_draw(maage_client_splash *splash,
    const maage_window_state *state, int preparing, maage_status_trace *trace)
{
    static const unsigned int indices[6] = {0, 1, 2, 0, 2, 3};
    static const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    maage_render_ui_vertex vertices[4];
    maage_render_ui_data batch;
    int scale, vertical, i;
    float width, height, left, top, right, bottom, x, y;
    if (state->framebuffer_width <= 0 || state->framebuffer_height <= 0 ||
        state->width <= 0 || state->height <= 0)
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0);
    scale = state->framebuffer_width / 320;
    vertical = state->framebuffer_height / 240;
    if (scale > vertical) scale = vertical;
    if (scale < 1) scale = 1;
    if (splash->gui_scale && scale > splash->gui_scale) scale = splash->gui_scale;
    /* ScaledResolution rounds up. Preserve the full 256px image; small
     * viewports clip it, matching Minecraft.loadScreen. */
    width = (float)(state->framebuffer_width / scale + (state->framebuffer_width % scale != 0));
    height = (float)(state->framebuffer_height / scale + (state->framebuffer_height % scale != 0));
    left = (float)(state->safe_area.left * state->framebuffer_width / state->width / scale);
    top = (float)(state->safe_area.top * state->framebuffer_height / state->height / scale);
    right = (float)(state->safe_area.right * state->framebuffer_width / state->width / scale);
    bottom = (float)(state->safe_area.bottom * state->framebuffer_height / state->height / scale);
    x = (float)(int)(left + (width - left - right - 256) * 0.5f);
    y = (float)(int)(top + (height - top - bottom - 256) * 0.5f);
    memset(vertices, 0, sizeof(vertices)); memset(&batch, 0, sizeof(batch));
    for (i = 0; i < 4; ++i) {
        vertices[i].uv[0] = i == 1 || i == 2 ? 1.0f : 0.0f;
        vertices[i].uv[1] = i >= 2 ? 1.0f : 0.0f;
        vertices[i].position[0] = x + vertices[i].uv[0] * 256;
        vertices[i].position[1] = y + vertices[i].uv[1] * 256;
        vertices[i].color[0] = vertices[i].color[1] =
            vertices[i].color[2] = vertices[i].color[3] = 1;
    }
    batch.vertices = vertices; batch.vertex_count = 4;
    batch.indices = indices; batch.index_count = 6;
    batch.texture = splash->texture; batch.logical_width = width; batch.logical_height = height;
    return preparing ? maage_renderer_prepare_ui_frame(splash->renderer, &batch, trace) :
        maage_renderer_draw_frame(splash->renderer, NULL, 0, identity, &batch, trace);
}

maage_status maage_client_splash_update(void *context, maage_status_trace *trace)
{
    maage_client_splash *splash = (maage_client_splash *)context;
    maage_window_state state;
    maage_status result;
    double now;
    result = maage_window_poll_events(splash->system, 0);
    if (!maage_status_failed(result)) result = maage_window_get_state(splash->window, &state);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_UPDATE);
    if (state.close_requested) {
        splash->close_requested = 1;
        return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE, 0);
    }
    now = maage_time_monotonic_seconds();
    if (now < 0) return maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_QUERY, 0);
    /* A static logo needs no redraw for every PNG decode. Keep pumping events
     * at every checkpoint, refreshing on resize and at most every 50 ms. */
    if (!state.resized && splash->last_frame > 0 && now - splash->last_frame < 0.05)
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0);
    splash->last_frame = now;
    return maage_client_splash_draw(splash, &state, 0, trace);
}

maage_status maage_client_splash_begin(maage_client_splash *splash,
    maage_window_system *system, maage_window *window, maage_renderer *renderer,
    const char *directory, int gui_scale, maage_status_trace *trace)
{
    maage_image logo;
    maage_pixel_buffer pixels;
    maage_window_state state;
    maage_status result;
    maage_status_trace resource_trace;
    char path[MAAGE_PATH_CAPACITY];
    unsigned char white[4] = {255, 255, 255, 255};
    memset(splash, 0, sizeof(*splash)); memset(&logo, 0, sizeof(logo));
    splash->system = system; splash->window = window;
    splash->renderer = renderer; splash->gui_scale = gui_scale;
    maage_status_trace_init(&resource_trace);
    result = maage_minecraft_asset_path(directory, "minecraft/textures/gui/title/mojang.png",
        path, sizeof(path), &resource_trace);
    if (!maage_status_failed(result)) result = maage_image_load_png(path, &logo, &resource_trace);
    if (!maage_status_failed(result) && (logo.width != 256 || logo.height != 256))
        result = MAAGE_STATUS_TRACE(&resource_trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT,
            MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD_TEXTURE, 0), MAAGE_CONTEXT_GUI, MAAGE_OPERATION_INITIALIZE);
    splash->resource_status = result;
    if (maage_status_failed(result)) {
        maage_image_destroy(&logo);
        pixels.width = pixels.height = 1; pixels.stride = 4; pixels.pixels = white;
    } else {
        pixels.width = logo.width; pixels.height = logo.height;
        pixels.stride = logo.stride; pixels.pixels = logo.pixels;
    }
    result = maage_renderer_create_texture(renderer, &pixels, &splash->texture, trace);
    maage_image_destroy(&logo);
    if (maage_status_failed(splash->resource_status)) {
        maage_status_trace_recover(&resource_trace, result, MAAGE_CONTEXT_GUI,
            MAAGE_OPERATION_INITIALIZE, __FILE__, __LINE__);
        maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.ui", splash->resource_status,
            &resource_trace, "Original Mojang splash unavailable; using a plain white startup screen. Repair resources with the Launcher.");
    }
    if (maage_status_failed(result)) return result;
    result = maage_renderer_set_clear_color(renderer, 1, 1, 1, 1);
    if (!maage_status_failed(result)) result = maage_window_poll_events(system, 0);
    if (!maage_status_failed(result)) result = maage_window_get_state(window, &state);
    if (!maage_status_failed(result)) result = maage_client_splash_draw(splash, &state, 1, trace);
    if (!maage_status_failed(result)) result = maage_window_set_visible(window, 1);
    if (!maage_status_failed(result)) result = maage_client_splash_update(splash, trace);
    return result;
}

maage_status maage_client_splash_destroy(maage_client_splash *splash)
{
    maage_status result = maage_render_texture_destroy(splash->texture);
    splash->texture = NULL;
    return result;
}
