#include "internal.h"
#include "maage/log.h"
#include "maage/time.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int maage_auth_cancelled(maage_auth *auth)
{
    int cancelled;
    maage_auth_lock(auth->runtime); cancelled = auth->cancelled || auth->stop; maage_auth_unlock(auth->runtime);
    return cancelled;
}
void maage_auth_progress(maage_auth *auth, const char *message)
{
    maage_auth_lock(auth->runtime); maage_auth_copy(auth->progress, sizeof(auth->progress), message); maage_auth_unlock(auth->runtime);
}
static size_t maage_auth_find(maage_auth *auth, const char *id)
{
    size_t index;
    for (index = 0; index < auth->count; ++index) if (id && !strcmp(id, auth->records[index].id)) break;
    return index;
}
static int maage_auth_ready(const maage_auth_record *record)
{
    return record->microsoft_valid && record->entitled && record->owns_java && record->profile_valid && record->minecraft[0] && record->expires_at > (double)time(NULL) + 60;
}
static maage_auth_error maage_auth_persist_refresh(maage_auth *auth, maage_auth_record *record)
{
    char key[128];
    int decision;
    if (record->session_only || !record->id[0]) return MAAGE_AUTH_OK;
    maage_auth_store_key(record, key);
    if (maage_auth_secret_set(key, record->refresh) == 1) return MAAGE_AUTH_OK;
    maage_auth_lock(auth->runtime);
    auth->candidate_pending = 1; auth->decision = 0; auth->error = MAAGE_AUTH_STORAGE;
    strcpy(auth->progress, "Secure storage is unavailable. Continue for this session without saving credentials, or cancel.");
    while (!auth->decision && !auth->cancelled && !auth->stop) maage_auth_wait(auth->runtime);
    decision = auth->decision;
    auth->candidate_pending = 0;
    maage_auth_unlock(auth->runtime);
    if (decision != 1 || maage_auth_cancelled(auth)) return MAAGE_AUTH_CANCELLED;
    record->session_only = 1;
    maage_auth_secret_delete(key);
    /* Never replace unavailable secure storage with a plaintext token file. */
    return MAAGE_AUTH_OK;
}
static maage_auth_error maage_auth_validate_account(maage_auth *auth, const char *target, int interactive)
{
    maage_auth_record *candidate = &auth->candidate;
    char *access = NULL, *xsts = NULL, hash[33], key[128], expected[40];
    size_t existing = maage_auth_find(auth, target), slot;
    maage_auth_error result, stored;
    int persist_early = 0, secret_result;
    memset(candidate, 0, sizeof(*candidate)); hash[0] = expected[0] = '\0';
    if (existing < auth->count) {
        *candidate = auth->records[existing]; strcpy(expected, candidate->id);
        if (interactive) { maage_auth_wipe(candidate->refresh, sizeof(candidate->refresh)); candidate->session_only = 0; }
        else if (!candidate->refresh[0]) {
            maage_auth_store_key(candidate, key);
            secret_result = candidate->session_only ? 0 : maage_auth_secret_get(key, candidate->refresh, sizeof(candidate->refresh));
            if (secret_result != 1) {
                maage_auth_wipe(candidate, sizeof(*candidate));
                if (secret_result < 0) maage_auth_progress(auth, maage_auth_message(MAAGE_AUTH_STORAGE));
                return secret_result < 0 ? MAAGE_AUTH_STORAGE : MAAGE_AUTH_REAUTH;
            }
        }
    }
    if (!interactive && !candidate->refresh[0]) return MAAGE_AUTH_REAUTH;
    strcpy(candidate->client_id, auth->config.client_id);
    candidate->entitled = candidate->owns_java = candidate->profile_valid = 0;
    candidate->expires_at = 0; maage_auth_wipe(candidate->minecraft, sizeof(candidate->minecraft));
    access = calloc(1, MAAGE_AUTH_TOKEN_SIZE); xsts = calloc(1, MAAGE_AUTH_TOKEN_SIZE);
    if (!access || !xsts) { result = MAAGE_AUTH_PROTOCOL; goto cleanup; }
    result = maage_auth_oauth(auth, candidate, access);
    if (result != MAAGE_AUTH_OK) goto cleanup;
    /* Persist rotation before downstream calls can fail or be cancelled. */
    if (!interactive && candidate->id[0]) {
        stored = maage_auth_persist_refresh(auth, candidate);
        if (stored != MAAGE_AUTH_OK) { result = stored; goto cleanup; }
        maage_auth_lock(auth->runtime);
        strcpy(auth->records[existing].refresh, candidate->refresh); auth->records[existing].session_only = candidate->session_only;
        maage_auth_unlock(auth->runtime);
        if (candidate->session_only && !maage_auth_store_save(auth)) { result = MAAGE_AUTH_STORAGE; goto cleanup; }
        persist_early = 1;
    }
    result = maage_auth_xbox(auth, access, xsts, hash);
    if (hash[0]) {
        sprintf(candidate->id, "xbox:%s", hash);
        if (expected[0] && strcmp(expected, candidate->id)) { result = MAAGE_AUTH_PROTOCOL; goto cleanup; }
        if (!candidate->name[0]) sprintf(candidate->name, "Microsoft account %.8s", hash);
        if (maage_auth_find(auth, candidate->id) == auth->count && auth->count == MAAGE_AUTH_MAX_ACCOUNTS) { result = MAAGE_AUTH_LIMIT; goto cleanup; }
        if (!persist_early) {
            stored = maage_auth_persist_refresh(auth, candidate);
            if (stored != MAAGE_AUTH_OK) { result = stored; goto cleanup; }
        }
    }
    if (result == MAAGE_AUTH_OK) result = maage_auth_minecraft(auth, candidate, xsts, hash);
    if (maage_auth_cancelled(auth)) { result = MAAGE_AUTH_CANCELLED; goto cleanup; }
    /* Keep a genuine Microsoft identity even when Java ownership is absent or
     * Xbox/Minecraft validation fails. It is never a launch-ready credential. */
    if (candidate->id[0] && candidate->microsoft_valid) {
        slot = maage_auth_find(auth, candidate->id);
        if (slot == auth->count && auth->count == MAAGE_AUTH_MAX_ACCOUNTS) { result = MAAGE_AUTH_LIMIT; goto cleanup; }
        maage_auth_lock(auth->runtime);
        if (result == MAAGE_AUTH_NETWORK && existing < auth->count && maage_auth_ready(&auth->records[existing])) {
            strcpy(candidate->minecraft, auth->records[existing].minecraft); candidate->expires_at = auth->records[existing].expires_at;
            candidate->entitled = auth->records[existing].entitled; candidate->owns_java = auth->records[existing].owns_java;
            candidate->profile_valid = auth->records[existing].profile_valid;
        }
        candidate->error = result;
        maage_auth_wipe(&auth->records[slot], sizeof(auth->records[slot])); auth->records[slot] = *candidate;
        if (slot == auth->count) ++auth->count;
        strcpy(auth->selected, candidate->id);
        maage_auth_unlock(auth->runtime);
        if (!maage_auth_store_save(auth)) {
            maage_auth_progress(auth, "Signed in for this session, but account metadata could not be saved. Check your MaaGE data directory permissions.");
            result = MAAGE_AUTH_STORAGE;
        }
    }
cleanup:
    if (!interactive && existing < auth->count && result == MAAGE_AUTH_REAUTH) {
        maage_auth_lock(auth->runtime);
        auth->records[existing].microsoft_valid = 0; auth->records[existing].error = result;
        maage_auth_wipe(auth->records[existing].minecraft, sizeof(auth->records[existing].minecraft));
        maage_auth_unlock(auth->runtime);
    }
    maage_auth_free_secret(access); maage_auth_free_secret(xsts);
    maage_auth_wipe(candidate, sizeof(*candidate));
    return result;
}
void maage_auth_worker(void *context)
{
    maage_auth *auth = context;
    maage_launcher_action command;
    maage_auth_error result;
    char target[40], key[128];
    size_t index;
    int loaded;
    loaded = maage_auth_store_load(auth);
    result = loaded < 0 ? MAAGE_AUTH_STORAGE : MAAGE_AUTH_OK;
    if (auth->config.client_id[0] && auth->selected[0]) result = maage_auth_validate_account(auth, auth->selected, 0);
    maage_auth_lock(auth->runtime);
    auth->busy = 0; auth->changed = 1; auth->error = result;
    if (result != MAAGE_AUTH_RESTRICTED) strcpy(auth->progress, loaded < 0 ? "Saved account metadata could not be read. Check the MaaGE data directory permissions." : maage_auth_message(result));
    maage_auth_unlock(auth->runtime);
    for (;;) {
        maage_auth_lock(auth->runtime);
        while (!auth->command && !auth->stop) maage_auth_wait(auth->runtime);
        if (auth->stop) { maage_auth_unlock(auth->runtime); break; }
        command = auth->command; auth->command = 0; strcpy(target, auth->target);
        maage_auth_unlock(auth->runtime);
        result = MAAGE_AUTH_OK;
        index = maage_auth_find(auth, target);
        if (command == MAAGE_ACTION_SIGN_IN || command == MAAGE_ACTION_REAUTH_ACCOUNT) result = maage_auth_validate_account(auth, target, 1);
        else if (command == MAAGE_ACTION_REFRESH_SESSION || command == MAAGE_ACTION_SELECT_ACCOUNT) {
            if (index == auth->count) result = MAAGE_AUTH_REAUTH;
            else {
                maage_auth_lock(auth->runtime); strcpy(auth->selected, target); maage_auth_unlock(auth->runtime);
                if (command == MAAGE_ACTION_REFRESH_SESSION || !maage_auth_ready(&auth->records[index])) result = maage_auth_validate_account(auth, target, 0);
                if (!maage_auth_store_save(auth)) result = MAAGE_AUTH_STORAGE;
            }
        } else if (index < auth->count && (command == MAAGE_ACTION_SIGN_OUT || command == MAAGE_ACTION_REMOVE_ACCOUNT)) {
            maage_auth_store_key(&auth->records[index], key);
            if (maage_auth_secret_delete(key) < 0) result = MAAGE_AUTH_STORAGE;
            maage_auth_lock(auth->runtime);
            maage_auth_wipe(auth->records[index].refresh, sizeof(auth->records[index].refresh));
            maage_auth_wipe(auth->records[index].minecraft, sizeof(auth->records[index].minecraft));
            auth->records[index].microsoft_valid = 0; auth->records[index].expires_at = 0; auth->records[index].error = MAAGE_AUTH_REAUTH;
            /* Suppress restoration even if the vault refused credential deletion. */
            auth->records[index].session_only = 1;
            if (command == MAAGE_ACTION_REMOVE_ACCOUNT && result == MAAGE_AUTH_OK) {
                maage_auth_wipe(&auth->records[index], sizeof(auth->records[index]));
                memmove(auth->records + index, auth->records + index + 1, (auth->count - index - 1) * sizeof(auth->records[0]));
                --auth->count; maage_auth_wipe(&auth->records[auth->count], sizeof(auth->records[0]));
                if (!strcmp(auth->selected, target)) strcpy(auth->selected, auth->count ? auth->records[0].id : "");
            }
            maage_auth_unlock(auth->runtime);
            if (!maage_auth_store_save(auth)) result = MAAGE_AUTH_STORAGE;
        }
        maage_auth_lock(auth->runtime);
        auth->busy = 0; auth->changed = 1; auth->error = result;
        auth->browser_url[0] = '\0'; auth->retry_at = (double)time(NULL) + 60;
        if (result != MAAGE_AUTH_RESTRICTED) strcpy(auth->progress, maage_auth_message(result));
        maage_auth_unlock(auth->runtime);
        if (result == MAAGE_AUTH_OK) maage_log_info("maage.launcher.auth", "Account operation completed.");
        else maage_log_warn("maage.launcher.auth", "%s", maage_auth_message(result));
    }
}
static maage_launcher_result maage_auth_link_request(void *context, maage_sign_in_action action)
{
    maage_auth *auth = context;
    maage_launcher_result result = MAAGE_LAUNCHER_UNAVAILABLE;
    maage_auth_lock(auth->runtime);
    if (auth->busy && auth->user_code[0] && !auth->cancelled) {
        if (action == MAAGE_SIGN_IN_COPY_CODE) { auth->copy_requested = 1; result = MAAGE_LAUNCHER_OK; }
        else if (action == MAAGE_SIGN_IN_OPEN_BROWSER) { strcpy(auth->browser_url, auth->verification_uri); result = MAAGE_LAUNCHER_OK; }
    }
    maage_auth_unlock(auth->runtime);
    return result;
}
static maage_launcher_result maage_auth_request(void *context, maage_launcher_action action, const char *id)
{
    maage_auth *auth = context;
    maage_launcher_result result = MAAGE_LAUNCHER_OK;
    maage_auth_lock(auth->runtime);
    if (action == MAAGE_ACTION_CANCEL_SIGN_IN) { auth->cancelled = 1; auth->decision = 2; maage_auth_wake(auth->runtime); }
    else if (action == MAAGE_ACTION_SESSION_ONLY && auth->candidate_pending) { auth->decision = 1; maage_auth_wake(auth->runtime); }
    else if (auth->busy || !auth->config.client_id[0] || !maage_auth_copy(auth->target, sizeof(auth->target), id ? id : "")) result = MAAGE_LAUNCHER_UNAVAILABLE;
    else {
        auth->cancelled = 0; auth->busy = 1; auth->command = action; auth->error = MAAGE_AUTH_OK;
        auth->dialog_active = action == MAAGE_ACTION_SIGN_IN || action == MAAGE_ACTION_REAUTH_ACCOUNT;
        auth->copy_requested = 0; auth->ui_hint[0] = auth->link_hint[0] = '\0';
        strcpy(auth->progress, "Preparing account operation..."); maage_auth_wake(auth->runtime);
    }
    maage_auth_unlock(auth->runtime);
    return result;
}
maage_launcher_result maage_auth_create(maage_launcher_state *state, maage_auth **output)
{
    maage_auth *auth;
    if (!output || !state) return MAAGE_LAUNCHER_INVALID_ARGUMENT;
    *output = NULL;
    auth = calloc(1, sizeof(*auth));
    if (!auth) return MAAGE_LAUNCHER_FAILED;
    auth->app = state;
    if (!maage_auth_config_init(&auth->config)) auth->config.client_id[0] = '\0';
    auth->runtime = maage_auth_runtime_create();
    if (!auth->runtime || !maage_auth_http_init()) { maage_auth_runtime_destroy(auth->runtime); free(auth); return MAAGE_LAUNCHER_FAILED; }
    auth->busy = 1; strcpy(auth->progress, "Restoring saved accounts...");
    state->accounts.service.context = auth; state->accounts.service.request = maage_auth_request;
    state->accounts.sign_in.context = auth; state->accounts.sign_in.request = maage_auth_link_request;
    if (!maage_auth_thread_start(auth->runtime, maage_auth_worker, auth)) {
        memset(&state->accounts.service, 0, sizeof(state->accounts.service));
        memset(&state->accounts.sign_in, 0, sizeof(state->accounts.sign_in));
        maage_auth_runtime_destroy(auth->runtime); maage_auth_http_cleanup(); free(auth); return MAAGE_LAUNCHER_FAILED;
    }
    *output = auth;
    return MAAGE_LAUNCHER_OK;
}
void maage_auth_poll(maage_auth *auth)
{
    maage_launcher_state *state = auth->app;
    maage_auth_record *record;
    size_t index;
    char copy_code[64];
    double now = maage_time_monotonic_seconds();
    int ready, refresh = 0;
    maage_auth_lock(auth->runtime);
    if (auth->changed && !auth->busy) {
        if (auth->dialog_active) {
            maage_auth_copy(auth->ui_feedback, sizeof(auth->ui_feedback), auth->progress);
            if (auth->error == MAAGE_AUTH_OK) strcpy(auth->ui_feedback, "Microsoft account linked. Open Accounts for Minecraft ownership and session status.");
            state->notice = auth->ui_feedback; auth->dialog_active = 0;
        }
        for (index = 0; index < auth->count; ++index) {
            record = &auth->records[index]; ready = maage_auth_ready(record);
            strcpy(auth->ids[index], record->id); strcpy(auth->names[index], record->name); strcpy(auth->uuids[index], record->uuid);
            strcpy(auth->statuses[index], !record->microsoft_valid ? "Sign-in required" : record->error == MAAGE_AUTH_NETWORK ? "Connection interrupted" :
                record->error != MAAGE_AUTH_OK ? "Account needs attention" : !record->owns_java || !record->entitled ? "No Minecraft Java ownership" : !record->profile_valid ? "Java profile unavailable" : ready ? "Minecraft Java verified" : "Session needs refresh");
            state->accounts.items = auth->views;
            auth->views[index].id = auth->ids[index]; auth->views[index].display_name = auth->names[index]; auth->views[index].provider = "Microsoft";
            auth->views[index].uuid = auth->uuids[index]; auth->views[index].status_message = auth->statuses[index];
            auth->views[index].authenticated = record->microsoft_valid; auth->views[index].owns_minecraft = record->owns_java && record->entitled;
            auth->views[index].minecraft_ready = ready;
            memcpy(auth->avatars[index], record->avatar, sizeof(record->avatar));
            auth->avatar_views[index].pixels = auth->avatars[index]; auth->avatar_views[index].width = auth->avatar_views[index].height = 8;
            auth->avatar_views[index].stride = 8 * 4;
            auth->views[index].avatar = record->avatar_valid ? &auth->avatar_views[index] : NULL;
        }
        state->accounts.count = auth->count; state->accounts.items = auth->views;
        strcpy(auth->ui_selected, auth->selected); state->accounts.selected_id = auth->ui_selected;
        auth->changed = 0;
    }
    /* Expiration changes readiness even when no worker result arrived. */
    if (!auth->busy) for (index = 0; index < auth->count; ++index) {
        auth->views[index].minecraft_ready = maage_auth_ready(&auth->records[index]);
        if (auth->records[index].microsoft_valid && auth->records[index].error == MAAGE_AUTH_OK && auth->records[index].profile_valid &&
            auth->records[index].owns_java && auth->records[index].entitled && !auth->views[index].minecraft_ready)
            strcpy(auth->statuses[index], "Session needs refresh");
        if (!strcmp(auth->selected, auth->records[index].id) && auth->records[index].microsoft_valid && auth->records[index].refresh[0] &&
            auth->records[index].expires_at > 0 && auth->records[index].expires_at < (double)time(NULL) + 300 && (double)time(NULL) >= auth->retry_at) refresh = 1;
    }
    if (auth->busy) for (index = 0; index < state->accounts.count; ++index) {
        auth->views[index].minecraft_ready = 0; strcpy(auth->statuses[index], "Account operation in progress");
    }
    state->accounts.storage_decision_required = auth->candidate_pending;
    if (!auth->config.client_id[0]) strcpy(auth->ui_message, maage_auth_message(MAAGE_AUTH_CONFIGURATION));
    else if (auth->error == MAAGE_AUTH_OK && !auth->busy) strcpy(auth->ui_message, "");
    else maage_auth_copy(auth->ui_message, sizeof(auth->ui_message), auth->progress[0] ? auth->progress : maage_auth_message(auth->error));
    state->accounts.error = auth->ui_message; state->accounts.progress = auth->ui_message;
    state->accounts.status = auth->busy && !auth->candidate_pending ? MAAGE_ACCOUNT_AUTHENTICATING : !auth->config.client_id[0] || auth->error != MAAGE_AUTH_OK ?
        MAAGE_ACCOUNT_ERROR : MAAGE_ACCOUNT_SIGNED_OUT;
    if (state->accounts.status == MAAGE_ACCOUNT_SIGNED_OUT) for (index = 0; index < state->accounts.count; ++index)
        if (!strcmp(auth->ids[index], auth->ui_selected) && auth->views[index].authenticated) state->accounts.status = MAAGE_ACCOUNT_SIGNED_IN;
    state->accounts.service.actions = auth->busy ? (MAAGE_ACTION_CANCEL_SIGN_IN | (auth->candidate_pending ? MAAGE_ACTION_SESSION_ONLY : 0)) :
        auth->config.client_id[0] ? MAAGE_ACTION_SIGN_IN | MAAGE_ACTION_SELECT_ACCOUNT | MAAGE_ACTION_SIGN_OUT | MAAGE_ACTION_REMOVE_ACCOUNT | MAAGE_ACTION_REFRESH_SESSION | MAAGE_ACTION_REAUTH_ACCOUNT : 0;
    state->accounts.sign_in.active = auth->dialog_active;
    state->accounts.sign_in.can_link = auth->user_code[0] && !auth->cancelled && now >= 0 && now < auth->device_deadline;
    state->accounts.sign_in.seconds_remaining = state->accounts.sign_in.can_link ? auth->device_deadline - now : 0;
    maage_auth_copy(auth->ui_code, sizeof(auth->ui_code), state->accounts.sign_in.can_link ? auth->user_code : "");
    maage_auth_copy(auth->ui_uri, sizeof(auth->ui_uri), state->accounts.sign_in.can_link ? auth->verification_uri : "");
    state->accounts.sign_in.user_code = auth->ui_code; state->accounts.sign_in.verification_uri = auth->ui_uri;
    maage_auth_copy(auth->ui_hint, sizeof(auth->ui_hint), auth->link_hint);
    state->accounts.sign_in.hint = auth->ui_hint;
    if (auth->cancelled && auth->busy) strcpy(auth->ui_message, "Cancelling sign-in...");
    copy_code[0] = '\0';
    if (state->accounts.sign_in.can_link && auth->copy_requested) strcpy(copy_code, auth->user_code);
    auth->copy_requested = 0;
    maage_auth_unlock(auth->runtime);
    if (copy_code[0]) {
        ready = maage_auth_copy_code(copy_code);
        maage_auth_lock(auth->runtime);
        strcpy(auth->link_hint, ready ? "Code copied to clipboard." : "Clipboard unavailable. Enter the displayed code manually.");
        strcpy(auth->ui_hint, auth->link_hint);
        maage_auth_unlock(auth->runtime);
    }
    if (refresh) maage_auth_request(auth, MAAGE_ACTION_REFRESH_SESSION, auth->ui_selected);
}
void maage_auth_session_clear(maage_minecraft_session *session) { if (session) maage_auth_wipe(session, sizeof(*session)); }
maage_launcher_result maage_auth_session_copy(maage_auth *auth, const char *id, maage_minecraft_session *session)
{
    size_t index;
    int ready = 0;
    if (!auth || !id || !session) return MAAGE_LAUNCHER_INVALID_ARGUMENT;
    maage_auth_session_clear(session);
    maage_auth_lock(auth->runtime);
    index = auth->busy ? MAAGE_AUTH_MAX_ACCOUNTS : maage_auth_find(auth, id);
    if (!auth->busy && index < auth->count && maage_auth_ready(&auth->records[index])) {
        ready = 1; strcpy(session->uuid, auth->records[index].uuid); strcpy(session->username, auth->records[index].name);
        strcpy(session->access_token, auth->records[index].minecraft); session->expires_at = auth->records[index].expires_at;
    }
    maage_auth_unlock(auth->runtime);
    if (!ready) maage_auth_request(auth, MAAGE_ACTION_REFRESH_SESSION, id);
    return ready ? MAAGE_LAUNCHER_OK : MAAGE_LAUNCHER_UNAVAILABLE;
}
void maage_auth_destroy(maage_auth *auth)
{
    if (!auth) return;
    maage_auth_lock(auth->runtime); auth->stop = auth->cancelled = 1; maage_auth_wake(auth->runtime); maage_auth_unlock(auth->runtime);
    maage_auth_runtime_destroy(auth->runtime);
    if (auth->app->notice == auth->ui_feedback) auth->app->notice = NULL;
    auth->app->accounts.error = auth->app->accounts.progress = NULL;
    auth->app->accounts.items = NULL; auth->app->accounts.count = 0; auth->app->accounts.selected_id = NULL;
    memset(&auth->app->accounts.service, 0, sizeof(auth->app->accounts.service));
    memset(&auth->app->accounts.sign_in, 0, sizeof(auth->app->accounts.sign_in));
    maage_auth_http_cleanup(); maage_auth_wipe(auth, sizeof(*auth)); free(auth);
}
