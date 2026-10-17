#include "access.h"
#include "accounts.h"
#include "verify.h"
#include "profile_store.h"
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
#define MAAGE_ACCESS_REFERENCES 128
#define MAAGE_ACCESS_MANUAL 16
typedef struct maage_access_reference {
    char uuid[33], launcher[32], path[MAAGE_PATH_CAPACITY];
    unsigned record;
    int compatible;
} maage_access_reference;
struct maage_access {
#ifdef _WIN32
    CRITICAL_SECTION mutex;
    HANDLE thread;
#else
    pthread_mutex_t mutex;
    pthread_t thread;
#endif
    int has_thread, cancelled, operation, loaded;
    char selected[33], active[33], manual[MAAGE_PATH_CAPACITY], root[MAAGE_PATH_CAPACITY];
    char directories[MAAGE_ACCESS_MANUAL][MAAGE_PATH_CAPACITY];
    unsigned directory_count, reference_count;
    maage_access_reference references[MAAGE_ACCESS_REFERENCES];
    maage_access_snapshot view;
    double deadlines[MAAGE_ACCESS_PROFILES], starts[MAAGE_ACCESS_PROFILES];
};
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
const char *maage_access_state_name(maage_access_state state)
{
    switch (state) {
    case MAAGE_ACCESS_DETECTED: return "Detected";
    case MAAGE_ACCESS_CONNECTED: return "Connected - unverified";
    case MAAGE_ACCESS_VERIFYING: return "Verifying";
    case MAAGE_ACCESS_VERIFIED: return "Verified";
    case MAAGE_ACCESS_EXPIRED: return "Expired - verify again";
    case MAAGE_ACCESS_UNSUPPORTED: return "Verification unsupported";
    default: return "Verification failed";
    }
}
static unsigned maage_access_find(maage_access *a, const char *uuid)
{
    unsigned i;
    for (i = 0; i < a->view.count; ++i) if (!strcmp(a->view.profiles[i].uuid, uuid)) break;
    return i;
}
/* Called only under the service lock. Neither a registry flag nor a source
 * launcher login/ownership flag can create a deadline. */
