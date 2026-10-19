#include "access.h"
#include "accounts.h"
#include "verify.h"
#include "profile_store.h"
#include "oauth.h"
#include "maage/storage.h"
#include "maage/time.h"
#include "maage/log.h"
#include <openssl/crypto.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif
struct maage_access {
#ifdef _WIN32
    CRITICAL_SECTION mutex;
    HANDLE thread;
#else
    pthread_mutex_t mutex;
    pthread_t thread;
#endif
    int has_thread, cancelled, revoked, operation, loaded;
    char selected[33], active[33], root[MAAGE_PATH_CAPACITY];
    maage_access_snapshot view;
    double deadlines[MAAGE_ACCESS_PROFILES], starts[MAAGE_ACCESS_PROFILES], retry[MAAGE_ACCESS_PROFILES];
};
static int maage_access_begin(maage_access *a, int operation, const char *uuid);
static void maage_access_lock(maage_access *a)
{
#ifdef _WIN32
    EnterCriticalSection(&a->mutex);
#else
    pthread_mutex_lock(&a->mutex);
#endif
}
static void maage_access_unlock(maage_access *a)
{
#ifdef _WIN32
    LeaveCriticalSection(&a->mutex);
#else
    pthread_mutex_unlock(&a->mutex);
#endif
}
static double maage_access_wall(void) { return difftime(time(NULL), (time_t)0); }
const char *maage_access_state_name(maage_access_state state)
{
    switch (state) {
    case MAAGE_ACCESS_DETECTED: return "Signed out";
    case MAAGE_ACCESS_CONNECTED: return "Connected - unverified";
    case MAAGE_ACCESS_VERIFYING: return "Verifying";
    case MAAGE_ACCESS_VERIFIED: return "Verified";
    case MAAGE_ACCESS_EXPIRED: return "Expired - refresh required";
    case MAAGE_ACCESS_UNSUPPORTED: return "Sign in with Microsoft";
    default: return "Verification failed";
    }
}
static unsigned maage_access_find(maage_access *a, const char *uuid)
{
    unsigned i;
    for (i = 0; i < a->view.count; ++i) if (!strcmp(a->view.profiles[i].uuid, uuid)) break;
    return i;
}
static int maage_access_policy(maage_access *a)
{
    unsigned i;
    int allowed = 0;
    double now = maage_time_monotonic_seconds(), wall = maage_access_wall();
    for (i = 0; i < a->view.count; ++i) {
        maage_access_profile *p = &a->view.profiles[i];
        if (p->state != MAAGE_ACCESS_VERIFIED) continue;
        if (a->revoked || !p->connected || now < a->starts[i] || now >= a->deadlines[i] || wall < p->last_verified || wall >= p->last_verified + 3600) {
            p->state = MAAGE_ACCESS_EXPIRED; a->deadlines[i] = 0;
            strcpy(p->detail, "Verification lease expired. MaaGE will refresh this session securely."); ++a->view.revision;
        } else if (!*a->active || !strcmp(a->active, p->uuid)) allowed = 1;
    }
    a->view.allowed = allowed; return allowed;
}
/* Service callers (including active asset work) schedule renewal without UI.
 * The worker keeps a current lease usable until expiry or an actual failure. */
