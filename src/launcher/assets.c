#include "assets.h"
#include "client_resources.h"
#include <ctype.h>
#include "maage/storage.h"
#include "maage/minecraft_assets.h"
#include "maage/download.h"
#include "maage/log.h"
#include <json-c/json.h>
#if JSON_C_VERSION_NUM < ((0 << 16) | (15 << 8))
#error MaaGE requires json-c 0.15 or newer
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif
#define MAAGE_METADATA_LIMIT (16.0 * 1024.0 * 1024.0)
#define MAAGE_OBJECT_LIMIT (512.0 * 1024.0 * 1024.0)
#define MAAGE_MANIFEST "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"

typedef struct maage_asset_object {
    const char *name; /* One indexed name for download diagnostics; aliases remain logical entries. */
    char hash[41], fingerprint[128];
    double size;
    int verified, repaired;
} maage_asset_object;
typedef struct maage_logical_asset {
    const char *name;
    char hash[41], fingerprint[128];
    maage_asset_object *object;
    int verified;
} maage_logical_asset;
struct maage_assets {
#ifdef _WIN32
    CRITICAL_SECTION mutex;
    HANDLE thread;
#else
    pthread_mutex_t mutex;
    pthread_t thread;
#endif
    int has_thread, running, repair;
    maage_access *access;
    maage_setup_progress progress;
    char root[MAAGE_PATH_CAPACITY];
};
static void maage_assets_lock(maage_assets *assets)
{
#ifdef _WIN32
    EnterCriticalSection(&assets->mutex);
#else
    pthread_mutex_lock(&assets->mutex);
#endif
}
static void maage_assets_unlock(maage_assets *assets)
{
#ifdef _WIN32
    LeaveCriticalSection(&assets->mutex);
#else
    pthread_mutex_unlock(&assets->mutex);
#endif
}
void maage_assets_snapshot(maage_assets *assets, maage_setup_progress *progress)
{
    maage_assets_lock(assets); *progress = assets->progress; maage_assets_unlock(assets);
}
void maage_assets_cancel(maage_assets *assets)
{
    maage_assets_lock(assets); assets->progress.cancellation_requested = 1; maage_assets_unlock(assets);
}
static int maage_assets_cancelled(void *context)
{
    maage_assets *assets = (maage_assets *)context;
    int cancelled;
    maage_assets_lock(assets); cancelled = assets->progress.cancellation_requested; maage_assets_unlock(assets);
    return cancelled || !maage_access_allowed(assets->access);
}
static void maage_assets_bytes(void *context, double bytes)
{
    maage_assets *assets = (maage_assets *)context;
    maage_assets_lock(assets); assets->progress.downloaded_bytes += bytes; maage_assets_unlock(assets);
}
static void maage_assets_stage(maage_assets *assets, maage_setup_stage stage, const char *activity)
{
    maage_assets_lock(assets);
    assets->progress.stage = stage;
    strncpy(assets->progress.activity, activity, sizeof(assets->progress.activity) - 1);
    maage_assets_unlock(assets);
    maage_log_info("maage.launcher.assets", "%s", activity);
}
static const char *maage_assets_string(struct json_object *object, const char *key)
{
    struct json_object *value;
    const char *text;
    if (!object || !json_object_object_get_ex(object, key, &value) || !json_object_is_type(value, json_type_string)) return NULL;
    text = json_object_get_string(value);
    return strlen(text) == (size_t)json_object_get_string_len(value) ? text : NULL;
}
static int maage_assets_hash(const char *text)
{
    size_t i;
    if (!text || strlen(text) != 40) return 0;
    for (i = 0; i < 40; ++i) if (!strchr("0123456789abcdef", text[i])) return 0;
    return 1;
}
static int maage_assets_size(struct json_object *object, double limit, double *size)
{
    struct json_object *value;
    if (!json_object_object_get_ex(object, "size", &value) || !json_object_is_type(value, json_type_int)) return 0;
    *size = json_object_get_double(value);
    return *size >= 0 && *size <= limit && floor(*size) == *size;
}
static int maage_assets_official(const char *url)
{
    const char *hosts[] = { "https://piston-meta.mojang.com/", "https://launchermeta.mojang.com/", "https://launchermeta.minecraft.net/" };
    size_t i;
    if (!url || strlen(url) > 2048 || strchr(url, '\\') || strchr(url, '\r') || strchr(url, '\n')) return 0;
    for (i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i)
        if (!strncmp(url, hosts[i], strlen(hosts[i]))) return 1;
    return 0;
}
static struct json_object *maage_assets_json(const char *path)
{
    FILE *file;
    double size;
    char fingerprint[128], *text;
    struct json_object *object;
    struct json_tokener *tokener;
    size_t count;
    if (!maage_storage_stat(path, &size, fingerprint) || size <= 0 || size > MAAGE_METADATA_LIMIT) return NULL;
    file = maage_storage_open(path, "rb");
    if (!file) return NULL;
    text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(file); return NULL; }
    count = fread(text, 1, (size_t)size, file);
    if (count != (size_t)size || ferror(file)) { free(text); fclose(file); return NULL; }
    fclose(file); text[count] = 0;
    tokener = json_tokener_new_ex(32);
    if (!tokener) { free(text); return NULL; }
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    object = json_tokener_parse_ex(tokener, text, (int)count + 1);
    if (json_tokener_get_error(tokener) != json_tokener_success || !json_object_is_type(object, json_type_object)) {
        if (object) json_object_put(object);
        object = NULL;
    }
    json_tokener_free(tokener); free(text); return object;
}
static int maage_assets_write_json(const char *path, struct json_object *object)
{
    char temporary[MAAGE_PATH_CAPACITY];
    const char *text = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    FILE *file;
    int ok;
    if (!text || strlen(path) + 6 >= sizeof(temporary)) return 0;
    strcpy(temporary, path); strcat(temporary, ".part");
    maage_storage_remove(temporary);
    file = maage_storage_open(temporary, "wb");
    if (!file) return 0;
    ok = fwrite(text, 1, strlen(text), file) == strlen(text);
    if (fclose(file)) ok = 0;
    if (ok) ok = maage_storage_replace(temporary, path);
    if (!ok) maage_storage_remove(temporary);
    return ok;
}
static int maage_assets_file(maage_assets *assets, const char *path, const char *expected, double expected_size)
{
    char hash[41], fingerprint[128];
    double size;
    int valid;
    maage_log_debug("maage.launcher.assets", "Hash-verifying %s (expected SHA-1 %s, bytes %.0f)", path, expected, expected_size);
    valid = maage_storage_stat(path, &size, fingerprint) && (expected_size < 0 || size == expected_size) &&
        maage_storage_sha1(path, hash, maage_assets_cancelled, assets) && !strcmp(hash, expected);
    maage_log_debug("maage.launcher.assets", "Verification %s: %s", valid ? "passed" : "failed", path);
    return valid;
}
static int maage_assets_download(maage_assets *assets, const char *url, const char *path, const char *hash, double size, char error[512])
{
    maage_download_request request;
    maage_download_observer observer;
    request.url = url; request.destination = path; request.sha1 = hash;
    request.size = size; request.limit = MAAGE_METADATA_LIMIT;
    observer.cancelled = maage_assets_cancelled; observer.bytes = NULL; observer.context = assets;
    return maage_download_batch(&request, 1, &observer, error);
}
static int maage_assets_compare(const void *left, const void *right)
{
    return strcmp(((const maage_asset_object *)left)->hash, ((const maage_asset_object *)right)->hash);
}
static int maage_assets_object_path(maage_assets *assets, const char *hash, char path[MAAGE_PATH_CAPACITY])
{
    char relative[80];
    sprintf(relative, "minecraft/assets/objects/%.2s", hash);
    if (!maage_storage_join(path, MAAGE_PATH_CAPACITY, assets->root, relative) || !maage_storage_directories(path)) return 0;
    sprintf(relative, "minecraft/assets/objects/%.2s/%s", hash, hash);
    return maage_storage_join(path, MAAGE_PATH_CAPACITY, assets->root, relative);
}
static void maage_assets_count(maage_assets *assets, unsigned long objects, unsigned long logical)
{
    double total;
    maage_assets_lock(assets);
    assets->progress.verified_objects = objects;
    assets->progress.reconstructed_files = logical;
    total = (double)assets->progress.total_objects + assets->progress.total_logical_assets +
        2.0 * assets->progress.client_resource_count + 1;
    assets->progress.percent = assets->progress.client_resource_count ?
        (int)(((double)objects + logical + assets->progress.client_resources_checked +
        assets->progress.client_resources_processed + assets->progress.client_archive_ready) * 100.0 / total) : -1;
    if (assets->progress.percent >= 100) assets->progress.percent = 99;
    maage_assets_unlock(assets);
}
/* Migrate only verified files. Repeated/interrupted attempts can mix both layouts. */
static int maage_assets_reuse(maage_assets *assets, const char *destination,
    const char *legacy_relative, const char *hash, double size)
{
    char source[MAAGE_PATH_CAPACITY], parent[MAAGE_PATH_CAPACITY], temporary[MAAGE_PATH_CAPACITY];
    char *separator;
    int result;
    if (!maage_storage_join(source, sizeof(source), assets->root, legacy_relative)) return 0;
    strcpy(parent, source); separator = strrchr(parent, '/');
    if (!separator) return 0;
    *separator = 0;
    if (!maage_storage_directory_exists(parent) || !maage_assets_file(assets, source, hash, size)) return 0;
    maage_log_info("maage.launcher.assets", "Migrating verified file: %s -> %s (SHA-1 %s)", source, destination, hash);
    result = maage_storage_replace(source, destination);
    if (!result && strlen(destination) + 6 < sizeof(temporary)) {
        strcpy(temporary, destination); strcat(temporary, ".part");
        if (maage_storage_clone(source, temporary, maage_assets_cancelled, assets) &&
            maage_assets_file(assets, temporary, hash, size)) result = maage_storage_replace(temporary, destination);
        maage_storage_remove(temporary);
        if (result) maage_storage_remove(source);
    }
    if (result) {
        maage_log_info("maage.launcher.assets", "Migration complete: %s", destination);
        maage_storage_remove_directory(parent);
        maage_assets_lock(assets); assets->progress.needs_setup = 1; maage_assets_unlock(assets);
    }
    return result;
}
/* Remove only recognized obsolete cache paths after the new installation is complete. */
static void maage_assets_cleanup_legacy(maage_assets *assets, const char *relative)
{
    char path[MAAGE_PATH_CAPACITY], parent[MAAGE_PATH_CAPACITY], *separator;
    if (!maage_storage_join(path, sizeof(path), assets->root, relative) || strlen(path) + 6 >= sizeof(path)) return;
    strcpy(parent, path); separator = strrchr(parent, '/');
    if (!separator) return;
    *separator = 0;
    if (!maage_storage_directory_exists(parent)) return;
    maage_storage_remove(path);
    strcat(path, ".part"); maage_storage_remove(path);
    maage_storage_remove_directory(parent);
}
static int maage_assets_logical_path(maage_assets *assets, const char *name, char path[MAAGE_PATH_CAPACITY])
{
    char parent[MAAGE_PATH_CAPACITY], *separator;
    if (!maage_minecraft_asset_path(assets->root, name, path, MAAGE_PATH_CAPACITY)) return 0;
    strcpy(parent, path); separator = strrchr(parent, '/');
    if (!separator) return 0;
    *separator = 0;
    return maage_storage_directories(parent);
}
typedef struct maage_assets_client_context {
    maage_assets *assets;
    struct json_object *index_objects, *records;
} maage_assets_client_context;
static int maage_assets_client_cancel(void *context)
{ return maage_assets_cancelled(((maage_assets_client_context *)context)->assets); }
static void maage_assets_client_bytes(void *context, double bytes)
{
    maage_assets *assets = (maage_assets *)context;
    maage_assets_lock(assets); assets->progress.client_downloaded_bytes += bytes; maage_assets_unlock(assets);
}
static int maage_assets_client_collision(void *context, const char *name, const char *hash, double size, char error[512])
{
    maage_assets_client_context *client = (maage_assets_client_context *)context;
    struct json_object_iter iterator;
    const char *a, *b, *expected;
    double indexed_size;
    int left, right;
    json_object_object_foreachC(client->index_objects, iterator) {
        a = name; b = iterator.key;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
        left = (unsigned char)*a; right = (unsigned char)*b;
        if (!left && !right) {
            expected = maage_assets_string(iterator.val, "hash");
            if (!strcmp(name, iterator.key) && expected && !strcmp(hash, expected) &&
                maage_assets_size(iterator.val, MAAGE_OBJECT_LIMIT, &indexed_size) && size == indexed_size) {
                maage_log_info("maage.launcher.assets", "Identical resource shared by index and client JAR: '%s' (SHA-1 %s)", name, hash); continue;
            }
            strcpy(error, "Asset-index and client-JAR resources conflict at the same path. Setup stopped without overwriting that resource."); return 0;
        }
        if ((!left && right == '/') || (!right && left == '/')) {
            strcpy(error, "Asset-index and client-JAR resource paths conflict as files/directories. Setup stopped before reconstruction."); return 0;
        }
    }
    return 1;
}
static void maage_assets_client_progress(void *context, maage_client_phase phase,
    unsigned long done, unsigned long total, const char *name, int changed)
{
    maage_assets *assets = ((maage_assets_client_context *)context)->assets;
    unsigned long objects, logical;
    maage_assets_lock(assets);
    assets->progress.client_resource_count = total;
    if (phase == MAAGE_CLIENT_INSPECT) assets->progress.client_resources_checked = done;
    else if (phase == MAAGE_CLIENT_RECONSTRUCT) assets->progress.client_resources_processed = done;
    if (changed) assets->progress.needs_setup = 1;
    strncpy(assets->progress.activity, name, sizeof(assets->progress.activity) - 1);
    objects = assets->progress.verified_objects; logical = assets->progress.reconstructed_files;
    maage_assets_unlock(assets);
    maage_assets_count(assets, objects, logical);
}
static int maage_assets_client_record(void *context, const char *name, const char *archive_name, const char *hash, double size, const char *fingerprint)
{
    struct json_object *record = json_object_new_object();
    maage_assets_client_context *client = (maage_assets_client_context *)context;
    if (!record) return 0;
    json_object_object_add(record, "archive_path", json_object_new_string(archive_name));
    json_object_object_add(record, "sha1", json_object_new_string(hash));
    json_object_object_add(record, "size", json_object_new_double(size));
    json_object_object_add(record, "fingerprint", json_object_new_string(fingerprint));
    return json_object_object_add(client->records, name, record) == 0;
}
static int maage_assets_client_path_owned(struct json_object *records, const char *name)
{
    struct json_object_iter iterator;
    const char *a, *b;
    json_object_object_foreachC(records, iterator) {
        a = name; b = iterator.key;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
        if (!*a && !*b) return 1;
    }
    return 0;
}
/* Only remove old names whose verified replacement exists. Unknown contents and
 * paths still owned by the index remain untouched; prune empty directories only. */
