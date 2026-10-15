#ifndef MAAGE_LAUNCHER_STATE_H
#define MAAGE_LAUNCHER_STATE_H
#include "settings.h"
#include "maage/pixels.h"

/* Application owns this state. Services publish borrowed UTF-8 strings, arrays,
 * and images, valid until the next application-thread update between UI frames.
 * UI receives display identity/status only, never credentials or token material.
 * No provider is connected by default; callbacks acknowledge requests, not
 * completion. Requests queue work; providers publish subsequent state between
 * frames and must not invalidate snapshots from inside a UI callback. */
typedef enum maage_launcher_page { MAAGE_PAGE_PLAY, MAAGE_PAGE_NEWS, MAAGE_PAGE_INSTALLATIONS, MAAGE_PAGE_SETTINGS } maage_launcher_page;
typedef enum maage_settings_section { MAAGE_SETTINGS_GENERAL, MAAGE_SETTINGS_GAME, MAAGE_SETTINGS_ACCOUNTS, MAAGE_SETTINGS_ABOUT } maage_settings_section;
typedef enum maage_content_status { MAAGE_CONTENT_UNAVAILABLE, MAAGE_CONTENT_LOADING, MAAGE_CONTENT_EMPTY, MAAGE_CONTENT_READY, MAAGE_CONTENT_ERROR } maage_content_status;
typedef enum maage_account_status { MAAGE_ACCOUNT_SIGNED_OUT, MAAGE_ACCOUNT_AUTHENTICATING, MAAGE_ACCOUNT_SIGNED_IN, MAAGE_ACCOUNT_ERROR } maage_account_status;
typedef enum maage_launcher_result { MAAGE_LAUNCHER_OK, MAAGE_LAUNCHER_UNAVAILABLE, MAAGE_LAUNCHER_INVALID_ARGUMENT, MAAGE_LAUNCHER_FAILED } maage_launcher_result;
typedef enum maage_launcher_action {
    MAAGE_ACTION_SIGN_IN = 0x001, MAAGE_ACTION_CANCEL_SIGN_IN = 0x002,
    MAAGE_ACTION_SELECT_ACCOUNT = 0x004, MAAGE_ACTION_SIGN_OUT = 0x008,
    MAAGE_ACTION_CREATE_INSTALLATION = 0x010, MAAGE_ACTION_EDIT_INSTALLATION = 0x020,
    MAAGE_ACTION_REMOVE_INSTALLATION = 0x040, MAAGE_ACTION_SELECT_INSTALLATION = 0x080,
    MAAGE_ACTION_PLAY = 0x100, MAAGE_ACTION_REFRESH_NEWS = 0x200,
    MAAGE_ACTION_REMOVE_ACCOUNT = 0x400, MAAGE_ACTION_REFRESH_SESSION = 0x800,
    MAAGE_ACTION_REAUTH_ACCOUNT = 0x1000, MAAGE_ACTION_SESSION_ONLY = 0x2000
} maage_launcher_action;
typedef struct maage_launcher_service {
    void *context;
    unsigned long actions;
    maage_launcher_result (*request)(void *context, maage_launcher_action action, const char *id);
} maage_launcher_service;
typedef struct maage_launcher_account {
    const char *id, *provider, *display_name;
    int authenticated, owns_minecraft, minecraft_ready;
    const char *uuid, *status_message;
    const maage_pixel_buffer *avatar;
} maage_launcher_account;
typedef struct maage_launcher_installation {
    const char *id, *name, *version, *modpack;
    int launchable;
} maage_launcher_installation;
typedef struct maage_launcher_article {
    const char *id, *title, *subtitle, *body;
    const maage_pixel_buffer *image;
} maage_launcher_article;
typedef struct maage_launcher_state {
    struct { maage_launcher_page page; maage_settings_section settings; size_t article; } navigation;
    struct {
        maage_account_status status;
        const maage_launcher_account *items;
        size_t count;
        const char *selected_id, *error, *progress;
        int storage_decision_required;
        maage_launcher_service service;
    } accounts;
    struct {
        maage_content_status status;
        const maage_launcher_installation *items;
        size_t count;
        const char *selected_id, *error;
        maage_launcher_service service;
    } installations;
    struct {
        maage_content_status status;
        const maage_launcher_article *items;
        size_t count;
        const char *error;
        maage_launcher_service service;
    } news;
    maage_launcher_settings settings;
    const char *notice;
} maage_launcher_state;
void maage_launcher_state_init(maage_launcher_state *state);
const maage_launcher_account *maage_launcher_selected_account(const maage_launcher_state *state);
const maage_launcher_installation *maage_launcher_selected_installation(const maage_launcher_state *state);
int maage_launcher_can_request(const maage_launcher_state *state, maage_launcher_action action, const char *id);
maage_launcher_result maage_launcher_request(maage_launcher_state *state, maage_launcher_action action, const char *id);
#endif