static int maage_access_policy(maage_access *a)
{
    unsigned i;
    int allowed = 0;
    double now = maage_time_monotonic_seconds(), wall = difftime(time(NULL), (time_t)0);
    for (i = 0; i < a->view.count; ++i) {
        maage_access_profile *p = &a->view.profiles[i];
        if (p->state != MAAGE_ACCESS_VERIFIED) continue;
        if (a->cancelled || !p->connected || now < 0 || now < a->starts[i] || now >= a->deadlines[i] || wall < p->last_verified || wall >= p->last_verified + 3600.0) {
            p->state = MAAGE_ACCESS_EXPIRED; a->deadlines[i] = 0;
            strcpy(p->detail, "Verification expired. MaaGE can check its saved session again, or re-import an expired session."); ++a->view.revision;
        } else if (!*a->active || !strcmp(a->active, p->uuid)) allowed = 1;
    }
    a->view.allowed = allowed;
    return allowed;
}
int maage_access_allowed(maage_access *a)
{
    int allowed;
    if (!a) return 0;
    maage_access_lock(a); allowed = maage_access_policy(a); maage_access_unlock(a);
    return allowed;
}
int maage_access_select(maage_access *a, const char *uuid)
{
    unsigned index;
    int ok = 1;
    maage_access_lock(a);
    if (!uuid) a->active[0] = 0;
    else {
        index = maage_access_find(a, uuid);
        if (index == a->view.count || !a->view.profiles[index].connected) ok = 0;
        else strcpy(a->active, uuid);
    }
    maage_access_policy(a); ++a->view.revision; maage_access_unlock(a);
    return ok;
}
void maage_access_snapshot_get(maage_access *a, maage_access_snapshot *snapshot)
{ maage_access_lock(a); maage_access_policy(a); *snapshot = a->view; maage_access_unlock(a); }
static int maage_access_cancelled(void *context)
{
    maage_access *a = (maage_access *)context;
    int cancelled;
    maage_access_lock(a); cancelled = a->cancelled; maage_access_unlock(a); return cancelled;
}
void maage_access_cancel(maage_access *a)
{ maage_access_lock(a); a->cancelled = 1; maage_access_policy(a); maage_access_unlock(a); }
static void maage_access_status(maage_access *a, const char *status)
{ maage_access_lock(a); strcpy(a->view.status, status); maage_access_unlock(a); }
static int maage_access_absolute(const char *path)
{
#ifdef _WIN32
    return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' && (path[2] == '/' || path[2] == '\\');
#else
    return path[0] == '/';
#endif
}
static int maage_access_config_path(maage_access *a, char path[MAAGE_PATH_CAPACITY], const char *file)
{
    char directory[MAAGE_PATH_CAPACITY];
    return maage_storage_join(directory, sizeof(directory), a->root, "config") && maage_storage_directories(directory) && maage_storage_join(path, MAAGE_PATH_CAPACITY, directory, file);
}
static void maage_access_load(maage_access *a)
{
    char path[MAAGE_PATH_CAPACITY], *data;
    FILE *file;
    size_t size, i, count;
    struct json_object *root, *list, *item, *value;
    const char *text, *name;
    char uuid[33];
    a->loaded = 1;
    if (!maage_access_config_path(a, path, "minecraft-profiles.json")) return;
    file = maage_storage_open(path, "rb"); if (!file) return;
    data = (char *)malloc(131073);
    if (!data) { fclose(file); return; }
    size = fread(data, 1, 131073, file); fclose(file);
    root = size <= 131072 ? maage_access_json(data, size) : NULL; free(data);
    if (!root) { maage_log_warn("maage.launcher.access", "Ignoring malformed profile registry; verification is still required."); return; }
    maage_access_lock(a);
    if (json_object_object_get_ex(root, "profiles", &list) && json_object_is_type(list, json_type_array)) {
        count = json_object_array_length(list);
        if (count > MAAGE_ACCESS_PROFILES) count = MAAGE_ACCESS_PROFILES;
        for (i = 0; i < count; ++i) {
            item = json_object_array_get_idx(list, i);
            name = maage_access_json_string(item, "name");
            if (!maage_access_username(name) || !maage_access_uuid(maage_access_json_string(item, "uuid"), uuid)) continue;
            if (maage_access_find(a, uuid) < a->view.count) continue;
            {
                maage_access_profile *p = &a->view.profiles[a->view.count++];
                strcpy(p->name, name); strcpy(p->uuid, uuid); p->connected = 1; p->state = MAAGE_ACCESS_CONNECTED;
                text = maage_access_json_string(item, "sources");
                if (strlen(text) < sizeof(p->sources)) strcpy(p->sources, text);
                if (json_object_object_get_ex(item, "lastVerified", &value) && (json_object_is_type(value, json_type_double) || json_object_is_type(value, json_type_int))) p->last_verified = json_object_get_double(value);
                if (p->last_verified > 0) p->state = MAAGE_ACCESS_EXPIRED;
                strcpy(p->detail, "Local profile restored. Checking the saved session and verification lease.");
            }
        }
    }
    maage_access_unlock(a);
    if (json_object_object_get_ex(root, "launcherDirectories", &list) && json_object_is_type(list, json_type_array)) {
        count = json_object_array_length(list);
        for (i = 0; i < count && a->directory_count < MAAGE_ACCESS_MANUAL; ++i) {
            item = json_object_array_get_idx(list, i);
            if (!json_object_is_type(item, json_type_string)) continue;
            text = json_object_get_string(item);
            if (strlen(text) != (size_t)json_object_get_string_len(item) || strlen(text) >= MAAGE_PATH_CAPACITY || !maage_access_absolute(text)) continue;
            strcpy(a->directories[a->directory_count++], text);
        }
    }
    json_object_put(root);
}
static void maage_access_save(maage_access *a)
{
    struct json_object *root, *list, *item, *directories;
    maage_storage_lock *lock;
    maage_access_snapshot *snapshot;
    char path[MAAGE_PATH_CAPACITY], temporary[MAAGE_PATH_CAPACITY], lockpath[MAAGE_PATH_CAPACITY];
    const char *data;
    size_t length;
    unsigned i;
    FILE *file;
    int ok = 0;
    if (!maage_access_config_path(a, path, "minecraft-profiles.json") || !maage_access_config_path(a, temporary, "minecraft-profiles.part") || !maage_access_config_path(a, lockpath, "minecraft-profiles.lock")) goto failed;
    lock = maage_storage_acquire(lockpath); if (!lock) goto failed;
    snapshot = (maage_access_snapshot *)malloc(sizeof(*snapshot));
    if (!snapshot) { maage_storage_release(lock); goto failed; }
    maage_access_snapshot_get(a, snapshot);
    root = json_object_new_object(); list = json_object_new_array(); directories = json_object_new_array();
    if (!root || !list || !directories) {
        if (root) json_object_put(root);
        if (list) json_object_put(list);
        if (directories) json_object_put(directories);
        free(snapshot); maage_storage_release(lock); goto failed;
    }
    json_object_object_add(root, "version", json_object_new_int(2));
    json_object_object_add(root, "profiles", list); json_object_object_add(root, "launcherDirectories", directories);
    for (i = 0; i < snapshot->count; ++i) {
        maage_access_profile *p = &snapshot->profiles[i];
        if (!p->connected) continue;
        item = json_object_new_object(); if (!item) continue;
        json_object_object_add(item, "name", json_object_new_string(p->name));
        json_object_object_add(item, "uuid", json_object_new_string(p->uuid));
        json_object_object_add(item, "sources", json_object_new_string(p->sources));
        json_object_object_add(item, "credentialProvider", json_object_new_string(p->imported ? "minecraft-session-import" : "none"));
        json_object_object_add(item, "state", json_object_new_string(maage_access_state_name(p->state)));
        json_object_object_add(item, "lastVerified", json_object_new_double(p->last_verified));
        json_object_array_add(list, item);
    }
    for (i = 0; i < a->directory_count; ++i) json_object_array_add(directories, json_object_new_string(a->directories[i]));
    data = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY); length = strlen(data);
    maage_storage_remove(temporary);
    file = maage_storage_open(temporary, "wb");
    if (file) { ok = fwrite(data, 1, length, file) == length; if (fclose(file)) ok = 0; }
    if (ok) ok = maage_storage_replace(temporary, path);
    if (!ok) maage_storage_remove(temporary);
    json_object_put(root); free(snapshot); maage_storage_release(lock);
    if (ok) return;
