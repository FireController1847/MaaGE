#include "maage/log.h"
#include "maage/window.h"
#include "maage/launcher_version.h"
#include "maage/application.h"
#include "ui.h"
#include "auth.h"
#ifdef MAAGE_LAUNCHER_NATIVE_LINKS
#include "links.h"
#endif
#include <stddef.h>

int maage_application_main(int argc, char *const argv[])
{
    maage_launcher_state application;
    maage_window_system *system;
    maage_window *window;
    maage_window_config config;
    maage_window_state state;
    maage_input_state input;
    maage_window_result result;
    maage_render_desc desc;
    maage_renderer *renderer = NULL;
    maage_launcher_ui *ui = NULL;
    maage_auth *auth = NULL;
    maage_render_result rendered;
    maage_log_info("maage.launcher", "MaaGE Launcher %s", MAAGE_LAUNCHER_VERSION);
    maage_render_desc_init(&desc);
    rendered = maage_render_desc_parse_args(argc, argv, &desc);
    if (rendered != MAAGE_RENDER_OK) return 1;
    maage_launcher_state_init(&application);
    rendered = maage_launcher_ui_create(&application, &ui);
    if (rendered != MAAGE_RENDER_OK) {
        maage_log_error("maage.launcher", "UI canvas unavailable: %s (native canvas/input currently require macOS)", maage_render_result_string(rendered));
        return 1;
    }
#ifdef MAAGE_LAUNCHER_NATIVE_LINKS
    if (!maage_launcher_links_init()) maage_log_warn("maage.launcher", "Native URL handling could not be initialized.");
#endif
    result = maage_window_system_create(MAAGE_WINDOW_AUTO, &system);
    if (result != MAAGE_WINDOW_OK) {
#ifdef MAAGE_LAUNCHER_NATIVE_LINKS
        maage_launcher_links_shutdown();
#endif
        maage_launcher_ui_destroy(ui);
        maage_log_error("maage.window", "%s", maage_window_result_string(result));
        return 1;
    }
    maage_window_config_init(&config);
    config.title = "MaaGE Launcher";
    config.width = 1040; config.height = 680;
    result = maage_window_create(system, &config, &window);
    if (result == MAAGE_WINDOW_OK) {
        desc.window = window;
        desc.required_features |= MAAGE_RENDER_CAP_PIXEL_PRESENT;
        rendered = maage_renderer_create(&desc, &renderer);
        if (rendered == MAAGE_RENDER_OK && maage_auth_create(&application, &auth) != MAAGE_LAUNCHER_OK) {
            maage_log_error("maage.launcher.auth", "Could not initialize the account worker or HTTPS library.");
            rendered = MAAGE_RENDER_PLATFORM_ERROR;
        }
        if (rendered == MAAGE_RENDER_OK) result = maage_input_poll(window, &input);
        while (result == MAAGE_WINDOW_OK && (rendered == MAAGE_RENDER_OK || rendered == MAAGE_RENDER_SKIPPED)) {
            result = maage_window_poll_events(system, 0);
            if (result != MAAGE_WINDOW_OK) break;
            result = maage_window_get_state(window, &state);
            if (result != MAAGE_WINDOW_OK || state.close_requested) break;
            result = maage_input_poll(window, &input);
            if (result != MAAGE_WINDOW_OK) break;
            maage_auth_poll(auth);
#ifdef MAAGE_LAUNCHER_NATIVE_LINKS
            if (maage_launcher_links_poll(window) && !application.accounts.sign_in.active) {
                application.navigation.page = MAAGE_PAGE_SETTINGS;
                application.navigation.settings = MAAGE_SETTINGS_ACCOUNTS;
            }
#endif
            rendered = maage_launcher_ui_frame(ui, renderer, &state, &input);
            if (rendered != MAAGE_RENDER_OK && rendered != MAAGE_RENDER_SKIPPED) break;
            result = maage_window_wait_events(system, rendered == MAAGE_RENDER_SKIPPED ? 100 : 16);
        }
        maage_auth_destroy(auth);
        maage_renderer_destroy(renderer);
        maage_window_destroy(window);
    }
#ifdef MAAGE_LAUNCHER_NATIVE_LINKS
    maage_launcher_links_shutdown();
#endif
    maage_window_system_destroy(system);
    maage_launcher_ui_destroy(ui);
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
