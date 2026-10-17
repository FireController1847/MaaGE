#include "maage/log.h"
#include "maage/window.h"
#include "maage/render.h"
#include "maage/time.h"
#include <stddef.h>
#include <math.h>

int maage_launcher_demonstration(int argc, char *const argv[])
{
    maage_window_system *system;
    maage_window *window;
    maage_window_config config;
    maage_window_state state;
    maage_window_result result;
    maage_render_desc render_desc;
    maage_renderer *renderer = NULL;
    maage_render_result rendered = MAAGE_RENDER_OK;
    double start, now, after, remaining;
    int color, timeout_ms;
    maage_render_desc_init(&render_desc);
    rendered = maage_render_desc_parse_args(argc, argv, &render_desc);
    if (rendered != MAAGE_RENDER_OK) return 1;
    result = maage_window_system_create(MAAGE_WINDOW_AUTO, &system);
    if (result != MAAGE_WINDOW_OK) {
        maage_log_error("maage.window", "%s", maage_window_result_string(result));
        return 1;
    }
    maage_window_config_init(&config);
    config.title = "MaaGE Launcher";
    config.full_display_content = 1;
    config.visible = 0;
    result = maage_window_create(system, &config, &window);
    if (result == MAAGE_WINDOW_OK) {
        render_desc.window = window;
        rendered = maage_renderer_create(&render_desc, &renderer);
        /* Keep native backing hidden during device/backend initialization. */
        if (rendered == MAAGE_RENDER_OK)
            rendered = maage_renderer_set_clear_color(renderer, 1.0f, 0.0f, 0.0f, 1.0f);
        if (rendered == MAAGE_RENDER_OK) {
            rendered = maage_renderer_prepare_frame(renderer);
            if (rendered == MAAGE_RENDER_UNSUPPORTED) rendered = MAAGE_RENDER_OK;
        }
        if (rendered == MAAGE_RENDER_OK || rendered == MAAGE_RENDER_SKIPPED)
            result = maage_window_set_visible(window, 1);
        start = maage_time_monotonic_seconds();
        if (start < 0.0) {
            maage_log_error("maage.launcher", "Monotonic clock unavailable");
            rendered = MAAGE_RENDER_PLATFORM_ERROR;
        }
        while (result == MAAGE_WINDOW_OK &&
            (rendered == MAAGE_RENDER_OK || rendered == MAAGE_RENDER_SKIPPED)) {
            result = maage_window_poll_events(system, 0);
            if (result != MAAGE_WINDOW_OK) break;
            result = maage_window_get_state(window, &state);
            if (result != MAAGE_WINDOW_OK || state.close_requested) break;
            now = maage_time_monotonic_seconds();
            if (now < start) { rendered = MAAGE_RENDER_PLATFORM_ERROR; break; }
            color = (int)fmod(now - start, 3.0);
            rendered = maage_renderer_set_clear_color(renderer, color == 0 ? 1.0f : 0.0f,
                color == 1 ? 1.0f : 0.0f, color == 2 ? 1.0f : 0.0f, 1.0f);
            if (rendered != MAAGE_RENDER_OK) break;
            rendered = maage_renderer_present(renderer);
            if (rendered != MAAGE_RENDER_OK && rendered != MAAGE_RENDER_SKIPPED) break;
            after = maage_time_monotonic_seconds();
            if (after < now) { rendered = MAAGE_RENDER_PLATFORM_ERROR; break; }
            remaining = 0.016 - (after - now);
            timeout_ms = remaining > 0.0 ? (int)(remaining * 1000.0) + 1 : 0;
            if (rendered == MAAGE_RENDER_SKIPPED) timeout_ms = 100;
            result = maage_window_wait_events(system, timeout_ms);
            if (result != MAAGE_WINDOW_OK) break;
        }
        maage_renderer_destroy(renderer);
        maage_window_destroy(window);
    }
    maage_window_system_destroy(system);
    if (result != MAAGE_WINDOW_OK) {
        maage_log_error("maage.window", "%s", maage_window_result_string(result));
        return 1;
    }
    if (rendered != MAAGE_RENDER_OK && rendered != MAAGE_RENDER_SKIPPED) {
        maage_log_error("maage.render", "%s", maage_render_result_string(rendered));
        return 1;
    }
    return 0;
}