failed:
    maage_log_warn("maage.launcher.access", "Cannot save nonsecret linked-profile registry.");
    maage_access_status(a, "Profile changes apply for this session, but the registry could not be saved. Check MaaGE's data-folder permissions.");
}
static void maage_access_add_file(maage_access *a, const char *directory, const char *filename, const char *launcher, int prism)
{
    char path[MAAGE_PATH_CAPACITY], uuid[33], fingerprint[128];
    maage_account_file *accounts;
    unsigned i, pindex, j;
    int compatible;
    double size;
    if (maage_access_cancelled(a) || !maage_storage_join(path, sizeof(path), directory, filename) || !maage_storage_stat(path, &size, fingerprint)) return;
    for (j = 0; j < a->reference_count; ++j) if (!strcmp(a->references[j].path, path)) return;
    accounts = (maage_account_file *)malloc(sizeof(*accounts)); if (!accounts) return;
    maage_log_info("maage.launcher.access", "Discovering public account identities from %s (%s).", launcher, path);
    if (!maage_accounts_read(path, prism, -1, accounts, NULL, NULL)) {
        maage_access_lock(a); ++a->view.unsupported_files; maage_access_unlock(a); free(accounts); return;
    }
    for (i = 0; i < accounts->count; ++i) {
        maage_account_record *record = &accounts->records[i];
        maage_access_profile *p;
        maage_access_reference *ref;
        if (!maage_access_username(record->name) || !maage_access_uuid(record->uuid, uuid)) continue;
        if (a->reference_count >= MAAGE_ACCESS_REFERENCES) break;
        compatible = prism && accounts->version == 3 && !strcmp(record->type, "MSA");
        maage_access_lock(a);
        pindex = maage_access_find(a, uuid);
        if (pindex == a->view.count) {
            if (pindex == MAAGE_ACCESS_PROFILES) { maage_access_unlock(a); break; }
            ++a->view.count; memset(&a->view.profiles[pindex], 0, sizeof(a->view.profiles[pindex]));
            strcpy(a->view.profiles[pindex].uuid, uuid);
        }
        p = &a->view.profiles[pindex]; if (!p->imported) strcpy(p->name, record->name); p->available = 1;
        if (!strstr(p->sources, launcher) && strlen(p->sources) + strlen(launcher) + 3 < sizeof(p->sources)) {
            if (*p->sources) strcat(p->sources, ", ");
            strcat(p->sources, launcher);
        }
        if (!p->connected) p->state = MAAGE_ACCESS_DETECTED;
        if (!p->imported && p->state != MAAGE_ACCESS_VERIFIED) strcpy(p->detail, compatible ? "Compatible account detected. Import its Minecraft session into MaaGE." : "Identity discovery only. This launcher/account format has no supported Minecraft session transfer.");
        ++a->view.revision; maage_access_unlock(a);
        ref = &a->references[a->reference_count++]; strcpy(ref->uuid, uuid); strcpy(ref->launcher, launcher); strcpy(ref->path, path); ref->record = i; ref->compatible = compatible;
    }
    free(accounts);
}
static void maage_access_directory(maage_access *a, const char *directory, const char *launcher, int prism)
{
    if (!maage_storage_directory_exists(directory)) return;
    maage_access_lock(a); ++a->view.launchers_found;
    if (!strstr(a->view.detected_launchers, launcher) && strlen(a->view.detected_launchers) + strlen(launcher) + 3 < sizeof(a->view.detected_launchers)) {
        if (*a->view.detected_launchers) strcat(a->view.detected_launchers, ", ");
        strcat(a->view.detected_launchers, launcher);
    }
    maage_access_unlock(a);
    maage_access_add_file(a, directory, prism ? "accounts.json" : "launcher_accounts.json", launcher, prism);
    if (!strcmp(launcher, "FTB App")) {
        char bin[MAAGE_PATH_CAPACITY];
        if (maage_storage_join(bin, sizeof(bin), directory, "bin")) maage_access_add_file(a, bin, "launcher_accounts.json", launcher, 0);
    }
}
static int maage_access_environment(const char *key, char out[MAAGE_PATH_CAPACITY])
{
#ifdef _WIN32
    wchar_t name[64];
    const wchar_t *value;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key, -1, name, 64)) return 0;
    value = _wgetenv(name);
    return value && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, out, MAAGE_PATH_CAPACITY, NULL, NULL) > 0;
