#include "pacing.h"
#include "maage/time.h"
maage_status maage_client_pacing_wait(maage_client_pacing *pacing,
                                      maage_window_system *system, maage_window *window, int fps,
                                      double frame_start, int *resized) {
    double interval, now, remaining;
    int milliseconds;
    maage_window_state state;
    maage_status result;
    if (!pacing || !system || !window || fps < 0)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_WAIT, 0);
    if (!fps) {
        pacing->fps = 0;
        pacing->deadline = 0;
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_WAIT, 0);
    }
    interval = 1.0 / fps;
    if (fps != pacing->fps || pacing->deadline <= 0 || frame_start - pacing->deadline > interval)
        pacing->deadline = frame_start + interval;
    else
        pacing->deadline += interval;
    pacing->fps = fps;
    for (;;) {
        now = maage_time_monotonic_seconds();
        if (now < 0) return maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED,
                                              MAAGE_CONTEXT_CORE, MAAGE_OPERATION_QUERY, 0);
        remaining = pacing->deadline - now;
        if (remaining <= 0) break;
        milliseconds = (int) (remaining * 1000);
        if (milliseconds > 100) milliseconds = 100;
        /* Only the final sub-ms remainder spins; never round it up to 1 ms. */
        if (milliseconds > 0) {
            result = maage_window_wait_events(system, milliseconds);
            if (maage_status_failed(result)) return result;
            result = maage_window_get_state(window, &state);
            if (maage_status_failed(result)) return result;
            if (resized) *resized = *resized || state.resized;
            if (state.close_requested) break;
        }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_WAIT, 0);
}
