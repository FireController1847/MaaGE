#include "../window/window_internal.h"
#include <string.h>
maage_window_result maage_input_poll(maage_window *window, maage_input_state *state)
{
    if (!window || !state) return MAAGE_WINDOW_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    if (window->system->api->kind != MAAGE_WINDOW_COCOA) return MAAGE_WINDOW_UNSUPPORTED;
    window->input_enabled = 1;
    *state = window->input;
    window->input.pressed = window->input.released = window->input.cancelled = 0;
    window->input.key = MAAGE_KEY_NONE;
    window->input.scroll = 0;
    window->input.text[0] = '\0';
    return MAAGE_WINDOW_OK;
}