#else
    const char *value = getenv(key);
    if (!value || strlen(value) >= MAAGE_PATH_CAPACITY || !maage_access_absolute(value)) return 0;
    strcpy(out, value); return 1;
#endif
}
static void maage_access_candidate(maage_access *a, const char *base, const char *relative, const char *launcher, int prism)
{
    char directory[MAAGE_PATH_CAPACITY];
    if (maage_storage_join(directory, sizeof(directory), base, relative)) maage_access_directory(a, directory, launcher, prism);
}
static void maage_access_manual_scan(maage_access *a, const char *directory)
{
    char marker[MAAGE_PATH_CAPACITY], fingerprint[128];
    double size;
    const char *source = "Prism/MultiMC (manual)";
    if (maage_storage_join(marker, sizeof(marker), directory, "prismlauncher.cfg") && maage_storage_stat(marker, &size, fingerprint)) source = "PrismLauncher";
    else if (maage_storage_join(marker, sizeof(marker), directory, "multimc.cfg") && maage_storage_stat(marker, &size, fingerprint)) source = "MultiMC";
    maage_access_directory(a, directory, source, 1);
    maage_access_add_file(a, directory, "launcher_accounts.json", "Official/embedded launcher", 0);
    maage_access_candidate(a, directory, "bin", "FTB App", 0);
    maage_access_candidate(a, directory, "Install", "CurseForge", 0);
    maage_access_candidate(a, directory, "minecraft/Install", "CurseForge", 0);
}
static void maage_access_restore_profiles(maage_access *a);
static void maage_access_discover(maage_access *a)
{
    char home[MAAGE_PATH_CAPACITY], base[MAAGE_PATH_CAPACITY];
    unsigned i, j;
    if (!a->loaded) maage_access_load(a);
    maage_access_lock(a);
    for (i = 0, j = 0; i < a->view.count; ++i) if (a->view.profiles[i].connected) {
        a->view.profiles[j] = a->view.profiles[i]; a->view.profiles[j].available = 0;
        a->deadlines[j] = a->deadlines[i]; a->starts[j] = a->starts[i]; ++j;
    }
    a->view.count = j; a->view.detected_launchers[0] = 0; a->view.launchers_found = 0; a->view.unsupported_files = 0; ++a->view.revision;
    maage_access_unlock(a); a->reference_count = 0;
    if (*a->manual) {
        for (i = 0; i < a->directory_count; ++i) if (!strcmp(a->directories[i], a->manual)) break;
        if (i == a->directory_count && i < MAAGE_ACCESS_MANUAL) strcpy(a->directories[a->directory_count++], a->manual);
        else if (i == MAAGE_ACCESS_MANUAL) maage_access_status(a, "Manual folder limit reached. Disconnect unused profiles and edit the nonsecret registry to remove old folders.");
    }
#ifdef _WIN32
    if (maage_access_environment("APPDATA", base)) {
        maage_access_candidate(a, base, "PrismLauncher", "PrismLauncher", 1);
        maage_access_candidate(a, base, "MultiMC", "MultiMC", 1);
        maage_access_candidate(a, base, ".minecraft", "Minecraft Launcher", 0);
    }
    if (maage_access_environment("LOCALAPPDATA", base)) {
        maage_access_candidate(a, base, "ftb-app", "FTB App", 0);
        maage_access_candidate(a, base, ".ftba", "FTB App", 0);
        maage_access_candidate(a, base, "CurseForge", "CurseForge", 0);
    }
    if (maage_access_environment("USERPROFILE", home)) {
        maage_access_candidate(a, home, ".ftba", "FTB App", 0);
        maage_access_candidate(a, home, "curseforge/minecraft/Install", "CurseForge", 0);
        maage_access_candidate(a, home, "Documents/curseforge/minecraft/Install", "CurseForge", 0);
    }
#else
    if (maage_access_environment("HOME", home)) {
#ifdef __APPLE__
        if (maage_storage_join(base, sizeof(base), home, "Library/Application Support")) {
            maage_access_candidate(a, base, "PrismLauncher", "PrismLauncher", 1);
            maage_access_candidate(a, base, "MultiMC", "MultiMC", 1);
            maage_access_candidate(a, base, "multimc", "MultiMC", 1);
            maage_access_candidate(a, base, "minecraft", "Minecraft Launcher", 0);
            maage_access_candidate(a, base, "ftb-app", "FTB App", 0);
            maage_access_candidate(a, base, "FTB App", "FTB App", 0);
            maage_access_candidate(a, base, "CurseForge", "CurseForge", 0);
        }
#else
        if (!maage_access_environment("XDG_DATA_HOME", base)) maage_storage_join(base, sizeof(base), home, ".local/share");
        maage_access_candidate(a, base, "PrismLauncher", "PrismLauncher", 1);
        maage_access_candidate(a, base, "multimc", "MultiMC", 1);
        maage_access_candidate(a, base, "MultiMC", "MultiMC", 1);
        maage_access_candidate(a, base, "ftb-app", "FTB App", 0);
        maage_access_candidate(a, home, ".minecraft", "Minecraft Launcher", 0);
        maage_access_candidate(a, home, ".config/PrismLauncher", "PrismLauncher", 1);
        maage_access_candidate(a, home, ".config/MultiMC", "MultiMC", 1);
        maage_access_candidate(a, home, ".config/local/multimc", "MultiMC", 1);
        maage_access_candidate(a, home, ".var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher", "PrismLauncher", 1);
        maage_access_candidate(a, home, ".var/app/org.multimc.MultiMC/data/multimc", "MultiMC", 1);
#endif
        maage_access_candidate(a, home, ".ftba", "FTB App", 0);
        maage_access_candidate(a, home, "curseforge/minecraft/Install", "CurseForge", 0);
        maage_access_candidate(a, home, "Documents/curseforge/minecraft/Install", "CurseForge", 0);
    }
#endif
    for (i = 0; i < a->directory_count && !maage_access_cancelled(a); ++i) maage_access_manual_scan(a, a->directories[i]);
    maage_access_lock(a);
    if (!a->cancelled) {
        if (!a->view.launchers_found) strcpy(a->view.status, "No launcher found. Choose its configuration folder manually, or install/sign in through PrismLauncher or MultiMC.");
        else if (!a->view.count) sprintf(a->view.status, "Found: %s. No supported player account records. FTB/CurseForge private stores are unsupported; choose a PrismLauncher/MultiMC configuration folder.", a->view.detected_launchers);
        else sprintf(a->view.status, "Found: %s. Select an account and Import Account. Only compatible PrismLauncher/MultiMC sessions can verify. FTB/CurseForge private stores are unsupported.", a->view.detected_launchers);
    }
    for (i = 0; i < a->view.count; ++i) if (!a->view.profiles[i].available && !a->view.profiles[i].imported)
        strcpy(a->view.profiles[i].detail, "The linked source account is unavailable. Choose its launcher folder or sign in there, then rescan.");
    ++a->view.revision; maage_access_unlock(a);
    maage_access_restore_profiles(a);
    maage_access_save(a);
}
static void maage_access_verify_selected(maage_access *a, int importing)
{
    unsigned index, i;
    maage_access_reference *ref = NULL;
    maage_account_file *accounts = NULL;
    maage_profile_session *session = (maage_profile_session *)calloc(1, sizeof(*session));
    char detail[256], name[17] = "", uuid[33];
    maage_access_state state = MAAGE_ACCESS_FAILED;
    double wall, now, duration;
    int stored = 0;
    maage_access_lock(a); index = maage_access_find(a, a->selected);
    if (index == a->view.count) { maage_access_unlock(a); free(session); return; }
    a->view.profiles[index].connected = 1; a->view.profiles[index].state = MAAGE_ACCESS_VERIFYING;
    a->deadlines[index] = 0; ++a->view.revision; maage_access_unlock(a);
    strcpy(detail, "Cannot allocate the account verification worker.");
    if (!session) goto done;
    if (!importing) {
        strcpy(detail, "The local account session is unavailable or damaged. Re-import this account; access remains unverified.");
        if (!maage_profile_store_load(a->root, a->selected, session)) goto done;
        stored = 1;
        /* Clear the sealed lease before a new request; failures must not restore
         * an older verified flag on the next process start. Keep the token. */
        session->verified_at = session->verified_until = 0;
        if (!maage_profile_store_save(a->root, session)) { strcpy(detail, "Cannot update the local credential file. Check MaaGE data permissions and retry."); goto done; }
    } else {
        /* A rejected re-import must not leave an older sealed lease usable. */
        if (maage_profile_store_load(a->root, a->selected, session)) {
            stored = 1; session->verified_at = session->verified_until = 0;
            if (!maage_profile_store_save(a->root, session)) { strcpy(detail, "Cannot update the saved account before re-import. Check MaaGE data permissions."); goto done; }
            OPENSSL_cleanse(session, sizeof(*session));
        }
        for (i = 0; i < a->reference_count; ++i)
            if (!strcmp(a->references[i].uuid, a->selected) && a->references[i].compatible) { ref = &a->references[i]; break; }
        state = MAAGE_ACCESS_UNSUPPORTED;
        strcpy(detail, "No compatible Minecraft session import is available. Sign in through PrismLauncher/MultiMC, then rescan.");
        if (!ref) goto done;
        accounts = (maage_account_file *)malloc(sizeof(*accounts));
        state = MAAGE_ACCESS_FAILED;
        if (!accounts) goto done;
        maage_log_info("maage.launcher.access", "Approved session import from %s for profile %s.", ref->launcher, a->selected);
        if (!maage_accounts_read(ref->path, 1, (int)ref->record, accounts, session->minecraft_token, a->selected) || ref->record >= accounts->count ||
            !maage_access_uuid(accounts->records[ref->record].uuid, uuid) || strcmp(uuid, a->selected)) {
            strcpy(detail, "The source account changed or is unsupported. Rescan and select it again."); goto done;
        }
        strcpy(session->uuid, a->selected);
        session->expires_at = accounts->records[ref->record].expires;
        if (!*session->minecraft_token) { strcpy(detail, "No transferable Minecraft session was found. Refresh it in the source launcher and re-import."); goto done; }
    }
    wall = difftime(time(NULL), (time_t)0);
    if (session->expires_at > 0 && session->expires_at <= wall) {
        state = MAAGE_ACCESS_EXPIRED;
        strcpy(detail, "The imported Minecraft session expired. Refresh the original launcher session and re-import it into MaaGE."); goto done;
    }
    if (maage_access_cancelled(a)) goto done;
    state = maage_access_verify(session->minecraft_token, a->selected, name, detail, maage_access_cancelled, a);
    wall = difftime(time(NULL), (time_t)0); now = maage_time_monotonic_seconds();
    if (state == MAAGE_ACCESS_VERIFIED && !maage_access_cancelled(a)) {
        if (session->expires_at <= 0) session->expires_at = wall + 3600.0;
        if (session->expires_at <= wall || now < 0) { state = MAAGE_ACCESS_EXPIRED; strcpy(detail, "The session expired during verification. Re-import a current session."); goto done; }
        strcpy(session->name, name); session->verified_at = wall;
        session->verified_until = wall + 3600.0;
        if (session->verified_until > session->expires_at) session->verified_until = session->expires_at;
        if (!maage_profile_store_save(a->root, session)) {
            state = MAAGE_ACCESS_FAILED; strcpy(detail, "Minecraft access passed, but MaaGE could not save the local account. Check data-folder permissions and retry."); goto done;
        }
        stored = 1;
        strcpy(detail, importing ? "Account imported into MaaGE. Its saved session will be checked automatically when needed." : "Saved Minecraft session verified. No source launcher was accessed.");
    }
done:
    free(accounts);
    now = maage_time_monotonic_seconds(); wall = difftime(time(NULL), (time_t)0);
    maage_access_lock(a);
    if (a->cancelled) { state = MAAGE_ACCESS_CONNECTED; strcpy(detail, "Account operation cancelled. No access was granted."); }
    if (state == MAAGE_ACCESS_VERIFIED && (now < 0 || !session || session->verified_until <= wall)) {
        state = MAAGE_ACCESS_EXPIRED; strcpy(detail, "The saved session expired. Re-import a current Minecraft session.");
    }
    a->view.profiles[index].state = state;
    if (stored) a->view.profiles[index].imported = 1;
    strcpy(a->view.profiles[index].detail, detail);
    if (state == MAAGE_ACCESS_VERIFIED) {
        duration = session->verified_until - wall;
        strcpy(a->view.profiles[index].name, session->name); a->view.profiles[index].last_verified = session->verified_at;
        a->starts[index] = now; a->deadlines[index] = now + duration;
    }
    strcpy(a->view.status, detail); ++a->view.revision; maage_access_unlock(a);
    if (session && stored && maage_access_cancelled(a)) {
        session->verified_at = session->verified_until = 0;
        if (*session->name) maage_profile_store_save(a->root, session);
    }
    if (session) { OPENSSL_cleanse(session, sizeof(*session)); free(session); }
    maage_log_info("maage.launcher.access", "Profile %s: %s. %s", a->selected, maage_access_state_name(state), detail);
    maage_access_save(a);
}
static void maage_access_restore_profiles(maage_access *a)
{
    maage_profile_session *session = (maage_profile_session *)calloc(1, sizeof(*session));
    unsigned i;
    double wall, now;
    char uuid[33];
    int check;
    if (!session) return;
    maage_access_status(a, "Restoring MaaGE accounts and checking saved Minecraft sessions...");
    /* Restore all still-valid leases before considering any network requests. */
    for (i = 0; i < a->view.count && !maage_access_cancelled(a); ++i) {
        maage_access_lock(a); strcpy(uuid, a->view.profiles[i].uuid); maage_access_unlock(a);
        if (!maage_profile_store_load(a->root, uuid, session)) continue;
        wall = difftime(time(NULL), (time_t)0); now = maage_time_monotonic_seconds();
        maage_access_lock(a);
        a->view.profiles[i].connected = 1; a->view.profiles[i].imported = 1; strcpy(a->view.profiles[i].name, session->name);
        if (session->verified_at > 0 && wall >= session->verified_at && wall < session->verified_until && wall < session->expires_at && now >= 0) {
            a->view.profiles[i].state = MAAGE_ACCESS_VERIFIED; a->view.profiles[i].last_verified = session->verified_at;
            a->starts[i] = now; a->deadlines[i] = now + session->verified_until - wall;
            strcpy(a->view.profiles[i].detail, "Restored a current, authenticated local verification lease.");
        } else {
            a->deadlines[i] = 0;
            a->view.profiles[i].state = session->expires_at <= wall ? MAAGE_ACCESS_EXPIRED : MAAGE_ACCESS_CONNECTED;
            strcpy(a->view.profiles[i].detail, session->expires_at <= wall ? "The saved session expired. Refresh it in the original launcher and re-import." : "MaaGE has a saved Minecraft session; an access check is due.");
        }
        ++a->view.revision; maage_access_unlock(a);
        OPENSSL_cleanse(session, sizeof(*session));
    }
    OPENSSL_cleanse(session, sizeof(*session)); free(session);
    for (i = 0; i < a->view.count && !maage_access_cancelled(a) && !maage_access_allowed(a); ++i) {
        maage_access_lock(a);
        check = a->view.profiles[i].imported && a->view.profiles[i].state == MAAGE_ACCESS_CONNECTED;
        strcpy(uuid, a->view.profiles[i].uuid); maage_access_unlock(a);
        if (check) { strcpy(a->selected, uuid); maage_access_verify_selected(a, 0); }
    }
    maage_access_lock(a);
    a->view.auto_continue = maage_access_policy(a);
    if (a->view.auto_continue) strcpy(a->view.status, "A local Minecraft account is ready. Continuing setup...");
    else if (!a->view.count) strcpy(a->view.status, "No Minecraft accounts found. Choose a launcher folder or sign in through PrismLauncher/MultiMC.");
    else strcpy(a->view.status, "Import a launcher account, verify a saved session, or re-import an expired session to continue.");
    ++a->view.revision; maage_access_unlock(a);
}
#ifdef _WIN32
static DWORD WINAPI maage_access_worker(LPVOID context)
#else
static void *maage_access_worker(void *context)
#endif
{
    maage_access *a = (maage_access *)context;
    if (maage_access_cancelled(a)) { /* Cancellation never starts new account I/O. */ }
    else if (a->operation == 1) maage_access_discover(a);
    else if (a->operation == 2 || a->operation == 4) maage_access_verify_selected(a, a->operation == 2);
    else {
        unsigned i;
        int removed = maage_profile_store_remove(a->root, a->selected);
        maage_access_lock(a); i = maage_access_find(a, a->selected);
        if (i < a->view.count) {
            a->view.profiles[i].connected = !removed; a->view.profiles[i].imported = !removed; a->view.profiles[i].state = removed ? MAAGE_ACCESS_DETECTED : MAAGE_ACCESS_FAILED;
            strcpy(a->view.profiles[i].detail, removed ? "Local account disconnected. Import it again to reconnect." : "The saved credential could not be removed. Access is revoked for this session; retry Disconnect after fixing permissions.");
            a->deadlines[i] = 0; ++a->view.revision;
        }
        strcpy(a->view.status, removed ? "Local account removed. Existing Minecraft resources are preserved." : "Access revoked for this session, but the saved credential could not be removed. Check MaaGE data permissions and retry Disconnect."); maage_access_unlock(a);
        maage_access_save(a);
    }
    maage_access_lock(a); a->view.busy = 0; ++a->view.revision; maage_access_unlock(a);
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
static int maage_access_begin(maage_access *a, int operation, const char *value)
{
    char normalized[33];
    if (operation == 1 && value && (!maage_access_absolute(value) || strlen(value) >= sizeof(a->manual))) return 0;
    maage_access_lock(a);
    if (a->view.busy || a->cancelled) { maage_access_unlock(a); return 0; }
    if (operation != 1 && (!maage_access_uuid(value, normalized) || maage_access_find(a, normalized) == a->view.count)) { maage_access_unlock(a); return 0; }
    a->view.busy = 1; a->view.auto_continue = 0;
    maage_access_unlock(a); maage_access_join(a); maage_access_lock(a);
    if (operation == 1) {
        a->manual[0] = 0;
        if (value) {
            strcpy(a->manual, value);
        }
        strcpy(a->view.status, "Scanning installed launcher account metadata...");
    } else {
        strcpy(a->selected, normalized);
        a->deadlines[maage_access_find(a, normalized)] = 0;
        strcpy(a->view.status, operation == 2 ? "Importing this Minecraft account..." : operation == 4 ? "Verifying the saved Minecraft session..." : "Removing local account...");
    }
    a->operation = operation;
#ifdef _WIN32
    a->thread = CreateThread(NULL, 0, maage_access_worker, a, 0, NULL); a->has_thread = a->thread != NULL;
#else
    a->has_thread = pthread_create(&a->thread, NULL, maage_access_worker, a) == 0;
#endif
    if (!a->has_thread) { a->view.busy = 0; strcpy(a->view.status, "Cannot start account worker. Close other applications and retry."); }
    maage_access_unlock(a);
    return a->has_thread;
}
int maage_access_scan(maage_access *a, const char *directory) { return maage_access_begin(a, 1, directory); }
int maage_access_connect(maage_access *a, const char *uuid) { return maage_access_begin(a, 2, uuid); }
int maage_access_verify_local(maage_access *a, const char *uuid) { return maage_access_begin(a, 4, uuid); }
int maage_access_disconnect(maage_access *a, const char *uuid) { return maage_access_begin(a, 3, uuid); }
maage_access *maage_access_create(void)
{
    maage_access *a = (maage_access *)calloc(1, sizeof(*a));
    if (!a) return NULL;
#ifdef _WIN32
    InitializeCriticalSection(&a->mutex);
#else
    if (pthread_mutex_init(&a->mutex, NULL)) { free(a); return NULL; }
#endif
    if (!maage_storage_data_path(a->root, sizeof(a->root))) { maage_access_destroy(a); return NULL; }
    strcpy(a->view.status, "Scan for existing Minecraft player accounts to begin."); return a;
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
    free(a);
}
