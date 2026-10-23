#include "client_resources.h"
#include "maage/storage.h"
#include "maage/minecraft_assets.h"
#include "maage/log.h"
#include <zip.h>
#include "maage/crypto.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#define MAAGE_CLIENT_MAX_ENTRIES 100000
#define MAAGE_CLIENT_ENTRY_LIMIT 67108864.0
#define MAAGE_CLIENT_TOTAL_LIMIT 1073741824.0
typedef struct maage_client_entry {
    const char *name, *archive_name; /* Borrowed logical/source names. */
    zip_uint64_t index;
    double size;
    char hash[41], fingerprint[128];
} maage_client_entry;
struct maage_client_resources {
    zip_t *archive;
    maage_client_entry *entries;
    unsigned long count;
};
static int maage_client_compare_names(const char *a, const char *b)
{
    int left, right;
    do {
        left = tolower((unsigned char)*a++); right = tolower((unsigned char)*b++);
        if (left != right) return left - right;
    } while (left);
    return 0;
}
static int maage_client_compare(const void *a, const void *b)
{ return maage_client_compare_names(((const maage_client_entry *)a)->name, ((const maage_client_entry *)b)->name); }
static int maage_client_selected(const char *name)
{
    const char *namespace_end, *extension;
    const char *excluded[] = { ".class", ".java", ".jar", ".zip", ".exe", ".dll", ".so", ".dylib", ".jnilib", ".bat", ".cmd", ".ps1", ".sh", ".com", ".wasm" };
    size_t i;
    extension = strrchr(name, '.');
    for (i = 0; extension && i < sizeof(excluded) / sizeof(excluded[0]); ++i)
        if (!maage_client_compare_names(extension, excluded[i])) return 0;
    if (!strcmp(name, "pack.png") || !strcmp(name, "pack.mcmeta")) return 1;
    if (strncmp(name, "assets/", 7)) return 0;
    namespace_end = strchr(name + 7, '/');
    if (!namespace_end || namespace_end == name + 7 || !namespace_end[1]) return 0;
    return 1;
}
/* Reads to EOF so libzip verifies the ZIP entry's CRC, with explicit size and
 * expansion bounds. SHA-1 then binds reconstructed files to the verified JAR. */
