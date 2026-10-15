#include <stdio.h>
#include "maage/window.h"

int main(void)
{
    maage_window_system *system;
    maage_window *window;
    maage_window_config config;
    maage_window_state state;
    maage_window_result result;
    result = maage_window_system_create(MAAGE_WINDOW_AUTO, &system);
    if (result != MAAGE_WINDOW_OK) {
        fprintf(stderr, "MaaGE Launcher: %s\n", maage_window_result_string(result));
        return 1;
    }
    maage_window_config_init(&config);
    config.title = "MaaGE Launcher";
    config.full_display_content = 1;
    result = maage_window_create(system, &config, &window);
    if (result == MAAGE_WINDOW_OK) {
        for (;;) {
            result = maage_window_get_state(window, &state);
            if (result != MAAGE_WINDOW_OK || state.close_requested) break;
            result = maage_window_poll_events(system, 1);
            if (result != MAAGE_WINDOW_OK) break;
        }
        maage_window_destroy(window);
    }
    maage_window_system_destroy(system);
    if (result != MAAGE_WINDOW_OK) {
        fprintf(stderr, "MaaGE Launcher: %s\n", maage_window_result_string(result));
        return 1;
    }
    return 0;
}
