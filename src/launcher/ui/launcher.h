#ifndef MAAGE_LAUNCHER_UI_H
#define MAAGE_LAUNCHER_UI_H
#include "../assets.h"
#include "maage/storage.h"
#include "maage/instance.h"
#include "../clients/registry.h"
#include "../clients/job.h"
#define MAAGE_PROJECT_URL "https://github.com/FireController1847/MaaGE"
typedef enum maage_launcher_page {
    MAAGE_PAGE_PLAY,
    MAAGE_PAGE_INSTALLATIONS,
    MAAGE_PAGE_ACCOUNTS,
    MAAGE_PAGE_SETTINGS,
    MAAGE_PAGE_ABOUT
} maage_launcher_page;
typedef struct maage_installation {
    unsigned id;
    char uuid[37], name[96], client[65];
    char arguments[MAAGE_INSTANCE_ARGUMENTS_CAPACITY];
} maage_installation;
typedef struct maage_launcher_startup maage_launcher_startup;
typedef struct maage_launcher_ui {
    maage_access *access;
    maage_assets *assets;
    maage_launcher_page page;
    maage_installation *installations;
    unsigned count, capacity, selected, default_id, next_id;
    int appearance; /* 0 system, 1 light, 2 dark */
    char account[33], root[MAAGE_PATH_CAPACITY], resources[MAAGE_PATH_CAPACITY];
    char error[256], account_status[512];
    char resource_status[768], launch_status[512], active_summary[256], authentication_status[768];
    maage_status result;
    maage_status_trace trace;
    maage_access_snapshot accounts;
    maage_setup_progress progress;
    const char *readiness;
    maage_client_registry *clients;
    maage_client_job *client_job;
    maage_process *client_process;
    int can_play;
    maage_launcher_startup *startup;
    int catalog_ready, catalog_finished, services_ready, account_bound, account_explicit, resources_pending, play_timed, presented, launch_pending;
    char pending_client[65], pending_instance[37], pending_account[33];
    maage_assets_mode resource_mode;
    double started_at;
    char running_client[65], running_instance[37];
    maage_client_candidate client_candidate;
    char replacing_client[65];
    unsigned client_revision;
    int client_operation, client_confirmation, clients_valid;
    char client_status[512];
} maage_launcher_ui;
maage_status maage_launcher_ui_init(maage_launcher_ui *ui, maage_access *access, maage_assets *assets);
/* Native adapters call after first presentation. Only this schedules startup I/O. */
maage_status maage_launcher_ui_presented(maage_launcher_ui *ui);
maage_status maage_launcher_ui_refresh(maage_launcher_ui *ui);
maage_launcher_page maage_launcher_attention_page(const maage_launcher_ui *ui);
maage_status maage_launcher_ui_save(maage_launcher_ui *ui);
maage_status maage_launcher_installation_write(maage_launcher_ui *ui, int index, const char *name, const char *client, const char *arguments);
maage_status maage_launcher_installation_remove(maage_launcher_ui *ui, unsigned index);
maage_status maage_launcher_installation_select(maage_launcher_ui *ui, unsigned index);
maage_status maage_launcher_installation_default(maage_launcher_ui *ui, unsigned index);
/* Copies the physical instance directory after opening its manifest read-only. */
maage_status maage_launcher_installation_directory(maage_launcher_ui *ui, unsigned index, char path[MAAGE_PATH_CAPACITY]);
maage_status maage_launcher_ui_destroy(maage_launcher_ui *ui);
const char *maage_launcher_installation_client(maage_launcher_ui *ui, unsigned index);
maage_status maage_launcher_client_play(maage_launcher_ui *ui);
maage_status maage_launcher_client_remove(maage_launcher_ui *ui, unsigned index);
#if MAAGE_LAUNCHER_DEVELOPMENT
maage_status maage_launcher_client_probe(maage_launcher_ui *ui, const char *path);
maage_status maage_launcher_client_import(maage_launcher_ui *ui);
maage_status maage_launcher_client_replace(maage_launcher_ui *ui, unsigned index, const char *path);
maage_status maage_launcher_client_cancel_import(maage_launcher_ui *ui);
#endif
maage_status maage_launcher_account_select(maage_launcher_ui *ui, const char *uuid);
maage_status maage_launcher_account_restore(maage_launcher_ui *ui);
/* Queues one resource operation, waiting for actual account authorization. */
maage_status maage_launcher_resources(maage_launcher_ui *ui, maage_assets_mode mode);
maage_status maage_launcher_resources_cancel(maage_launcher_ui *ui);
/* Main-thread native shell. Closing cancels services; the caller joins them. */
maage_status maage_launcher_native_run(maage_launcher_ui *state);
#endif
