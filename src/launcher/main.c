#include "maage/log.h"
#include "maage/download.h"
#include "maage/application.h"
#include "maage/launcher_version.h"
#include "native/setup.h"
#include "ui/launcher.h"
#include "access/oauth.h"
#include <string.h>

int maage_launcher_demonstration(int argc, char *const argv[]);
int maage_application_main(int argc, char *const argv[])
{
    maage_assets *assets;
    maage_access *access;
    maage_launcher_ui ui;
    int result, repair = 0, manage_accounts = 0, demonstration = 0, i;
    for (i = 1; i < argc; ++i) if (!strncmp(argv[i], "maage:", 6)) {
        /* Protocol activation forwards to the existing pending login and exits.
         * A cold/stale callback cannot create a new authenticated session. */
        maage_oauth_forward_url(argv[i]); return 0;
    }
    maage_auth_platform_start();
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--repair-assets")) repair = 1;
        if (!strcmp(argv[i], "--manage-accounts")) manage_accounts = 1;
        if (!strcmp(argv[i], "--renderer-demo")) demonstration = 1;
    }
    maage_log_info("maage.launcher", "MaaGE Launcher %s", MAAGE_LAUNCHER_VERSION);
    if (!maage_download_initialize()) {
        maage_log_error("maage.launcher.assets", "Cannot initialize HTTPS networking."); return 1;
    }
    access = maage_access_create();
    if (!access) { maage_download_shutdown(); return 1; }
    assets = maage_assets_create(access);
    if (!assets) { maage_access_destroy(access); maage_download_shutdown(); return 1; }
    maage_access_restore(access);
    for (;;) {
        result = maage_access_native_run(access, manage_accounts);
        if (result <= 0) break;
        if (!maage_access_allowed(access)) { result = 0; break; }
        maage_assets_start(assets, repair);
        result = maage_setup_native_run(assets);
        if (result == 1 && !maage_access_allowed(access)) result = 2;
        if (result != 2) break;
        maage_access_restore(access);
    }
    if (result == 1 && !demonstration) {
        if (!maage_launcher_ui_init(&ui, access, assets)) result = -1;
        else for (;;) {
            result = maage_launcher_native_run(&ui);
            if (result <= 0) break;
            if (result == 1) {
                /* The established account workflow accepts any verified account.
                 * Restore the explicit main-window selection when it returns. */
                maage_access_select(access, NULL);
                result = maage_access_native_run(access, 1);
                if (result <= 0) break;
                maage_launcher_account_restore(&ui);
            } else if (maage_access_allowed(access)) {
                maage_assets_start(assets, 1);
                result = maage_setup_native_run(assets);
                if (result == 0 || result < 0) break;
                /* Expiry returns to account controls, never a fake ready state. */
                if (result == 2) ui.page = MAAGE_PAGE_SETTINGS;
            }
        }
    }
    maage_assets_destroy(assets); maage_access_destroy(access); maage_download_shutdown(); maage_auth_platform_stop();
    if (result <= 0) return result < 0 ? 1 : 0;
    return demonstration ? maage_launcher_demonstration(argc, argv) : 0;
}
