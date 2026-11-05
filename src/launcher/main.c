#include "maage/log.h"
#include "maage/download.h"
#include "maage/application.h"
#include "maage/launcher_version.h"
#include "maage/time.h"
#include "ui/launcher.h"
#include <string.h>

maage_status maage_launcher_demonstration(int argc, char *const argv[]);
maage_status maage_application_main(int argc, char *const argv[]) {
    maage_assets *assets = NULL;
    maage_access *access = NULL;
    maage_launcher_ui ui;
    maage_status result, cleanup;
    maage_status_trace trace;
    double started = maage_time_monotonic_seconds();
    int repair = 0, manage_accounts = 0, i;
    memset(&ui, 0, sizeof(ui));
    maage_status_trace_init(&trace);
    maage_log_info("maage.launcher", "MaaGE Launcher %s", MAAGE_LAUNCHER_VERSION);
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--repair-assets")) repair = 1;
        if (!strcmp(argv[i], "--manage-accounts")) manage_accounts = 1;
        /* This explicit graphics diagnostic uses no account or Minecraft data. */
        if (!strcmp(argv[i], "--renderer-demo")) return maage_launcher_demonstration(argc, argv);
    }
    result = maage_access_create(&access, &trace);
    if (maage_status_failed(result)) goto finished;
    result = maage_assets_create(access, &assets, &trace);
    if (maage_status_failed(result)) goto finished;
    result = maage_launcher_ui_init(&ui, access, assets);
    if (maage_status_failed(result)) {
        trace = ui.trace;
        goto finished;
    }
    ui.started_at = started;
    if (manage_accounts) ui.page = MAAGE_PAGE_ACCOUNTS;
    if (repair) {
        ui.resource_mode = MAAGE_ASSETS_REPAIR;
        ui.page = MAAGE_PAGE_SETTINGS;
    }
    MAAGE_LOG_DEBUG_EVENT("maage.launcher.startup", maage_log_debug("maage.launcher.startup", "Launcher core initialization: %.3f s; catalog, authentication and resource I/O deferred until presentation.", maage_time_monotonic_seconds() - started));
    result = maage_launcher_native_run(&ui);
    trace = ui.trace;
finished:
    maage_assets_cancel(assets);
    maage_access_cancel(access);
    cleanup = maage_launcher_ui_destroy(&ui);
    if (!maage_status_failed(result) && maage_status_failed(cleanup)) {
        result = cleanup;
        trace = ui.trace;
    }
    /* Retain services if a failed join leaves any worker borrowing them. */
    if (ui.startup || ui.client_job) goto report;
    cleanup = maage_assets_destroy(assets);
    if (!maage_status_failed(result) && maage_status_failed(cleanup)) result = cleanup;
    if (maage_status_failed(cleanup)) goto report;
    cleanup = maage_access_destroy(access);
    if (!maage_status_failed(result) && maage_status_failed(cleanup)) result = cleanup;
    if (maage_status_failed(cleanup)) goto report;
    if (ui.services_ready) {
        cleanup = maage_download_shutdown();
        if (!maage_status_failed(result) && maage_status_failed(cleanup)) result = cleanup;
    }
report:
    if (maage_status_failed(result)) {
        MAAGE_STATUS_PROPAGATE(&trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE);
        maage_log_trace_status(MAAGE_LOG_ERROR, "maage.launcher", result, &trace, "Launcher session could not complete.");
    }
    return result;
}
