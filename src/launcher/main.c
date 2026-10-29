#include "maage/log.h"
#include "maage/download.h"
#include "maage/application.h"
#include "maage/launcher_version.h"
#include "native/setup.h"
#include "ui/launcher.h"
#include <string.h>

maage_status maage_launcher_demonstration(int argc, char *const argv[]);
maage_status maage_application_main(int argc, char *const argv[]) {
    maage_assets *assets = NULL;
    maage_access *access = NULL;
    maage_launcher_ui ui;
    maage_status result, cleanup;
    maage_status_trace trace;
    int action = 0, repair = 0, manage_accounts = 0, demonstration = 0, i;
    maage_status_trace_init(&trace);
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--repair-assets")) repair = 1;
        if (!strcmp(argv[i], "--manage-accounts")) manage_accounts = 1;
        if (!strcmp(argv[i], "--renderer-demo")) demonstration = 1;
    }
    maage_log_info("maage.launcher", "MaaGE Launcher %s", MAAGE_LAUNCHER_VERSION);
    result = maage_download_initialize();
    if (maage_status_failed(result)) goto finished;
    result = maage_access_create(&access, &trace);
    if (maage_status_failed(result)) goto finished;
    result = maage_assets_create(access, &assets, &trace);
    if (maage_status_failed(result)) goto finished;
    result = maage_access_restore(access);
    if (maage_status_failed(result)) goto finished;
    for (;;) {
        result = maage_access_native_run(access, manage_accounts, &action);
        if (maage_status_failed(result) || !action) goto finished;
        if (!maage_access_allowed(access)) {
            result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_VERIFY, 0);
            goto finished;
        }
        result = maage_assets_start(assets, repair);
        if (maage_status_failed(result)) goto finished;
        result = maage_setup_native_run(assets, &action);
        if (maage_status_failed(result) || !action) goto finished;
        if (action == 1 && !maage_access_allowed(access)) action = 2;
        if (action != 2) break;
        result = maage_access_restore(access);
        if (maage_status_failed(result)) goto finished;
    }
    if (demonstration) {
        result = maage_launcher_demonstration(argc, argv);
        goto finished;
    }
    result = maage_launcher_ui_init(&ui, access, assets);
    if (maage_status_failed(result)) {
        trace = ui.trace;
        goto finished;
    }
    for (;;) {
        result = maage_launcher_native_run(&ui, &action);
        if (maage_status_failed(result) || !action) break;
        if (action == 1) {
            result = maage_access_select(access, NULL);
            if (maage_status_failed(result)) break;
            result = maage_access_native_run(access, 1, &action);
            if (maage_status_failed(result) || !action) break;
            result = maage_launcher_account_restore(&ui);
            if (maage_status_failed(result)) break;
        } else if (maage_access_allowed(access)) {
            result = maage_assets_start(assets, 1);
            if (maage_status_failed(result)) break;
            result = maage_setup_native_run(assets, &action);
            if (maage_status_failed(result) || !action) break;
            if (action == 2) ui.page = MAAGE_PAGE_SETTINGS;
        }
    }
    trace = ui.trace;
    cleanup = maage_launcher_ui_destroy(&ui);
    if (!maage_status_failed(result) && maage_status_failed(cleanup)) result = cleanup;
finished:
    cleanup = maage_assets_destroy(assets);
    if (!maage_status_failed(result) && maage_status_failed(cleanup)) result = cleanup;
    cleanup = maage_access_destroy(access);
    if (!maage_status_failed(result) && maage_status_failed(cleanup)) result = cleanup;
    cleanup = maage_download_shutdown();
    if (!maage_status_failed(result) && maage_status_failed(cleanup)) result = cleanup;
    if (maage_status_failed(result)) {
        MAAGE_STATUS_PROPAGATE(&trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE);
        maage_log_trace_status(MAAGE_LOG_ERROR, "maage.launcher", result, &trace, "Launcher session could not complete.");
    }
    return result;
}