static void maage_access_renewal(maage_access *a, char renew[33])
{
    unsigned i;
    double now = maage_time_monotonic_seconds();
    renew[0] = 0;
    if (a->view.busy || a->revoked || now < 0) return;
    for (i = 0; i < a->view.count; ++i) {
        maage_access_profile *p = &a->view.profiles[i];
        if (p->session_saved && (!*a->active || !strcmp(a->active, p->uuid)) && now >= a->retry[i] &&
            (p->state == MAAGE_ACCESS_EXPIRED || (p->state == MAAGE_ACCESS_VERIFIED && a->deadlines[i] - now < 90))) {
            strcpy(renew, p->uuid); return;
        }
    }
}
int maage_access_allowed(maage_access *a)
{
    int allowed;
    char renew[33];
    if (!a) return 0;
    maage_access_lock(a); allowed = maage_access_policy(a); maage_access_renewal(a, renew); maage_access_unlock(a);
    if (*renew) maage_access_begin(a, 4, renew);
    return allowed;
}
int maage_access_select(maage_access *a, const char *uuid)
{
    unsigned i;
    int ok = 1;
    maage_access_lock(a);
    if (!uuid) a->active[0] = 0;
    else {
        i = maage_access_find(a, uuid);
        if (i == a->view.count || !a->view.profiles[i].connected) ok = 0;
        else strcpy(a->active, uuid);
    }
    maage_access_policy(a); ++a->view.revision; maage_access_unlock(a); return ok;
}
void maage_access_snapshot_get(maage_access *a, maage_access_snapshot *snapshot)
{
    char renew[33];
    maage_access_lock(a); maage_access_policy(a); maage_access_renewal(a, renew);
    *snapshot = a->view; maage_access_unlock(a);
    if (*renew) maage_access_begin(a, 4, renew);
}
static int maage_access_cancelled(void *context)
{
    maage_access *a = (maage_access *)context;
    int result;
    maage_access_lock(a); result = a->cancelled; maage_access_unlock(a); return result;
}
void maage_access_cancel_operation(maage_access *a)
{ maage_access_lock(a); a->cancelled = 1; maage_access_unlock(a); }
void maage_access_cancel(maage_access *a)
{ maage_access_lock(a); a->cancelled = a->revoked = 1; maage_access_policy(a); maage_access_unlock(a); }
static void maage_access_status(void *context, const char *text)
{
    maage_access *a = (maage_access *)context;
    maage_access_lock(a); strcpy(a->view.status, text); ++a->view.revision; maage_access_unlock(a);
}
static void maage_access_device(void *context, const char *url, const char *code, double deadline)
{
    maage_access *a = (maage_access *)context;
    maage_access_lock(a); strcpy(a->view.device_url, url); strcpy(a->view.device_code, code);
    a->view.device_deadline = deadline; ++a->view.revision; maage_access_unlock(a);
}
static int maage_access_path(maage_access *a, char *path, const char *file)
{
    char directory[MAAGE_PATH_CAPACITY];
    return maage_storage_join(directory, sizeof(directory), a->root, "config") && maage_storage_directories(directory) && maage_storage_join(path, MAAGE_PATH_CAPACITY, directory, file);
}
static void maage_access_load(maage_access *a)
{
    char path[MAAGE_PATH_CAPACITY], *data, uuid[33];
    FILE *file;
    size_t size, i, count;
    struct json_object *root, *list, *item;
    const char *name;
    a->loaded = 1;
    if (!maage_access_path(a, path, "minecraft-profiles.json")) return;
    file = maage_storage_open(path, "rb"); if (!file) return;
    data = (char *)malloc(131073); if (!data) { fclose(file); return; }
    size = fread(data, 1, 131073, file); fclose(file);
    root = size <= 131072 ? maage_access_json(data, size) : NULL; free(data);
    if (!root) return;
    maage_access_lock(a);
    if (json_object_object_get_ex(root, "profiles", &list) && json_object_is_type(list, json_type_array)) {
        count = json_object_array_length(list); if (count > MAAGE_ACCESS_PROFILES) count = MAAGE_ACCESS_PROFILES;
        for (i = 0; i < count; ++i) {
            maage_access_profile *p;
            item = json_object_array_get_idx(list, i); name = maage_access_json_string(item, "name");
            if (!maage_access_username(name) || !maage_access_uuid(maage_access_json_string(item, "uuid"), uuid) || maage_access_find(a, uuid) < a->view.count) continue;
            p = &a->view.profiles[a->view.count++]; strcpy(p->uuid, uuid); strcpy(p->name, name);
            p->connected = 1; p->state = MAAGE_ACCESS_DETECTED; strcpy(p->sources, "Microsoft");
            p->last_verified = maage_auth_number(item, "lastVerified"); if (p->last_verified < 0) p->last_verified = 0;
            strcpy(p->detail, "Sign in with Microsoft to connect this profile to MaaGE. Imported launcher sessions are no longer used.");
        }
    }
    ++a->view.revision; maage_access_unlock(a); json_object_put(root);
}
static int maage_access_save(maage_access *a)
{
    struct json_object *root, *list, *item;
    maage_storage_lock *lock;
    char path[MAAGE_PATH_CAPACITY], temporary[MAAGE_PATH_CAPACITY], lockpath[MAAGE_PATH_CAPACITY];
    const char *data;
    size_t length;
    unsigned i;
    FILE *file;
    int ok = 0;
    if (!maage_access_path(a, path, "minecraft-profiles.json") || !maage_access_path(a, temporary, "minecraft-profiles.part") || !maage_access_path(a, lockpath, "minecraft-profiles.lock")) return 0;
    lock = maage_storage_acquire(lockpath); if (!lock) return 0;
    root = json_object_new_object(); list = json_object_new_array();
    if (!root || !list) { if (root) json_object_put(root); if (list) json_object_put(list); maage_storage_release(lock); return 0; }
    json_object_object_add(root, "version", json_object_new_int(3)); json_object_object_add(root, "profiles", list);
    maage_access_lock(a);
    for (i = 0; i < a->view.count; ++i) {
        maage_access_profile *p = &a->view.profiles[i];
        item = json_object_new_object(); if (!item) continue;
        json_object_object_add(item, "name", json_object_new_string(p->name));
        json_object_object_add(item, "uuid", json_object_new_string(p->uuid));
        json_object_object_add(item, "credentialProvider", json_object_new_string(p->session_saved ? "microsoft-oauth" : "none"));
        json_object_object_add(item, "lastVerified", json_object_new_double(p->last_verified));
        json_object_array_add(list, item);
    }
    maage_access_unlock(a);
    data = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    if (data && maage_storage_remove(temporary)) {
        length = strlen(data); file = maage_storage_open(temporary, "wb");
        if (file) { ok = fwrite(data, 1, length, file) == length; if (fclose(file)) ok = 0; }
        if (ok) ok = maage_storage_replace(temporary, path);
    }
    if (!ok) maage_storage_remove(temporary);
    json_object_put(root); maage_storage_release(lock); return ok;
}
static void maage_access_apply(maage_access *a, unsigned i, maage_profile_session *s)
{
    double now = maage_time_monotonic_seconds(), wall = maage_access_wall();
    maage_access_profile *p = &a->view.profiles[i];
    strcpy(p->name, s->name); strcpy(p->uuid, s->uuid); strcpy(p->sources, "Microsoft");
    p->connected = p->session_saved = 1; p->refreshing = 0; p->last_verified = s->verified_at;
    if (s->verified_at > 0 && wall >= s->verified_at && wall < s->verified_until && wall < s->expires_at && now >= 0 && !a->revoked) {
        p->state = MAAGE_ACCESS_VERIFIED; a->starts[i] = now; a->deadlines[i] = now + s->verified_until - wall;
        strcpy(p->detail, "Minecraft Java Edition access verified. Permanent purchase versus subscription is not conclusively established.");
    } else { p->state = MAAGE_ACCESS_EXPIRED; a->deadlines[i] = 0; }
    ++a->view.revision;
}
static maage_storage_lock *maage_access_credential_lock(maage_access *a, const char *uuid)
{
    char directory[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY], name[64];
    sprintf(name, "%s.refresh.lock", uuid);
    if (!maage_storage_join(directory, sizeof(directory), a->root, "config/accounts") || !maage_storage_directories(directory) ||
        !maage_storage_join(path, sizeof(path), directory, name)) return NULL;
    return maage_storage_acquire(path);
}
static void maage_access_authenticate(maage_access *a, int operation)
{
    maage_profile_session *s = (maage_profile_session *)calloc(1, sizeof(*s));
    maage_auth_observer observer;
    unsigned i;
    int ok = 0, saved = 0;
    maage_storage_lock *refresh_lock = NULL;
    char detail[256] = "Cannot allocate Microsoft sign-in session.";
    observer.cancelled = maage_access_cancelled; observer.status = maage_access_status; observer.device = maage_access_device; observer.context = a;
    if (!s) goto done;
    if (operation == 4) {
        if (!(refresh_lock = maage_access_credential_lock(a, a->selected))) {
            strcpy(detail, "This account is being updated by another MaaGE process. Wait and retry Refresh."); goto done;
        }
        if (!maage_profile_store_load(a->root, a->selected, s)) { strcpy(detail, "No saved Microsoft session. Sign in with Microsoft to reconnect this account."); goto done; }
        ok = maage_oauth_refresh(a->root, s, &observer, detail);
    } else ok = maage_oauth_login(s, operation == 6, &observer, detail);
    if (ok && !maage_access_cancelled(a)) {
        maage_access_lock(a);
        i = maage_access_find(a, s->uuid);
        if (i == a->view.count && i == MAAGE_ACCESS_PROFILES) {
            ok = 0; strcpy(detail, "Account limit reached. Remove an account before adding another.");
        }
        maage_access_unlock(a);
    }
    if (ok && !maage_access_cancelled(a) && !refresh_lock) {
        refresh_lock = maage_access_credential_lock(a, s->uuid);
        if (!refresh_lock) { ok = 0; strcpy(detail, "This account is being updated by another MaaGE process. Retry sign-in when it finishes."); }
    }
    if (ok && !maage_access_cancelled(a)) {
        saved = maage_profile_store_save(a->root, s); ok = saved;
        if (!saved) strcpy(detail, "Java access verified, but MaaGE could not encrypt/save the account. Check data-folder permissions and retry.");
    } else ok = 0;
done:
    maage_access_lock(a);
    i = s && *s->uuid ? maage_access_find(a, s->uuid) : maage_access_find(a, a->selected);
    if (a->cancelled) { ok = 0; strcpy(detail, "Sign-in cancelled. Retry or choose device-code sign-in."); }
    if (saved && i == a->view.count && i < MAAGE_ACCESS_PROFILES) {
        ++a->view.count; strcpy(a->view.profiles[i].uuid, s->uuid); strcpy(a->view.profiles[i].name, s->name);
        strcpy(a->view.profiles[i].sources, "Microsoft"); a->view.profiles[i].connected = a->view.profiles[i].session_saved = 1;
    }
    if (i < a->view.count) {
        if (ok) maage_access_apply(a, i, s);
        else { a->view.profiles[i].state = (strstr(detail, "Cannot reach") || strstr(detail, "HTTP 5") || strstr(detail, "HTTP 429")) ? MAAGE_ACCESS_EXPIRED : MAAGE_ACCESS_FAILED; a->deadlines[i] = 0; a->view.profiles[i].refreshing = 0; }
        strcpy(a->view.profiles[i].detail, detail); a->retry[i] = maage_time_monotonic_seconds() + (ok ? 60 : 300);
    }
    strcpy(a->view.status, ok ? "Minecraft Java Edition access successfully verified. Continue to Preparing MaaGE." : detail);
    a->view.auto_continue = 0; ++a->view.revision; maage_access_unlock(a);
    if (saved && !ok && s) { s->verified_at = s->verified_until = 0; maage_profile_store_save(a->root, s); }
    if (!maage_access_save(a)) {
        maage_access_lock(a); i = s && *s->uuid ? maage_access_find(a, s->uuid) : maage_access_find(a, a->selected);
        if (i < a->view.count) { a->deadlines[i] = 0; a->view.profiles[i].state = MAAGE_ACCESS_FAILED; }
        maage_access_unlock(a); ok = 0;
        if (saved && s) { s->verified_at = s->verified_until = 0; maage_profile_store_save(a->root, s); }
        maage_access_status(a, "Cannot save profile registry. Check MaaGE data-folder permissions before continuing.");
    }
    maage_storage_release(refresh_lock);
    if (s) { OPENSSL_cleanse(s, sizeof(*s)); free(s); }
    maage_log_info("maage.launcher.access", "Microsoft account operation %s.", ok ? "verified Java access" : "did not grant access");
}
static void maage_access_restore_worker(maage_access *a)
{
    maage_profile_session *s;
    unsigned i;
    char uuid[33];
    if (!a->loaded) maage_access_load(a);
    s = (maage_profile_session *)calloc(1, sizeof(*s)); if (!s) return;
    for (i = 0; i < a->view.count && !maage_access_cancelled(a); ++i) {
        maage_access_lock(a); strcpy(uuid, a->view.profiles[i].uuid); maage_access_unlock(a);
        if (maage_profile_store_load(a->root, uuid, s)) {
            maage_access_lock(a); maage_access_apply(a, i, s); maage_access_unlock(a);
        }
        OPENSSL_cleanse(s, sizeof(*s));
    }
    free(s);
    for (i = 0; i < a->view.count && !maage_access_cancelled(a); ++i) {
        int refresh;
        maage_access_lock(a); refresh = a->view.profiles[i].session_saved && a->view.profiles[i].state != MAAGE_ACCESS_VERIFIED;
        strcpy(a->selected, a->view.profiles[i].uuid); maage_access_unlock(a);
        if (refresh) maage_access_authenticate(a, 4);
    }
    maage_access_lock(a); a->selected[0] = 0;
    a->view.auto_continue = maage_access_policy(a);
    strcpy(a->view.status, a->view.auto_continue ? "A verified Minecraft Java account is ready. Continuing setup..." : "Sign in with your Microsoft account to verify Minecraft Java Edition access.");
    ++a->view.revision; maage_access_unlock(a);
}
#ifdef _WIN32
static DWORD WINAPI maage_access_worker(LPVOID context)
#else
static void *maage_access_worker(void *context)
#endif
{
    maage_access *a = (maage_access *)context;
    if (maage_access_cancelled(a)) { }
    else if (a->operation == 1) maage_access_restore_worker(a);
    else if (a->operation == 2 || a->operation == 4 || a->operation == 6) maage_access_authenticate(a, a->operation);
    else {
        unsigned i;
        maage_storage_lock *lock = maage_access_credential_lock(a, a->selected);
        int removed = lock && maage_profile_store_remove(a->root, a->selected);
        maage_access_lock(a); i = maage_access_find(a, a->selected);
        if (i < a->view.count) {
            a->deadlines[i] = 0; a->view.profiles[i].session_saved = 0;
            a->view.profiles[i].state = removed ? MAAGE_ACCESS_DETECTED : MAAGE_ACCESS_FAILED;
            strcpy(a->view.profiles[i].detail, removed ? "Signed out. Sign in with Microsoft to reconnect." : "Credential removal failed. Access revoked; check data-folder permissions and retry.");
            if (removed && a->operation == 3) {
                --a->view.count;
                memmove(&a->view.profiles[i], &a->view.profiles[i + 1], (a->view.count - i) * sizeof(a->view.profiles[0]));
                memmove(&a->deadlines[i], &a->deadlines[i + 1], (a->view.count - i) * sizeof(double));
                memmove(&a->starts[i], &a->starts[i + 1], (a->view.count - i) * sizeof(double));
                memmove(&a->retry[i], &a->retry[i + 1], (a->view.count - i) * sizeof(double));
                if (!strcmp(a->active, a->selected)) a->active[0] = 0;
            }
            ++a->view.revision;
        }
        strcpy(a->view.status, removed ? "MaaGE credentials removed. Downloaded Minecraft resources are preserved." : "Credential removal failed. Fix permissions and retry Sign Out / Remove.");
        maage_access_unlock(a);
        if (!maage_access_save(a)) maage_access_status(a, "Could not save account registry changes. Check data-folder permissions.");
        maage_storage_release(lock);
    }
    maage_access_lock(a); a->view.device_code[0] = a->view.device_url[0] = 0; a->view.device_deadline = 0;
    a->view.busy = 0; ++a->view.revision; maage_access_unlock(a);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
static void maage_access_join(maage_access *a)
{
    if (!a->has_thread) return;
#ifdef _WIN32
    WaitForSingleObject(a->thread, INFINITE); CloseHandle(a->thread);
#else
    pthread_join(a->thread, NULL);
#endif
    a->has_thread = 0;
}
static int maage_access_begin(maage_access *a, int operation, const char *uuid)
{
    char normalized[33] = "";
    unsigned i;
    if (!a) return 0;
    maage_access_lock(a);
    if (a->view.busy || a->revoked) { maage_access_unlock(a); return 0; }
    if (uuid && (!maage_access_uuid(uuid, normalized) || maage_access_find(a, normalized) == a->view.count)) { maage_access_unlock(a); return 0; }
    a->view.busy = 1; a->view.auto_continue = 0; a->cancelled = 0;
    maage_access_unlock(a); maage_access_join(a); maage_access_lock(a);
    strcpy(a->selected, normalized); a->operation = operation;
    if (*normalized) {
        i = maage_access_find(a, normalized);
        if (operation != 4 || a->view.profiles[i].state != MAAGE_ACCESS_VERIFIED) {
            a->deadlines[i] = 0; a->view.profiles[i].state = MAAGE_ACCESS_VERIFYING;
        } a->view.profiles[i].refreshing = operation == 4;
    }
    strcpy(a->view.status, operation == 1 ? "Restoring MaaGE Microsoft accounts..." : operation == 3 || operation == 5 ? "Removing MaaGE credentials..." : "Starting Microsoft authentication...");
    ++a->view.revision;
#ifdef _WIN32
    a->thread = CreateThread(NULL, 0, maage_access_worker, a, 0, NULL); a->has_thread = a->thread != NULL;
#else
    a->has_thread = pthread_create(&a->thread, NULL, maage_access_worker, a) == 0;
#endif
    if (!a->has_thread) { a->view.busy = 0; strcpy(a->view.status, "Cannot start account worker. Retry."); }
    maage_access_unlock(a); return a->has_thread;
}
int maage_access_restore(maage_access *a) { return maage_access_begin(a, 1, NULL); }
int maage_access_sign_in(maage_access *a, int device) { return maage_access_begin(a, device ? 6 : 2, NULL); }
int maage_access_verify_local(maage_access *a, const char *uuid) { return maage_access_begin(a, 4, uuid); }
int maage_access_disconnect(maage_access *a, const char *uuid) { return maage_access_begin(a, 3, uuid); }
int maage_access_sign_out(maage_access *a, const char *uuid) { return maage_access_begin(a, 5, uuid); }
maage_access *maage_access_create(void)
{
    maage_access *a = (maage_access *)calloc(1, sizeof(*a)); if (!a) return NULL;
#ifdef _WIN32
    InitializeCriticalSection(&a->mutex);
#else
    if (pthread_mutex_init(&a->mutex, NULL)) { free(a); return NULL; }
#endif
    if (!maage_storage_data_path(a->root, sizeof(a->root))) { maage_access_destroy(a); return NULL; }
    strcpy(a->view.status, "Sign in with Microsoft to verify Minecraft Java Edition access."); return a;
}
void maage_access_destroy(maage_access *a)
{
    if (!a) return;
    maage_access_cancel(a); maage_access_join(a);
#ifdef _WIN32
    DeleteCriticalSection(&a->mutex);
#else
    pthread_mutex_destroy(&a->mutex);
#endif
    OPENSSL_cleanse(a, sizeof(*a)); free(a);
}