static void maage_assets_client_cleanup(maage_assets_client_context *client)
{
    struct json_object_iter iterator;
    char path[MAAGE_PATH_CAPACITY], parent[MAAGE_PATH_CAPACITY], boundary[MAAGE_PATH_CAPACITY], *separator;
    const char *archive_name, *legacy, *hash;
    unsigned candidate;
    if (!maage_storage_join(boundary, sizeof(boundary), client->assets->root, "minecraft/assets/reconstructed")) return;
    json_object_object_foreachC(client->records, iterator) {
        archive_name = maage_assets_string(iterator.val, "archive_path"); hash = maage_assets_string(iterator.val, "sha1");
        if (!archive_name || strncmp(archive_name, "assets/", sizeof("assets/") - 1) || !maage_assets_hash(hash)) continue;
        for (candidate = 0; candidate < 2; ++candidate) {
            if (maage_assets_cancelled(client->assets)) return;
            legacy = archive_name;
            if (candidate) {
                if (strncmp(legacy, "assets/minecraft/", sizeof("assets/minecraft/") - 1)) continue;
                legacy += sizeof("assets/minecraft/") - 1;
            }
            if (!strcmp(legacy, iterator.key) || maage_assets_client_path_owned(client->index_objects, legacy) ||
                maage_assets_client_path_owned(client->records, legacy) ||
                !maage_minecraft_asset_path(client->assets->root, legacy, path, sizeof(path))) continue;
            strcpy(parent, path); separator = strrchr(parent, '/'); if (!separator) continue; *separator = 0;
            if (!maage_storage_directory_exists(parent) || !maage_assets_file(client->assets, path, hash, -1)) continue;
            if (!maage_storage_remove(path)) { maage_log_warn("maage.launcher.assets", "Cannot remove migrated client resource: %s", path); continue; }
            maage_log_info("maage.launcher.assets", "Removed verified obsolete client resource: %s", path);
            while (strlen(parent) > strlen(boundary)) {
                maage_storage_remove_directory(parent);
                separator = strrchr(parent, '/'); if (!separator) break; *separator = 0;
            }
        }
    }
}
static int maage_assets_install(maage_assets *assets, char error[512])
{
    char metadata_path[MAAGE_PATH_CAPACITY], index_path[MAAGE_PATH_CAPACITY], state_path[MAAGE_PATH_CAPACITY];
    char path[MAAGE_PATH_CAPACITY], manifest_path[MAAGE_PATH_CAPACITY], metadata_hash[41], index_hash[41];
    char destinations[4][MAAGE_PATH_CAPACITY], urls[4][160], fingerprint[128];
    char legacy_state_path[MAAGE_PATH_CAPACITY], staging[MAAGE_PATH_CAPACITY] = "";
    char destination[MAAGE_PATH_CAPACITY], relative[128], client_path[MAAGE_PATH_CAPACITY], client_hash[41];
    const char *directories[] = { "minecraft/assets/indexes", "minecraft/assets/objects", "minecraft/assets/reconstructed", "minecraft/assets/.staging", "minecraft/metadata", "minecraft/cache", "config", "logs" };
    const char *text, *url, *expected, *cached, *client_url;
    struct json_object *manifest = NULL, *metadata = NULL, *index = NULL, *state = NULL, *receipt = NULL;
    struct json_object *versions, *version, *reference, *objects, *value, *saved = NULL, *new_saved = NULL;
    struct json_object *saved_logical = NULL, *new_logical = NULL, *downloads, *client_reference;
    maage_client_resources *client_resources = NULL;
    maage_client_observer client_observer;
    maage_assets_client_context client_context;
    struct json_object_iter iterator;
    maage_storage_lock *lock = NULL;
    maage_asset_object *list = NULL, key;
    maage_logical_asset *logical = NULL;
    maage_download_request requests[4];
    maage_download_observer observer;
    maage_setup_progress progress;
    unsigned long count = 0, unique = 0, i, j, completed = 0, reconstructed = 0, batch_indices[4];
    unsigned int batch;
    double size, index_size = -1, total = 0, client_size;
    int ok = 0, trusted = 0, same, cloned, completed_installation = 0, legacy_objects;
    maage_assets_stage(assets, MAAGE_SETUP_PREPARING, "Preparing application directories");
    if (!maage_storage_data_path(assets->root, sizeof(assets->root)) || strlen(assets->root) > MAAGE_PATH_CAPACITY - 160 || !maage_storage_directories(assets->root)) {
        strcpy(error, "Cannot create the per-user MaaGE data directory. Check permissions and your home directory."); goto cleanup;
    }
    maage_log_info("maage.launcher.assets", "%s in %s", assets->repair ? "Repairing Minecraft 1.7.10 resources" : "Assessing Minecraft 1.7.10 resources", assets->root);
    maage_storage_join(path, sizeof(path), assets->root, "config");
    if (!maage_storage_directories(path)) goto storage_error;
    maage_storage_join(path, sizeof(path), assets->root, "config/assets.lock");
    lock = maage_storage_acquire(path);
    if (!lock) { strcpy(error, "Another Launcher may be preparing assets. Close it and Retry; check data-directory permissions if it is already closed."); goto cleanup; }
    for (i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
        maage_storage_join(path, sizeof(path), assets->root, directories[i]);
        if (!maage_storage_directories(path)) goto storage_error;
        maage_log_debug("maage.launcher.assets", "Directory ready: %s", path);
    }
    maage_storage_join(metadata_path, sizeof(metadata_path), assets->root, "minecraft/metadata/1.7.10.json");
    maage_storage_join(index_path, sizeof(index_path), assets->root, "minecraft/assets/indexes/1.7.10.json");
    maage_storage_join(state_path, sizeof(state_path), assets->root, "minecraft/metadata/assets-state.json");
    maage_storage_join(legacy_state_path, sizeof(legacy_state_path), assets->root, "config/assets-state.json");
    maage_storage_join(manifest_path, sizeof(manifest_path), assets->root, "minecraft/metadata/version-manifest.json");
    maage_storage_join(staging, sizeof(staging), assets->root, "minecraft/assets/.staging/reconstruct.part");
    maage_storage_join(client_path, sizeof(client_path), assets->root, "minecraft/cache/client-1.7.10.jar");
    if (!maage_storage_remove(staging)) goto storage_error;
    /* All generated temporary names are predictable under the installation lock. */
    {
        const char *files[4];
        files[0] = metadata_path; files[1] = index_path; files[2] = state_path; files[3] = manifest_path;
        for (i = 0; i < 4; ++i) { strcpy(path, files[i]); strcat(path, ".part"); if (!maage_storage_remove(path)) goto storage_error; }
    }
    maage_assets_lock(assets); assets->progress.directories_ready = 1; maage_assets_unlock(assets);
    maage_storage_join(path, sizeof(path), assets->root, "assets/objects");
    legacy_objects = maage_storage_directory_exists(path);
    maage_assets_stage(assets, MAAGE_SETUP_METADATA, "Checking Minecraft 1.7.10 metadata");
    state = maage_assets_json(state_path);
    if (!state) state = maage_assets_json(legacy_state_path);
    if (state && json_object_object_get_ex(state, "complete", &value))
        completed_installation = json_object_is_type(value, json_type_boolean) && json_object_get_boolean(value);
    text = maage_assets_string(state, "metadata_sha1");
    if (maage_assets_hash(text) && (maage_assets_file(assets, metadata_path, text, -1) ||
        maage_assets_reuse(assets, metadata_path, "versions/1.7.10/metadata.json", text, -1))) {
        strcpy(metadata_hash, text); metadata = maage_assets_json(metadata_path);
        text = maage_assets_string(metadata, "id");
        trusted = text && !strcmp(text, "1.7.10");
    }
    if (!trusted) {
        maage_assets_lock(assets); assets->progress.needs_setup = 1; maage_assets_unlock(assets);
        if (!maage_assets_download(assets, MAAGE_MANIFEST, manifest_path, NULL, -1, error)) goto cleanup;
        manifest = maage_assets_json(manifest_path);
        if (!manifest || !json_object_object_get_ex(manifest, "versions", &versions) || !json_object_is_type(versions, json_type_array) || json_object_array_length(versions) > 100000) goto metadata_error;
        version = NULL;
        for (i = 0; i < (unsigned long)json_object_array_length(versions); ++i) {
            value = json_object_array_get_idx(versions, (size_t)i);
            text = maage_assets_string(value, "id");
            if (text && !strcmp(text, "1.7.10")) { version = value; break; }
        }
        url = maage_assets_string(version, "url"); expected = maage_assets_string(version, "sha1");
        if (!maage_assets_official(url) || !maage_assets_hash(expected)) goto metadata_error;
        strcpy(metadata_hash, expected);
        if (!maage_assets_file(assets, metadata_path, metadata_hash, -1) &&
            !maage_assets_reuse(assets, metadata_path, "versions/1.7.10/metadata.json", metadata_hash, -1) && !maage_assets_download(assets, url, metadata_path, metadata_hash, -1, error)) goto cleanup;
        if (metadata) json_object_put(metadata);
        metadata = maage_assets_json(metadata_path);
    }
    text = maage_assets_string(metadata, "id");
    if (!text || strcmp(text, "1.7.10") || !json_object_object_get_ex(metadata, "assetIndex", &reference)) goto metadata_error;
    expected = maage_assets_string(reference, "sha1"); url = maage_assets_string(reference, "url");
    if (!maage_assets_hash(expected) || !maage_assets_official(url) || !maage_assets_size(reference, MAAGE_METADATA_LIMIT, &index_size)) goto metadata_error;
    strcpy(index_hash, expected);
    if (!maage_assets_file(assets, index_path, index_hash, index_size) &&
        !maage_assets_reuse(assets, index_path, "assets/indexes/1.7.10.json", index_hash, index_size)) {
        maage_assets_lock(assets); assets->progress.needs_setup = 1; maage_assets_unlock(assets);
        if (!maage_assets_download(assets, url, index_path, index_hash, index_size, error)) goto cleanup;
    }
    if (!json_object_object_get_ex(metadata, "downloads", &downloads) ||
        !json_object_object_get_ex(downloads, "client", &client_reference)) goto metadata_error;
    client_url = maage_assets_string(client_reference, "url"); expected = maage_assets_string(client_reference, "sha1");
    if (!client_url || (strncmp(client_url, "https://piston-data.mojang.com/", strlen("https://piston-data.mojang.com/")) &&
        strncmp(client_url, "https://launcher.mojang.com/", strlen("https://launcher.mojang.com/"))) || strlen(client_url) > 2048 ||
        strchr(client_url, '\\') || strchr(client_url, '\r') || strchr(client_url, '\n') ||
        !maage_assets_hash(expected) || !maage_assets_size(client_reference, MAAGE_OBJECT_LIMIT, &client_size) || client_size <= 0) goto metadata_error;
    strcpy(client_hash, expected);
    index = maage_assets_json(index_path);
    if (!index || !json_object_object_get_ex(index, "objects", &objects) || !json_object_is_type(objects, json_type_object)) goto metadata_error;
    count = (unsigned long)json_object_object_length(objects);
    if (!count || count > 100000) goto metadata_error;
    list = (maage_asset_object *)calloc((size_t)count, sizeof(*list));
    logical = (maage_logical_asset *)calloc((size_t)count, sizeof(*logical));
    if (!list || !logical) goto memory_error;
    i = 0;
    json_object_object_foreachC(objects, iterator) {
        if (!maage_minecraft_asset_path(assets->root, iterator.key, path, sizeof(path))) {
            strcpy(error, "The asset index contains an unsafe or unsupported logical resource path."); goto cleanup;
        }
        logical[i].name = list[i].name = iterator.key;
        text = maage_assets_string(iterator.val, "hash");
        if (!maage_assets_hash(text) || !maage_assets_size(iterator.val, MAAGE_OBJECT_LIMIT, &size)) goto metadata_error;
        maage_log_debug("maage.launcher.assets", "Indexed '%s': SHA-1 %s, %.0f bytes", iterator.key, text, size);
        strcpy(list[i].hash, text); strcpy(logical[i].hash, text); list[i].size = size; ++i;
    }
    qsort(list, (size_t)count, sizeof(*list), maage_assets_compare);
    for (i = 0; i < count; ++i) {
        if (unique && !strcmp(list[unique - 1].hash, list[i].hash)) {
            if (list[unique - 1].size != list[i].size) goto metadata_error;
        } else { list[unique++] = list[i]; total += list[i].size; }
    }
    maage_log_info("maage.launcher.assets", "Index %s: %lu logical resources, %lu unique objects, %.0f object bytes", index_path, count, unique, total);
    if (state) {
        text = maage_assets_string(state, "index_sha1");
        if (text && !strcmp(text, index_hash)) {
            json_object_object_get_ex(state, "verified", &saved);
            json_object_object_get_ex(state, "reconstructed", &saved_logical);
        }
    }
    for (i = 0; i < count; ++i) {
        strcpy(key.hash, logical[i].hash);
        logical[i].object = (maage_asset_object *)bsearch(&key, list, (size_t)unique, sizeof(*list), maage_assets_compare);
        if (!logical[i].object) goto metadata_error;
    }
    receipt = json_object_new_object(); new_saved = json_object_new_object();
    if (!receipt || !new_saved) goto memory_error;
    json_object_object_add(receipt, "metadata_sha1", json_object_new_string(metadata_hash));
    json_object_object_add(receipt, "index_sha1", json_object_new_string(index_hash));
    json_object_object_add(receipt, "client_jar_sha1", json_object_new_string(client_hash));
    json_object_object_add(receipt, "client_resources", json_object_new_object());
    json_object_object_get_ex(receipt, "client_resources", &client_context.records);
    if (!client_context.records) goto memory_error;
    json_object_object_add(receipt, "complete", json_object_new_boolean(0));
    json_object_object_add(receipt, "verified", new_saved);
    new_saved = NULL;
    json_object_object_get_ex(receipt, "verified", &new_saved);
    json_object_object_add(receipt, "reconstructed", json_object_new_object());
    json_object_object_get_ex(receipt, "reconstructed", &new_logical);
    if (!new_logical) goto memory_error;
    /* A checkpoint authenticates cached metadata even if download is interrupted. */
    if (!maage_assets_write_json(state_path, receipt)) goto storage_error;
    maage_assets_lock(assets);
    assets->progress.metadata_ready = 1; assets->progress.total_objects = unique;
    assets->progress.total_logical_assets = count;
    assets->progress.total_bytes = total; assets->progress.client_total_bytes = client_size; assets->progress.percent = -1;
    if (assets->repair || !completed_installation) assets->progress.needs_setup = 1;
    maage_assets_unlock(assets);
    maage_assets_stage(assets, MAAGE_SETUP_VERIFYING, assets->repair ? "Repairing assets: verifying every resource" : "Checking installed resources");
    for (i = 0; i < unique; ++i) {
        if (maage_assets_cancelled(assets)) goto cleanup;
        if (!maage_assets_object_path(assets, list[i].hash, path)) goto storage_error;
        {
            char temporary[MAAGE_PATH_CAPACITY];
            strcpy(temporary, path); strcat(temporary, ".part");
            if (!maage_storage_remove(temporary)) goto storage_error;
        }
        same = 0;
        if (maage_storage_stat(path, &size, fingerprint) && size == list[i].size) {
            cached = maage_assets_string(saved, list[i].hash);
            same = completed_installation && !assets->repair && cached && !strcmp(cached, fingerprint);
            if (!same) same = maage_assets_file(assets, path, list[i].hash, list[i].size);
        }
        if (!same && legacy_objects) {
            sprintf(relative, "assets/objects/%.2s/%s", list[i].hash, list[i].hash);
            same = maage_assets_reuse(assets, path, relative, list[i].hash, list[i].size);
            if (same) list[i].repaired = 1;
            if (same && !maage_storage_stat(path, &size, fingerprint)) goto storage_error;
        }
        if (same) {
            maage_log_debug("maage.launcher.assets", "Using verified cached object for '%s': %s (SHA-1 %s)", list[i].name, path, list[i].hash);
            maage_assets_lock(assets); ++assets->progress.valid_cached_objects; maage_assets_unlock(assets);
            list[i].verified = 1; strcpy(list[i].fingerprint, fingerprint); ++completed;
            json_object_object_add(new_saved, list[i].hash, json_object_new_string(fingerprint));
        } else {
            maage_log_info("maage.launcher.assets", "Missing or invalid object for '%s': %s (SHA-1 %s)", list[i].name, path, list[i].hash);
            maage_assets_lock(assets); assets->progress.needs_setup = 1;
            ++assets->progress.missing_objects; maage_assets_unlock(assets);
        }
        maage_assets_count(assets, completed, 0);
    }
    if (!maage_assets_write_json(state_path, receipt)) goto storage_error;
    observer.cancelled = maage_assets_cancelled; observer.bytes = maage_assets_bytes; observer.context = assets;
    if (completed < unique) maage_assets_stage(assets, MAAGE_SETUP_DOWNLOADING, "Downloading and verifying resources");
    for (i = 0; i < unique; ) {
        batch = 0;
        while (i < unique && batch < 4) {
            j = i++;
            if (list[j].verified) continue;
            if (!maage_assets_object_path(assets, list[j].hash, destinations[batch])) goto storage_error;
            sprintf(urls[batch], "https://resources.download.minecraft.net/%.2s/%s", list[j].hash, list[j].hash);
            requests[batch].url = urls[batch]; requests[batch].destination = destinations[batch];
            requests[batch].sha1 = list[j].hash; requests[batch].size = list[j].size;
            maage_log_info("maage.launcher.assets", "Queueing download for '%s': SHA-1 %s, %.0f bytes -> %s", list[j].name, list[j].hash, list[j].size, destinations[batch]);
            requests[batch].limit = list[j].size; batch_indices[batch] = j; ++batch;
        }
        if (maage_assets_cancelled(assets)) goto cleanup;
        if (!batch) continue;
        if (!maage_download_batch(requests, batch, &observer, error)) goto cleanup;
        for (j = 0; j < batch; ++j) {
            if (!maage_storage_stat(destinations[j], &size, fingerprint) || size != list[batch_indices[j]].size) goto storage_error;
            list[batch_indices[j]].verified = 1; list[batch_indices[j]].repaired = 1; strcpy(list[batch_indices[j]].fingerprint, fingerprint);
            json_object_object_add(new_saved, list[batch_indices[j]].hash, json_object_new_string(fingerprint));
            ++completed;
            maage_assets_lock(assets); ++assets->progress.downloaded_objects;
            --assets->progress.missing_objects; maage_assets_unlock(assets);
        }
        maage_assets_count(assets, completed, 0);
        if (!maage_assets_write_json(state_path, receipt)) goto storage_error;
    }
    maage_assets_lock(assets); assets->progress.objects_ready = 1; maage_assets_unlock(assets);
    maage_assets_stage(assets, MAAGE_SETUP_CLIENT_ARCHIVE, "Checking Minecraft 1.7.10 client resource archive");
    if (!maage_assets_file(assets, client_path, client_hash, client_size)) {
        if (maage_assets_cancelled(assets)) goto cleanup;
        maage_assets_lock(assets); assets->progress.needs_setup = 1; maage_assets_unlock(assets);
        requests[0].url = client_url; requests[0].destination = client_path;
        requests[0].sha1 = client_hash; requests[0].size = client_size; requests[0].limit = client_size;
        observer.bytes = maage_assets_client_bytes;
        maage_assets_stage(assets, MAAGE_SETUP_CLIENT_ARCHIVE, "Downloading and verifying Minecraft client resources");
        if (!maage_download_batch(requests, 1, &observer, error)) goto cleanup;
        if (!maage_assets_file(assets, client_path, client_hash, client_size)) {
            strcpy(error, "The downloaded client JAR failed SHA-1 or size verification. Retry to reacquire it."); goto cleanup;
        }
    }
    maage_assets_lock(assets); assets->progress.client_archive_ready = 1; maage_assets_unlock(assets);
    client_context.assets = assets; client_context.index_objects = objects;
    client_observer.cancelled = maage_assets_client_cancel; client_observer.collision = maage_assets_client_collision;
    client_observer.progress = maage_assets_client_progress; client_observer.record = maage_assets_client_record; client_observer.context = &client_context;
    maage_assets_stage(assets, MAAGE_SETUP_CLIENT_EXTRACT, "Inspecting and verifying client JAR resources");
    client_resources = maage_client_resources_open(client_path, assets->root, &client_observer, error);
    if (!client_resources) goto cleanup;
    maage_assets_stage(assets, MAAGE_SETUP_RECONSTRUCTING, "Reconstructing Minecraft resources");
    for (i = 0; i < count; ++i) {
        maage_asset_object *object = logical[i].object;
        if (maage_assets_cancelled(assets)) goto cleanup;
        if (!maage_assets_object_path(assets, object->hash, path) ||
            !maage_assets_logical_path(assets, logical[i].name, destination)) goto storage_error;
        if (!maage_storage_stat(path, &size, fingerprint) || size != object->size) goto changed_error;
        if (strcmp(fingerprint, object->fingerprint)) {
            if (!maage_assets_file(assets, path, object->hash, object->size)) goto changed_error;
            strcpy(object->fingerprint, fingerprint);
        }
        same = 0;
        if (maage_storage_stat(destination, &size, fingerprint) && size == object->size) {
            cached = maage_assets_string(saved_logical, logical[i].name);
            same = maage_storage_same_file(path, destination) ||
                (completed_installation && !assets->repair && !object->repaired && cached && !strcmp(cached, fingerprint));
            if (!same) same = maage_assets_file(assets, destination, object->hash, object->size);
        }
        if (same) {
            maage_log_debug("maage.launcher.assets", "Using verified reconstructed resource '%s': %s (object %s)", logical[i].name, destination, object->hash);
            logical[i].verified = 1; ++reconstructed;
            strcpy(logical[i].fingerprint, fingerprint);
            json_object_object_add(new_logical, logical[i].name, json_object_new_string(fingerprint));
        } else {
            maage_log_info("maage.launcher.assets", "Missing or invalid reconstructed resource '%s': %s", logical[i].name, destination);
            maage_assets_lock(assets); assets->progress.needs_setup = 1;
            ++assets->progress.missing_reconstructed_files; maage_assets_unlock(assets);
        }
        maage_assets_count(assets, completed, reconstructed);
    }
    for (i = 0; i < count; ++i) {
        maage_asset_object *object = logical[i].object;
        if (maage_assets_cancelled(assets)) goto cleanup;
        if (logical[i].verified) continue;
        if (!maage_assets_object_path(assets, object->hash, path) ||
            !maage_assets_logical_path(assets, logical[i].name, destination)) goto storage_error;
        if (!maage_storage_stat(path, &size, fingerprint) || size != object->size) goto changed_error;
        if (strcmp(fingerprint, object->fingerprint) && !maage_assets_file(assets, path, object->hash, object->size)) goto changed_error;
        maage_log_info("maage.launcher.assets", "Reconstructing '%s': %s -> %s", logical[i].name, path, destination);
        cloned = maage_storage_clone(path, staging, maage_assets_cancelled, assets);
        if (!cloned || !maage_assets_file(assets, staging, object->hash, object->size) ||
            (cloned == 1 && !maage_storage_same_file(path, staging)) ||
            maage_assets_cancelled(assets) || !maage_storage_replace(staging, destination)) goto storage_error;
        maage_log_info("maage.launcher.assets", "Reconstructed '%s' via %s: %s (SHA-1 %s, %.0f bytes)", logical[i].name, cloned == 1 ? "hard link" : "copy", destination, object->hash, object->size);
        /* Link creation/promotion changes ctime on the verified source object. */
        if (!maage_storage_stat(path, &size, object->fingerprint) || size != object->size ||
            !maage_storage_stat(destination, &size, logical[i].fingerprint) || size != object->size) goto changed_error;
        logical[i].verified = 1; ++reconstructed;
        json_object_object_add(new_logical, logical[i].name, json_object_new_string(logical[i].fingerprint));
        json_object_object_add(new_saved, object->hash, json_object_new_string(object->fingerprint));
        maage_assets_lock(assets); --assets->progress.missing_reconstructed_files; maage_assets_unlock(assets);
        maage_assets_count(assets, completed, reconstructed);
        if (reconstructed % 32 == 0 && !maage_assets_write_json(state_path, receipt)) goto storage_error;
    }
    maage_assets_stage(assets, MAAGE_SETUP_CLIENT_EXTRACT, "Extracting and reconstructing client JAR resources");
    if (!maage_client_resources_reconstruct(client_resources, assets->root, staging, &client_observer, error)) goto cleanup;
    maage_assets_lock(assets); assets->progress.client_resources_ready = 1; maage_assets_unlock(assets);
    maage_assets_stage(assets, MAAGE_SETUP_VERIFYING, "Completing setup: checking both resource forms");
    for (i = 0; i < unique; ++i) {
        if (maage_assets_cancelled(assets)) goto cleanup;
        if (!maage_assets_object_path(assets, list[i].hash, path) || !maage_storage_stat(path, &size, fingerprint) ||
            !list[i].verified || size != list[i].size) goto changed_error;
        if (strcmp(list[i].fingerprint, fingerprint) && !maage_assets_file(assets, path, list[i].hash, list[i].size)) goto changed_error;
        strcpy(list[i].fingerprint, fingerprint);
        json_object_object_add(new_saved, list[i].hash, json_object_new_string(fingerprint));
    }
    for (i = 0; i < count; ++i) {
        if (maage_assets_cancelled(assets)) goto cleanup;
        if (!logical[i].verified || !maage_assets_logical_path(assets, logical[i].name, destination) ||
            !maage_storage_stat(destination, &size, fingerprint) || size != logical[i].object->size) goto changed_error;
        if (strcmp(logical[i].fingerprint, fingerprint)) {
            if (!maage_assets_object_path(assets, logical[i].hash, path) ||
                (!maage_storage_same_file(path, destination) && !maage_assets_file(assets, destination, logical[i].hash, size))) goto changed_error;
        }
        json_object_object_add(new_logical, logical[i].name, json_object_new_string(fingerprint));
    }
    if (!maage_assets_file(assets, client_path, client_hash, client_size)) goto changed_error;
    maage_assets_client_cleanup(&client_context);
    if (!maage_client_resources_check(client_resources, assets->root, &client_observer, error)) goto cleanup;
    maage_assets_lock(assets); assets->progress.reconstruction_ready = 1; maage_assets_unlock(assets);
    json_object_object_add(receipt, "complete", json_object_new_boolean(1));
    if (!maage_assets_write_json(state_path, receipt)) goto storage_error;
    if (legacy_objects) {
        for (i = 0; i < unique; ++i) {
            sprintf(relative, "assets/objects/%.2s/%s", list[i].hash, list[i].hash);
            maage_assets_cleanup_legacy(assets, relative);
        }
    }
    maage_assets_cleanup_legacy(assets, "versions/1.7.10/metadata.json");
    maage_assets_cleanup_legacy(assets, "assets/indexes/1.7.10.json");
    maage_assets_cleanup_legacy(assets, "config/assets-state.json");
    maage_assets_cleanup_legacy(assets, "config/version-manifest.json");
    maage_storage_join(path, sizeof(path), assets->root, "versions"); maage_storage_remove_directory(path);
    maage_storage_join(path, sizeof(path), assets->root, "assets/objects"); maage_storage_remove_directory(path);
    maage_storage_join(path, sizeof(path), assets->root, "assets"); maage_storage_remove_directory(path);
    maage_assets_snapshot(assets, &progress);
    maage_log_info("maage.launcher.assets", "Resources verified: %lu logical files, %lu unique objects; %lu reused, %lu downloaded, %.0f bytes transferred", count, unique, progress.valid_cached_objects, progress.downloaded_objects, progress.downloaded_bytes);
    maage_log_info("maage.launcher.assets", "Client resources verified: %lu files from %s; %.0f JAR bytes transferred", progress.client_resource_count, client_path, progress.client_downloaded_bytes);
    ok = 1; goto cleanup;
changed_error:
    strcpy(error, "A Minecraft resource changed during setup. Retry to verify and repair both resource forms."); goto cleanup;
metadata_error:
    strcpy(error, "Minecraft 1.7.10 metadata is missing or invalid. Check your connection and Retry."); goto cleanup;
storage_error:
    strcpy(error, "Cannot read or write MaaGE resources. Check data-directory permissions and free disk space, then Retry."); goto cleanup;
memory_error:
    strcpy(error, "Not enough memory to prepare resources. Close other applications and Retry.");
cleanup:
    maage_client_resources_close(client_resources);
    if (manifest) json_object_put(manifest);
    if (metadata) json_object_put(metadata);
    if (index) json_object_put(index);
    if (state) json_object_put(state);
    if (receipt) json_object_put(receipt);
    else if (new_saved) json_object_put(new_saved);
    if (staging[0] && !maage_storage_remove(staging))
        maage_log_warn("maage.launcher.assets", "Cannot remove reconstruction staging file: %s", staging);
    free(logical); free(list); maage_storage_release(lock);
    return ok;
}
#ifdef _WIN32
static DWORD WINAPI maage_assets_worker(LPVOID context)
#else
static void *maage_assets_worker(void *context)
#endif
{
    maage_assets *assets = (maage_assets *)context;
    char error[512] = "";
    int ok = 0;
    if (maage_access_allowed(assets->access)) ok = maage_assets_install(assets, error);
    if (!maage_access_allowed(assets->access)) {
        ok = 0; strcpy(error, "Minecraft access is no longer verified. Choose Verify Access and verify again in MaaGE. Existing resources are preserved.");
    }
    maage_assets_lock(assets);
    if (assets->progress.cancellation_requested) {
        assets->progress.stage = MAAGE_SETUP_CANCELLED;
        strcpy(assets->progress.activity, "Setup cancelled. Verified resources are saved.");
    } else if (ok) {
        assets->progress.stage = MAAGE_SETUP_COMPLETE; assets->progress.percent = 100;
        strcpy(assets->progress.activity, "Resources are ready. You can continue to MaaGE.");
    } else {
        assets->progress.stage = MAAGE_SETUP_FAILED;
        strncpy(assets->progress.error, error, sizeof(assets->progress.error) - 1);
        strcpy(assets->progress.activity, "Setup could not finish");
    }
    assets->running = 0;
    maage_assets_unlock(assets);
    if (maage_assets_cancelled(assets))
        maage_log_info("maage.launcher.assets", "Asset operation cancelled; verified objects and reconstructed files are retained for resume.");
    else if (!ok) maage_log_error("maage.launcher.assets", "%s", error[0] ? error : "Asset operation failed");
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
maage_assets *maage_assets_create(maage_access *access)
{
    maage_assets *assets = (maage_assets *)calloc(1, sizeof(*assets));
    if (!assets) return NULL;
#ifdef _WIN32
    InitializeCriticalSection(&assets->mutex);
#else
    if (pthread_mutex_init(&assets->mutex, NULL)) { free(assets); return NULL; }
#endif
    assets->access = access; assets->progress.percent = -1;
    return assets;
}
static void maage_assets_join(maage_assets *assets)
{
    if (!assets->has_thread) return;
#ifdef _WIN32
    WaitForSingleObject(assets->thread, INFINITE); CloseHandle(assets->thread);
#else
    pthread_join(assets->thread, NULL);
#endif
    assets->has_thread = 0;
}
int maage_assets_start(maage_assets *assets, int repair)
{
    maage_assets_lock(assets);
    if (assets->running) { maage_assets_unlock(assets); return 0; }
    maage_assets_unlock(assets);
    maage_assets_join(assets);
    maage_assets_lock(assets);
    memset(&assets->progress, 0, sizeof(assets->progress));
    assets->progress.percent = -1; assets->progress.needs_setup = repair;
    assets->repair = repair;
    if (!maage_access_allowed(assets->access)) {
        assets->progress.stage = MAAGE_SETUP_FAILED;
        strcpy(assets->progress.error, "Verify Minecraft access before starting or repairing resources. Choose Verify Access and verify again.");
        maage_assets_unlock(assets); return 0;
    }
    assets->running = 1;
    maage_assets_unlock(assets);
#ifdef _WIN32
    assets->thread = CreateThread(NULL, 0, maage_assets_worker, assets, 0, NULL);
    assets->has_thread = assets->thread != NULL;
#else
    assets->has_thread = pthread_create(&assets->thread, NULL, maage_assets_worker, assets) == 0;
#endif
    if (!assets->has_thread) {
        maage_assets_lock(assets); assets->running = 0; assets->progress.stage = MAAGE_SETUP_FAILED;
        strcpy(assets->progress.error, "Cannot start the setup worker. Close other applications and Retry."); maage_assets_unlock(assets);
    }
    return assets->has_thread;
}
int maage_assets_access_allowed(maage_assets *assets)
{ return assets && maage_access_allowed(assets->access); }
int maage_assets_retry(maage_assets *assets)
{
    return maage_assets_start(assets, assets->repair);
}
void maage_assets_destroy(maage_assets *assets)
{
    if (!assets) return;
    maage_assets_cancel(assets); maage_assets_join(assets);
#ifdef _WIN32
    DeleteCriticalSection(&assets->mutex);
#else
    pthread_mutex_destroy(&assets->mutex);
#endif
    free(assets);
}
