#include "maage/log.h"
#include "maage/window.h"
#include "maage/render.h"
#include "maage/time.h"
#include <stddef.h>
#include <math.h>

maage_status maage_launcher_demonstration(int argc, char *const argv[])
{
    maage_window_system *system = NULL;
    maage_window *window = NULL;
    maage_window_config config;
    maage_window_state state;
    maage_render_desc render_desc;
    maage_renderer *renderer = NULL;
    maage_status result, cleanup;
    maage_status_trace trace;
    double start, now, after, remaining;
    int color, timeout_ms;
    maage_status_trace_init(&trace);
    result = maage_render_desc_init(&render_desc);
    if (maage_status_failed(result)) goto done;
    result = maage_render_desc_parse_args(argc, argv, &render_desc);
    if (maage_status_failed(result)) goto done;
    result = maage_window_system_create(MAAGE_WINDOW_AUTO, &system, &trace);
    if (maage_status_failed(result)) goto done;
    if (maage_status_severity(result) == MAAGE_SEVERITY_WARNING) maage_log_trace_status(MAAGE_LOG_WARNING, "maage.launcher.demo", result, &trace, "Window backend selected with recovery.");
    maage_window_config_init(&config);
    config.title = "MaaGE Launcher"; config.full_display_content = 1; config.visible = 0;
    result = maage_window_create(system, &config, &window, &trace);
    if (maage_status_failed(result)) goto done;
    render_desc.window = window;
    result = maage_renderer_create(&render_desc, &renderer, &trace);
    if (maage_status_failed(result)) goto done;
    if (maage_status_severity(result) == MAAGE_SEVERITY_WARNING) maage_log_trace_status(MAAGE_LOG_WARNING, "maage.launcher.demo", result, &trace, "Renderer backend selected with recovery.");
    result = maage_renderer_set_clear_color(renderer, 1, 0, 0, 1);
    if (maage_status_failed(result)) goto done;
    result = maage_renderer_prepare_frame(renderer, &trace);
    if (maage_status_code(result) == MAAGE_STATUS_NOT_IMPLEMENTED)
        result = maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0);
    if (maage_status_failed(result)) goto done;
    result = maage_window_set_visible(window, 1);
    if (maage_status_failed(result)) goto done;
    start = maage_time_monotonic_seconds();
    if (start < 0) goto clock_failed;
    for (;;) {
        result = maage_window_poll_events(system, 0);
        if (maage_status_failed(result)) break;
        result = maage_window_get_state(window, &state);
        if (maage_status_failed(result) || state.close_requested) break;
        now = maage_time_monotonic_seconds();
        if (now < start) goto clock_failed;
        color = (int)fmod(now - start, 3.0);
        result = maage_renderer_set_clear_color(renderer, color == 0 ? 1.0f : 0.0f,
            color == 1 ? 1.0f : 0.0f, color == 2 ? 1.0f : 0.0f, 1);
        if (maage_status_failed(result)) break;
        result = maage_renderer_present(renderer, &trace);
        if (maage_status_failed(result)) break;
        after = maage_time_monotonic_seconds();
        if (after < now) goto clock_failed;
        remaining = 0.016 - (after - now);
        timeout_ms = remaining > 0 ? (int)(remaining * 1000.0) + 1 : 0;
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) timeout_ms = 100;
        result = maage_window_wait_events(system, timeout_ms);
        if (maage_status_failed(result)) break;
    }
    goto done;
clock_failed:
    result = maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_QUERY, 0);
done:
    if (maage_status_failed(result)) {
        MAAGE_STATUS_TRACE(&trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_START);
        maage_log_trace_status(MAAGE_LOG_ERROR, "maage.launcher.demo", result, &trace,
            "Renderer demonstration could not continue.");
    }
    cleanup = maage_renderer_destroy(renderer, &trace);
    if (!maage_status_failed(result) && maage_status_failed(cleanup)) result = cleanup;
    cleanup = maage_window_destroy(window);
    if (!maage_status_failed(result) && maage_status_failed(cleanup)) result = cleanup;
    cleanup = maage_window_system_destroy(system);
    if (!maage_status_failed(result) && maage_status_failed(cleanup)) result = cleanup;
    return result;
}