static maage_status maage_client_stream(maage_client_resources *resources, maage_client_entry *entry,
    FILE *output, const maage_client_observer *observer, char hash[41])
{
    zip_file_t *file = zip_fopen_index(resources->archive, entry->index, 0);
    maage_crypto_hash *digest = NULL;
    unsigned char buffer[32768], bytes[20];
    unsigned int i;
    zip_int64_t received = -1;
    double total = 0;
    long native_code = 0;
    maage_status result = maage_crypto_hash_open(MAAGE_CRYPTO_SHA1, &digest);
    int ok = file && !maage_status_failed(result);
    while (ok && !observer->cancelled(observer->context)) {
        received = zip_fread(file, buffer, sizeof(buffer));
        if (received <= 0) break;
        total += (double)received;
        if (total > entry->size || total > MAAGE_CLIENT_ENTRY_LIMIT ||
            maage_status_failed(result = maage_crypto_hash_update(digest, buffer, (size_t)received)) ||
            (output && fwrite(buffer, 1, (size_t)received, output) != (size_t)received)) ok = 0;
    }
    if (observer->cancelled(observer->context) || received != 0 || total != entry->size) ok = 0;
    if (ok && maage_status_failed(result = maage_crypto_hash_finish(digest, bytes, sizeof(bytes)))) ok = 0;
    if (file && !ok) native_code = zip_error_code_zip(zip_file_get_error(file));
    if (file && zip_fclose(file)) ok = 0;
    maage_crypto_hash_destroy(digest);
    if (ok) { for (i = 0; i < sizeof(bytes); ++i) sprintf(hash + i * 2, "%02x", (unsigned)bytes[i]); hash[40] = 0; }
    if (!maage_status_failed(result)) result = maage_status_make(ok ? MAAGE_STATUS_SUCCESS : observer->cancelled(observer->context) ? MAAGE_STATUS_INTERRUPTED : MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VERIFY, 0);
    if (maage_status_failed(result)) maage_status_trace_record(observer->trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VERIFY, __FILE__, (unsigned long)__LINE__, native_code);
    return result;
}
maage_status maage_client_resources_open(const char *archive_path,
    const char *application_data, const maage_client_observer *observer, char error[512], maage_client_resources **output)
{
    maage_client_resources *resources = NULL;
    FILE *file;
    zip_source_t *source;
    zip_error_t zip_error;
    zip_stat_t st;
    zip_int64_t count, index;
    zip_uint8_t opsys;
    zip_uint32_t attributes, type;
    char logical[1025], destination[MAAGE_PATH_CAPACITY];
    const char *name;
    size_t length, i;
    double expanded = 0;
    int directory;
    maage_status result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_OPEN, 0);
    if (!output || !observer || !archive_path || !application_data || !error) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_OPEN, 0);
    *output = NULL;
    strcpy(error, "The client archive is invalid or cannot be opened. Repair the cached JAR and retry.");
    file = maage_storage_open(archive_path, "rb"); if (!file) return MAAGE_STATUS_PROPAGATE(observer->trace, maage_status_from_errno(errno ? errno : EIO, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_OPEN), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_OPEN);
    zip_error_init(&zip_error);
    source = zip_source_filep_create(file, 0, -1, &zip_error);
    if (!source) { fclose(file); zip_error_fini(&zip_error); return MAAGE_STATUS_PROPAGATE(observer->trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_OPEN); }
    resources = (maage_client_resources *)calloc(1, sizeof(*resources));
    if (!resources) { result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_CREATE, 0); zip_source_free(source); zip_error_fini(&zip_error); return MAAGE_STATUS_PROPAGATE(observer->trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_OPEN); }
    resources->archive = zip_open_from_source(source, ZIP_RDONLY | ZIP_CHECKCONS, &zip_error);
    zip_error_fini(&zip_error);
    if (!resources->archive) { zip_source_free(source); free(resources); return MAAGE_STATUS_PROPAGATE(observer->trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_OPEN); }
    count = zip_get_num_entries(resources->archive, 0);
    if (count <= 0 || count > MAAGE_CLIENT_MAX_ENTRIES) goto failed;
    resources->entries = (maage_client_entry *)calloc((size_t)count, sizeof(*resources->entries));
    if (!resources->entries) { result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_CREATE, 0); goto failed; }
    for (index = 0; index < count; ++index) {
        if (observer->cancelled(observer->context)) { result = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_OPEN, 0); goto failed; }
        name = zip_get_name(resources->archive, (zip_uint64_t)index, ZIP_FL_ENC_STRICT);
        if (!name || !(length = strlen(name)) || length >= sizeof(logical)) goto unsafe;
        directory = name[length - 1] == '/';
        memcpy(logical, name, length + 1); if (directory) logical[--length] = 0;
        /* Validate code/metadata entries too, although they will never be written. */
        if (!(!maage_status_failed(maage_minecraft_asset_path(application_data, logical, destination, sizeof(destination), observer->trace))) ||
            zip_file_get_external_attributes(resources->archive, (zip_uint64_t)index, 0, &opsys, &attributes)) goto unsafe;
        type = (attributes >> 16) & 0170000;
        if ((type && type != 0100000 && type != 0040000) ||
            (attributes & 0x400) || (type == 0040000 && !directory) || ((attributes & 0x10) && !directory)) goto unsafe;
        if (directory || !maage_client_selected(name)) continue;
        /* Stable cross-platform spelling: reject Unicode normalization aliases
         * rather than silently rename archive resources on case-folding disks. */
        for (i = 0; i < length; ++i) if ((unsigned char)name[i] >= 128) goto unsafe;
        zip_stat_init(&st);
        if (zip_stat_index(resources->archive, (zip_uint64_t)index, 0, &st) ||
            (st.valid & (ZIP_STAT_SIZE | ZIP_STAT_CRC | ZIP_STAT_COMP_METHOD | ZIP_STAT_ENCRYPTION_METHOD)) !=
                (ZIP_STAT_SIZE | ZIP_STAT_CRC | ZIP_STAT_COMP_METHOD | ZIP_STAT_ENCRYPTION_METHOD) ||
            st.encryption_method != ZIP_EM_NONE || (st.comp_method != ZIP_CM_STORE && st.comp_method != ZIP_CM_DEFLATE) ||
            (double)st.size > MAAGE_CLIENT_ENTRY_LIMIT) goto failed;
        expanded += (double)st.size;
        if (expanded > MAAGE_CLIENT_TOTAL_LIMIT) goto failed;
        resources->entries[resources->count].archive_name = name;
        if (!strncmp(name, "assets/", sizeof("assets/") - 1)) name += sizeof("assets/") - 1;
        if (!(!maage_status_failed(maage_minecraft_asset_path(application_data, name, destination, sizeof(destination), observer->trace)))) goto unsafe;
        resources->entries[resources->count].name = name;
        resources->entries[resources->count].index = (zip_uint64_t)index;
        resources->entries[resources->count++].size = (double)st.size;
    }
    if (!resources->count) { strcpy(error, "The client JAR contains no supported game resources."); goto failed; }
    qsort(resources->entries, resources->count, sizeof(*resources->entries), maage_client_compare);
    for (i = 1; i < resources->count; ++i) {
        if (!maage_client_compare_names(resources->entries[i - 1].name, resources->entries[i].name)) goto unsafe;
    }
    for (i = 0; i < resources->count; ++i) {
        const char *separator = resources->entries[i].name;
        maage_client_entry key;
        while ((separator = strchr(separator, '/')) != NULL) {
            length = (size_t)(separator - resources->entries[i].name);
            memcpy(logical, resources->entries[i].name, length); logical[length] = 0;
            key.name = logical;
            if (bsearch(&key, resources->entries, resources->count, sizeof(*resources->entries), maage_client_compare)) goto unsafe;
            ++separator;
        }
    }
    observer->progress(observer->context, MAAGE_CLIENT_INSPECT, 0, resources->count, "Checking client archive resource contents", 0);
    for (i = 0; i < resources->count; ++i) {
        maage_client_entry *entry = &resources->entries[i];
        result = maage_client_stream(resources, entry, NULL, observer, entry->hash);
        if (maage_status_failed(result)) goto failed;
        result = observer->collision(observer->context, entry->name, entry->hash, entry->size, error);
        if (maage_status_failed(result)) goto failed;
        maage_log_verbose("maage.launcher.assets", "Client resource '%s' -> '%s': SHA-1 %s, %.0f bytes", entry->archive_name, entry->name, entry->hash, entry->size);
        observer->progress(observer->context, MAAGE_CLIENT_INSPECT, (unsigned long)i + 1, resources->count, entry->name, 0);
    }
    maage_log_info("maage.launcher.assets", "Verified client archive: %lu applicable resources, %.0f expanded bytes", resources->count, expanded);
    error[0] = 0; *output = resources; return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_OPEN, 0);
