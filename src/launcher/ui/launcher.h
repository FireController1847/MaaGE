#ifndef MAAGE_LAUNCHER_UI_H
#define MAAGE_LAUNCHER_UI_H
#include "../assets.h"
#include "maage/storage.h"
#define MAAGE_INSTALLATIONS 32
#define MAAGE_PROJECT_URL "https://github.com/FireController1847/MaaGE"
typedef enum maage_launcher_page {
    MAAGE_PAGE_PLAY, MAAGE_PAGE_NEWS, MAAGE_PAGE_INSTALLATIONS, MAAGE_PAGE_SETTINGS
} maage_launcher_page;
typedef struct maage_installation {
    unsigned id;
    char name[96];
} maage_installation;
typedef struct maage_launcher_ui {
    maage_access *access;
    maage_assets *assets;
    maage_launcher_page page;
    maage_installation installations[MAAGE_INSTALLATIONS];
    unsigned count, selected, default_id, next_id;
    int appearance; /* 0 system, 1 light, 2 dark */
    char account[33], root[MAAGE_PATH_CAPACITY], resources[MAAGE_PATH_CAPACITY];
    char error[256], account_status[512];
    maage_access_snapshot accounts;
    maage_setup_progress progress;
    const char *readiness;
} maage_launcher_ui;
int maage_launcher_ui_init(maage_launcher_ui *ui, maage_access *access, maage_assets *assets);
void maage_launcher_ui_refresh(maage_launcher_ui *ui);
int maage_launcher_ui_save(maage_launcher_ui *ui);
int maage_launcher_installation_write(maage_launcher_ui *ui, int index, const char *name);
int maage_launcher_installation_remove(maage_launcher_ui *ui, unsigned index);
int maage_launcher_installation_select(maage_launcher_ui *ui, unsigned index);
int maage_launcher_installation_default(maage_launcher_ui *ui, unsigned index);
int maage_launcher_account_select(maage_launcher_ui *ui, const char *uuid);
void maage_launcher_account_restore(maage_launcher_ui *ui);
/* Main-thread native shell: 0 close, 1 manage accounts, 2 repair assets, -1 error.
 * Services outlive the shell. Returning to it preserves navigation in ui. */
int maage_launcher_native_run(maage_launcher_ui *ui);
#endif