unsafe:
    result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0);
    strcpy(error, "The client JAR contains an unsafe, aliased, linked, or unsupported resource path. No archive paths were reconstructed.");
failed:
    maage_client_resources_close(resources); return MAAGE_STATUS_PROPAGATE(observer->trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_OPEN);
}
static int maage_client_file_valid(const char *path, const maage_client_entry *entry,
    const maage_client_observer *observer, char fingerprint[128])
{
    char hash[41];
    double size;
    return (!maage_status_failed(maage_storage_stat(path, &size, fingerprint))) && size == entry->size &&
        (!maage_status_failed(maage_storage_sha1(path, hash, observer->cancelled, observer->context))) && !strcmp(hash, entry->hash);
}
maage_status maage_client_resources_reconstruct(maage_client_resources *resources,
    const char *application_data, const char *staging,
    const maage_client_observer *observer, char error[512])
{
    unsigned long i, j;
    char source_path[MAAGE_PATH_CAPACITY];
    char path[MAAGE_PATH_CAPACITY], parent[MAAGE_PATH_CAPACITY], hash[41], *separator;
    FILE *output;
    int ok, changed, cloned;
    maage_status result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VERIFY, 0);
    if (!resources || !observer || !application_data || !staging || !error) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD, 0);
    observer->progress(observer->context, MAAGE_CLIENT_RECONSTRUCT, 0, resources->count, "Reconstructing client JAR resources", 0);
    for (i = 0; i < resources->count; ++i) {
        maage_client_entry *entry = &resources->entries[i];
        if (observer->cancelled(observer->context)) goto failed;
        if (!(!maage_status_failed(maage_minecraft_asset_path(application_data, entry->name, path, sizeof(path), observer->trace)))) goto failed;
        strcpy(parent, path); separator = strrchr(parent, '/'); if (!separator) goto failed; *separator = 0;
        if (maage_status_failed(maage_storage_directories(parent))) goto failed;
        changed = !maage_client_file_valid(path, entry, observer, entry->fingerprint);
        if (changed) {
            if (maage_status_failed(maage_storage_remove(staging))) goto failed;
            cloned = 0;
            for (j = 0; !cloned && j < i; ++j) if (resources->entries[j].size == entry->size && !strcmp(resources->entries[j].hash, entry->hash)) {
                if ((!maage_status_failed(maage_minecraft_asset_path(application_data, resources->entries[j].name, source_path, sizeof(source_path), observer->trace))) &&
                    maage_client_file_valid(source_path, entry, observer, entry->fingerprint))
                    result = maage_storage_clone(source_path, staging, observer->cancelled, observer->context, &cloned, observer->trace);
                break;
            }
            output = cloned ? NULL : maage_storage_open(staging, "wb"); if (!cloned && !output) goto failed;
            maage_log_info("maage.launcher.assets", "Extracting client JAR resource '%s' -> %s (staging %s)", entry->archive_name, path, staging);
            if (cloned) { strcpy(hash, entry->hash); ok = 1; }
            else {
                result = maage_client_stream(resources, entry, output, observer, hash);
                ok = !maage_status_failed(result);
                if (fclose(output)) ok = 0;
            }
            if (!ok || strcmp(hash, entry->hash) || !maage_client_file_valid(staging, entry, observer, entry->fingerprint) ||
                observer->cancelled(observer->context) || maage_status_failed(maage_storage_replace(staging, path)) ||
                !maage_client_file_valid(path, entry, observer, entry->fingerprint)) goto failed;
            maage_log_info("maage.launcher.assets", "Reconstructed client resource '%s' via %s: %s (SHA-1 %s, %.0f bytes)", entry->name, cloned == 1 ? "hard link" : cloned ? "copy" : "ZIP extraction", path, entry->hash, entry->size);
        } else maage_log_verbose("maage.launcher.assets", "Using verified client resource '%s': %s", entry->name, path);
        result = observer->record(observer->context, entry->name, entry->archive_name, entry->hash, entry->size, entry->fingerprint);
        if (maage_status_failed(result)) goto failed;
        observer->progress(observer->context, MAAGE_CLIENT_RECONSTRUCT, i + 1, resources->count, entry->name, changed);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
failed:
    if (observer->cancelled(observer->context)) result = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD, 0);
    else if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD, 0);
    maage_storage_remove(staging);
    strcpy(error, observer->cancelled(observer->context) ? "Client resource reconstruction cancelled." : "Cannot safely reconstruct a client JAR resource. Check permissions, free space, and the archive; then retry."); return MAAGE_STATUS_PROPAGATE(observer->trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD);
}
maage_status maage_client_resources_check(maage_client_resources *resources,
    const char *application_data, const maage_client_observer *observer, char error[512])
{
    char path[MAAGE_PATH_CAPACITY], fingerprint[128];
    double size;
    unsigned long i;
    maage_status result;
    if (!resources || !observer || !application_data || !error) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VERIFY, 0);
    for (i = 0; i < resources->count; ++i) {
        maage_client_entry *entry = &resources->entries[i];
        if (observer->cancelled(observer->context)) return MAAGE_STATUS_PROPAGATE(observer->trace, maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VERIFY, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VERIFY);
        if (!(!maage_status_failed(maage_minecraft_asset_path(application_data, entry->name, path, sizeof(path), observer->trace))) ||
            maage_status_failed(maage_storage_stat(path, &size, fingerprint)) || size != entry->size ||
            (strcmp(fingerprint, entry->fingerprint) && !maage_client_file_valid(path, entry, observer, fingerprint))) {
            strcpy(error, "A reconstructed client resource changed during setup. Retry to repair it from the cached JAR."); return MAAGE_STATUS_PROPAGATE(observer->trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VERIFY, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VERIFY);
        }
        result = observer->record(observer->context, entry->name, entry->archive_name, entry->hash, entry->size, fingerprint);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(observer->trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VERIFY);
        observer->progress(observer->context, MAAGE_CLIENT_FINAL_CHECK, i + 1, resources->count, entry->name, 0);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_VERIFY, 0);
}
maage_status maage_client_resources_close(maage_client_resources *resources)
{
    if (!resources) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CLOSE, 0);
    if (resources->archive) zip_discard(resources->archive);
    free(resources->entries); free(resources);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CLOSE, 0);
}
