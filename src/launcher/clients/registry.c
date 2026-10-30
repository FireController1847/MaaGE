#include "registry.h"
#include "files.h"
#include "semver.h"
#include "maage/log.h"
#include "../access/verify.h"
#include <json-c/json.h>
#include "maage/crypto.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#define MAAGE_CLIENT_PAYLOAD "package"
#define MAAGE_CLIENT_DESCRIPTOR "maage-client.json"
#ifdef _WIN32
#define MAAGE_CLIENT_FILENAME "maage-client.exe"
#else
#define MAAGE_CLIENT_FILENAME "maage-client"
#endif
static maage_status maage_client_text(struct json_object *o, const char *key, char *out, size_t capacity, int optional, maage_status_trace *trace) {
    const char *s = maage_access_json_string(o, key), *p;
    if ((!optional && !*s) || strlen(s) >= capacity) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    for (p = s; *p; ++p)
        if ((unsigned char) *p < 32 || (unsigned char) *p > 126 || *p == '"' || *p == '\\') return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    strcpy(out, s);
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
}
static int maage_client_id(const char *s) {
    return strlen(s) == 64 && strspn(s, "0123456789abcdef") == 64;
}
/* Descriptors stay small; registries can grow within a bounded JSON byte budget. */
#define MAAGE_CLIENT_DESCRIPTOR_BYTES (256UL * 1024UL)
#define MAAGE_CLIENT_REGISTRY_BYTES (16UL * 1024UL * 1024UL)
static maage_status maage_client_json(const char *path, size_t limit, struct json_object **output, maage_status_trace *trace) {
    char *data;
    FILE *file;
    long length;
    size_t received;
    maage_status result;
    *output = NULL;
    errno = 0;
    file = maage_storage_open(path, "rb");
    if (!file) return MAAGE_STATUS_PROPAGATE(trace, maage_status_from_errno(errno, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN), MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD);
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_READ, 0), MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD);
    }
    if ((unsigned long) length > limit) {
        fclose(file);
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE, 0), MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD);
    }
    data = (char *) malloc((size_t) length + 1);
    if (!data) {
        fclose(file);
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD, 0), MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD);
    }
    received = fread(data, 1, (size_t) length, file);
    result = received == (size_t) length && !ferror(file) ? maage_access_json(data, received, output) : maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_READ, 0);
    if (fclose(file) && !maage_status_failed(result)) {
        if (*output) json_object_put(*output);
        *output = NULL;
        result = maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_CLOSE, 0);
    }
    free(data);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD);
}
static maage_status maage_client_launch_contract(struct json_object *o, maage_status_trace *trace) {
    struct json_object *value;
    if (!json_object_object_get_ex(o, "instance_storage", &value) || !json_object_is_type(value, json_type_int) || json_object_get_int(value) != 1) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    if (!json_object_object_get_ex(o, "player_appearance", &value) || !json_object_is_type(value, json_type_int) || json_object_get_int(value) != 1) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_VALIDATE, 0);
}
static maage_status maage_client_fields(struct json_object *o, maage_client_build *b, maage_status_trace *trace) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    {
        maage_status result = maage_status_make((((!maage_status_failed(child_result = maage_client_launch_contract(o, trace))) && (!maage_status_failed(child_result = maage_client_text(o, "version", b->version, sizeof(b->version), 0, trace))) && maage_semver_valid(b->version) &&
                                                  (!maage_status_failed(child_result = maage_client_text(o, "build", b->build, sizeof(b->build), 1, trace))) &&
                                                  (!maage_status_failed(child_result = maage_client_text(o, "build_type", b->build_type, sizeof(b->build_type), 0, trace))) &&
                                                  (!maage_status_failed(child_result = maage_client_text(o, "platform", b->platform, sizeof(b->platform), 0, trace))) &&
                                                  (!maage_status_failed(child_result = maage_client_text(o, "architecture", b->architecture, sizeof(b->architecture), 0, trace))) &&
                                                  (!maage_status_failed(child_result = maage_client_text(o, "executable", b->executable, sizeof(b->executable), 0, trace))) &&
                                                  !strcmp(b->executable, MAAGE_CLIENT_FILENAME) &&
                                                  (!maage_status_failed(child_result = maage_client_text(o, "sha256", b->sha256, sizeof(b->sha256), 0, trace))) && maage_client_id(b->sha256) &&
                                                  (!maage_status_failed(child_result = maage_client_text(o, "channel", b->channel, sizeof(b->channel), 1, trace))))
                                                     ? MAAGE_STATUS_SUCCESS
                                                     : MAAGE_STATUS_ERROR),
                                                MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0);
        return MAAGE_STATUS_PROPAGATE(trace, maage_status_failed(result) && maage_status_failed(child_result) ? child_result : result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    }
}
static maage_status maage_client_executable(const char *package, const maage_client_build *b, char path[MAAGE_PATH_CAPACITY], maage_status_trace *trace) {
    if (!maage_storage_directory_exists(package)) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
    return MAAGE_STATUS_PROPAGATE(trace, maage_storage_join(path, MAAGE_PATH_CAPACITY, package, b->executable), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
}
static maage_status maage_client_identity(maage_client_build *b, maage_status_trace *trace) {
    char identity[1024];
    unsigned char digest[32];
    unsigned i;
    maage_status result;
    sprintf(identity, "%s\n%s\n%s\n%s\n%s\n%s", b->version, b->build, b->platform, b->architecture, b->build_type, b->sha256);
    result = maage_crypto_digest(MAAGE_CRYPTO_SHA256, identity, strlen(identity), digest, sizeof(digest));
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    for (i = 0; i < 32; ++i)
        sprintf(b->id + i * 2, "%02x", digest[i]);
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_HASH, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
}
static int maage_client_compatible(const maage_client_build *b) {
    const char *p = b->architecture, *end, *host, *host_end;
    if (strcmp(b->platform, MAAGE_CLIENT_HOST_PLATFORM)) return 0;
    do {
        end = strchr(p, ',');
        if (!end) end = p + strlen(p);
        host = MAAGE_CLIENT_HOST_ARCH;
        do {
            host_end = strchr(host, ',');
            if (!host_end) host_end = host + strlen(host);
            if (end - p == host_end - host && !memcmp(p, host, (size_t) (end - p))) return 1;
            host = *host_end ? host_end + 1 : host_end;
        } while (*host);
        p = *end ? end + 1 : end;
    } while (*p);
    return 0;
}
static maage_status maage_client_inspect(const char *package, maage_client_build *b, char error[256], maage_status_trace *trace) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    char path[MAAGE_PATH_CAPACITY], actual[65];
    struct json_object *o, *v;
    FILE *f;
    unsigned char magic[4];
    int ok;
    if (!maage_storage_directory_exists(package)) {
#ifdef _WIN32
        maage_log_error("maage.launcher.clients.registry", "Windows Client package directory cannot be accessed: '%s'.", package);
#endif
        goto failed;
    }
    child_result = maage_storage_join(path, sizeof(path), package, MAAGE_CLIENT_DESCRIPTOR);
    if (maage_status_failed(child_result)) {
#ifdef _WIN32
        maage_log_error("maage.launcher.clients.registry", "Windows Client manifest path cannot be constructed for package '%s' (status=0x%08lx).", package, (unsigned long) child_result);
#endif
        goto failed;
    }
    child_result = maage_client_json(path, MAAGE_CLIENT_DESCRIPTOR_BYTES, &o, trace);
    if (maage_status_failed(child_result)) {
#ifdef _WIN32
        maage_log_error("maage.launcher.clients.registry", "Windows Client manifest cannot be loaded: '%s' (status=0x%08lx).", path, (unsigned long) child_result);
#endif
        goto failed;
    }
    ok = o && json_object_object_get_ex(o, "schema", &v) && json_object_is_type(v, json_type_int) && json_object_get_int(v) == 1 && !strcmp(maage_access_json_string(o, "application"), "maage-client") && (!maage_status_failed(child_result = maage_client_fields(o, b, trace)));
    /* No network protocol has been implemented; do not claim compatibility. */
    if (ok && json_object_object_get_ex(o, "protocol", &v) && v) ok = json_object_is_type(v, json_type_null);
    if (o) json_object_put(o);
    if (!ok || !maage_client_compatible(b)) {
#ifdef _WIN32
        maage_log_error("maage.launcher.clients.registry", "Windows Client manifest is invalid or incompatible: '%s' (schema/contract valid=%d, package platform='%s', architecture='%s', host platform='%s', host architecture='%s').",
                        path, ok, b->platform, b->architecture, MAAGE_CLIENT_HOST_PLATFORM, MAAGE_CLIENT_HOST_ARCH);
#endif
        strcpy(error, "Select a MaaGE Client package for this platform and architecture with a valid schema-1 manifest, instance_storage 1 and player_appearance 1. Network protocol must be undefined.");
        return MAAGE_STATUS_PROPAGATE(trace, (maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0)), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    }
    child_result = maage_client_executable(package, b, path, trace);
    if (maage_status_failed(child_result)) {
#ifdef _WIN32
        maage_log_error("maage.launcher.clients.registry", "Windows Client executable path cannot be resolved in package '%s' (status=0x%08lx).", package, (unsigned long) child_result);
#endif
        goto failed;
    }
    if (!maage_client_executable_file(path)) {
#ifdef _WIN32
        maage_log_error("maage.launcher.clients.registry", "Windows Client executable is absent, a reparse point, or not a regular file: '%s'.", path);
#endif
        goto failed;
    }
    f = maage_storage_open(path, "rb");
    if (!f) {
#ifdef _WIN32
        maage_log_error("maage.launcher.clients.registry", "Windows Client executable cannot be opened: '%s' (errno=%d).", path, errno);
#endif
        goto failed;
    }
    ok = fread(magic, 1, 4, f) == 4;
    fclose(f);
#ifdef __APPLE__
    ok = ok && ((!memcmp(magic, "\xcf\xfa\xed\xfe", 4)) || (!memcmp(magic, "\xfe\xed\xfa\xcf", 4)) || (!memcmp(magic, "\xca\xfe\xba\xbe", 4)) || (!memcmp(magic, "\xca\xfe\xba\xbf", 4)));
#elif defined(_WIN32)
    ok = ok && magic[0] == 'M' && magic[1] == 'Z';
#else
    ok = ok && !memcmp(magic, "\177ELF", 4);
#endif
    if (!ok) {
#ifdef _WIN32
        maage_log_error("maage.launcher.clients.registry", "Windows Client executable has an invalid PE signature or cannot be read: '%s'.", path);
#endif
        goto failed;
    }
    child_result = maage_client_sha256(path, actual, trace);
    if (maage_status_failed(child_result)) {
#ifdef _WIN32
        maage_log_error("maage.launcher.clients.registry", "Cannot hash Windows Client executable: '%s' (status=0x%08lx).", path, (unsigned long) child_result);
#endif
        goto failed;
    }
    if (strcmp(actual, b->sha256)) {
#ifdef _WIN32
        maage_log_error("maage.launcher.clients.registry", "Windows Client executable SHA-256 does not match manifest: '%s' (expected=%s; actual=%s).", path, b->sha256, actual);
#endif
        goto failed;
    }
    if (maage_status_failed(child_result = maage_client_identity(b, trace))) goto failed;
    b->available = 1;
    strcpy(b->status, "Available");
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
failed:
    strcpy(error, "Client package is missing, unsafe, incomplete, or its executable does not match the build manifest. Rebuild and import the whole package.");
    return MAAGE_STATUS_PROPAGATE(trace, (maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0)), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
}
int maage_clients_find(const maage_client_registry *r, const char *id) {
    unsigned i;
    for (i = 0; i < r->count; ++i)
        if (!strcmp(r->builds[i].id, id)) return (int) i;
    return -1;
}
static maage_status maage_client_directory(const maage_client_registry *r, const char *id, char path[MAAGE_PATH_CAPACITY], maage_status_trace *trace) {
    char relative[96];
    if (!maage_client_id(id)) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 1), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
    sprintf(relative, "clients/%s", id);
    return MAAGE_STATUS_PROPAGATE(trace, maage_storage_join(path, MAAGE_PATH_CAPACITY, r->root, relative), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
}
static maage_status maage_client_package(const maage_client_registry *r, const char *id, char path[MAAGE_PATH_CAPACITY], maage_status_trace *trace) {
    maage_status result = maage_client_directory(r, id, path, trace);
    if (!maage_status_failed(result)) result = maage_storage_join(path, MAAGE_PATH_CAPACITY, path, MAAGE_CLIENT_PAYLOAD);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_clients_path(const maage_client_registry *r, const char *id, char path[MAAGE_PATH_CAPACITY], maage_status_trace *trace) {
    int index;
    char package[MAAGE_PATH_CAPACITY];
    maage_status result;
    if (!r || !id || !path) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, MAAGE_DETAIL_ARGUMENT_UNKNOWN), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
    index = maage_clients_find(r, id);
    if (index < 0) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
    if (!r->builds[index].available) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
    result = maage_client_package(r, id, package, trace);
    if (!maage_status_failed(result)) result = maage_client_executable(package, &r->builds[index], path, trace);
    if (!maage_status_failed(result) && !maage_client_executable_file(path)) result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE, 0);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_RESOLVE);
}
void maage_client_build_label(const maage_client_build *b, char label[384]) {
    sprintf(label, "%s%s%s | %.24s | %.12s | %.96s", b->version, *b->build ? " + " : "", b->build, b->build_type, b->origin, b->status);
}
static maage_status maage_client_add_string(struct json_object *o, const char *key, const char *value) {
    struct json_object *child = json_object_new_string(value);
    if (!child) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SAVE, 0);
    if (json_object_object_add(o, key, child)) {
        json_object_put(child);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SAVE, 0);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SAVE, 0);
}
static maage_status maage_client_save(maage_client_registry *r, maage_status_trace *trace) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SAVE, 0);
    struct json_object *o = json_object_new_object(), *list = json_object_new_array();
    struct json_object *b, *number;
    char path[MAAGE_PATH_CAPACITY], tmp[MAAGE_PATH_CAPACITY];
    const char *text;
    unsigned i;
    FILE *f;
    int ok = 0;
    if (!o || !list) goto failed;
    number = json_object_new_int(1);
    if (!number) goto failed;
    if (json_object_object_add(o, "schema", number)) {
        json_object_put(number);
        goto failed;
    }
    for (i = 0; i < r->count; ++i) {
        const maage_client_build *item = &r->builds[i];
        b = json_object_new_object();
        if (!b) goto failed;
#define MAAGE_CLIENT_FIELD(key)                                                            \
    if (maage_status_failed(child_result = maage_client_add_string(b, #key, item->key))) { \
        json_object_put(b);                                                                \
        goto failed;                                                                       \
    }
        MAAGE_CLIENT_FIELD(id);
        MAAGE_CLIENT_FIELD(version);
        MAAGE_CLIENT_FIELD(build);
        MAAGE_CLIENT_FIELD(build_type);
        MAAGE_CLIENT_FIELD(platform);
        MAAGE_CLIENT_FIELD(architecture);
        MAAGE_CLIENT_FIELD(executable);
        MAAGE_CLIENT_FIELD(sha256);
        MAAGE_CLIENT_FIELD(origin);
        MAAGE_CLIENT_FIELD(channel);
#undef MAAGE_CLIENT_FIELD
        number = json_object_new_int(1);
        if (!number) {
            json_object_put(b);
            goto failed;
        }
        if (json_object_object_add(b, "instance_storage", number)) {
            json_object_put(number);
            json_object_put(b);
            goto failed;
        }
        number = json_object_new_int(1);
        if (!number) {
            json_object_put(b);
            goto failed;
        }
        if (json_object_object_add(b, "player_appearance", number)) {
            json_object_put(number);
            json_object_put(b);
            goto failed;
        }
        number = json_object_new_double(item->imported);
        if (!number) {
            json_object_put(b);
            goto failed;
        }
        if (json_object_object_add(b, "imported", number)) {
            json_object_put(number);
            json_object_put(b);
            goto failed;
        }
        if (json_object_array_add(list, b)) {
            json_object_put(b);
            goto failed;
        }
    }
    if (json_object_object_add(o, "builds", list)) goto failed;
    list = NULL;
    text = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    if (!text || strlen(text) > MAAGE_CLIENT_REGISTRY_BYTES) goto failed;
    if ((!maage_status_failed(child_result = maage_storage_join(path, sizeof(path), r->root, "config/client-builds.json"))) && (!maage_status_failed(child_result = maage_storage_join(tmp, sizeof(tmp), r->root, "config/client-builds.json.tmp"))) && (f = maage_storage_open(tmp, "wb")) != NULL) {
        ok = fwrite(text, 1, strlen(text), f) == strlen(text);
        if (ok) {
            child_result = maage_storage_sync(f);
            if (maage_status_failed(child_result)) ok = 0;
        }
        if (fclose(f)) ok = 0;
        if (ok) ok = !maage_status_failed(child_result = maage_storage_replace(tmp, path));
        if (!ok) maage_storage_remove(tmp);
    } else if (!maage_status_failed(child_result)) child_result = maage_status_from_errno(errno ? errno : EIO, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN);
failed:
    if (list) json_object_put(list);
    if (o) json_object_put(o);
    return MAAGE_STATUS_PROPAGATE(trace, (ok ? maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SAVE, 0) : maage_status_failed(child_result) ? child_result
                                                                                                                                                       : maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SAVE, 0)),
                                  MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SAVE);
}
static int maage_client_sort(const void *left, const void *right) {
    const maage_client_build *a = (const maage_client_build *) left;
    const maage_client_build *b = (const maage_client_build *) right;
    return maage_semver_compare(b->version, a->version);
}
maage_status maage_clients_dispose(maage_client_registry *r) {
    if (!r) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    free(r->builds);
    r->builds = NULL;
    r->count = r->capacity = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
static maage_status maage_clients_read(maage_client_registry *r, const char *root, char error[256], maage_status_trace *trace) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    char path[MAAGE_PATH_CAPACITY], why[256];
    struct json_object *o, *list, *b, *v;
    unsigned i;
    size_t length;
    maage_client_build candidate;
    memset(r, 0, sizeof(*r));
    if (strlen(root) >= sizeof(r->root)) return MAAGE_STATUS_PROPAGATE(trace, (maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD, 0)), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    strcpy(r->root, root);
    if (maage_status_failed(child_result = maage_storage_join(path, sizeof(path), root, "config")) || maage_status_failed(child_result = maage_storage_directories(path)) || maage_status_failed(child_result = maage_storage_join(path, sizeof(path), root, "config/client-builds.json"))) return MAAGE_STATUS_PROPAGATE(trace, (maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD, 0)), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    if (!maage_client_file_kind(path)) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD, 0), MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD);
    child_result = maage_client_json(path, MAAGE_CLIENT_REGISTRY_BYTES, &o, trace);
    if (maage_status_failed(child_result)) {
        strcpy(error, "Cannot read Client registry. Restore config/client-builds.json before managing builds.");
        return MAAGE_STATUS_PROPAGATE(trace, child_result, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD);
    }
    if (!o || !json_object_object_get_ex(o, "schema", &v) || !json_object_is_type(v, json_type_int) || json_object_get_int(v) != 1 || !json_object_object_get_ex(o, "builds", &list) || !json_object_is_type(list, json_type_array)) {
        if (o) json_object_put(o);
        strcpy(error, "Cannot read Client registry. Restore config/client-builds.json before managing builds.");
        return MAAGE_STATUS_PROPAGATE(trace, (maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD, 0)), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    }
    length = json_object_array_length(list);
    if (length > INT_MAX || length >= (size_t) - 1 / sizeof(*r->builds)) goto invalid;
    r->builds = (maage_client_build *) calloc(length + 1, sizeof(*r->builds));
    if (!r->builds) {
        child_result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD, 0);
        json_object_put(o);
        strcpy(error, "Cannot allocate Client registry.");
        return MAAGE_STATUS_PROPAGATE(trace, (maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD, 0)), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    }
    r->capacity = (unsigned) (length + 1);
    for (i = 0; i < (unsigned) length; ++i) {
        maage_client_build *item = &r->builds[r->count];
        b = json_object_array_get_idx(list, i);
        memset(item, 0, sizeof(*item));
        if (maage_status_failed(child_result = maage_client_fields(b, item, trace)) || maage_status_failed(child_result = maage_client_text(b, "id", item->id, sizeof(item->id), 0, trace)) || !maage_client_id(item->id) || maage_status_failed(child_result = maage_client_text(b, "origin", item->origin, sizeof(item->origin), 0, trace)) || (strcmp(item->origin, "local") && strcmp(item->origin, "remote")) || maage_clients_find(r, item->id) >= 0) goto invalid;
        candidate = *item;
        if (maage_status_failed(child_result = maage_client_identity(&candidate, trace)) || strcmp(candidate.id, item->id)) goto invalid;
        if (json_object_object_get_ex(b, "imported", &v)) item->imported = json_object_get_double(v);
        memset(&candidate, 0, sizeof(candidate));
        if ((!maage_status_failed(child_result = maage_client_package(r, item->id, path, trace))) && (!maage_status_failed(child_result = maage_client_inspect(path, &candidate, why, trace))) && !strcmp(item->id, candidate.id)) {
            item->available = 1;
            strcpy(item->status, "Available");
        } else
            strcpy(item->status, maage_client_compatible(item) ? "Missing or invalid Client package" : "Incompatible platform or architecture");
        ++r->count;
    }
    /* Highest SemVer first; build metadata never changes SemVer precedence. */
    if (r->count > 1) qsort(r->builds, r->count, sizeof(*r->builds), maage_client_sort);
    json_object_put(o);
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD, 0), MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD);
invalid:
    json_object_put(o);
    r->count = 0;
    strcpy(error, "Invalid Client registry entry; restore the registry before managing builds.");
    return MAAGE_STATUS_PROPAGATE(trace, (maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD, 0)), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
}
maage_status maage_clients_load(maage_client_registry *r, const char *root, char error[256], maage_status_trace *trace) {
    maage_client_registry next;
    maage_status result;
    memset(&next, 0, sizeof(next));
    result = maage_clients_read(&next, root, error, trace);
    if (maage_status_failed(result)) {
        maage_clients_dispose(&next);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD);
    }
    maage_clients_dispose(r);
    *r = next;
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_LOAD);
}
static maage_status maage_client_lock(maage_client_registry *r, maage_storage_lock **output, maage_status_trace *trace) {
    char path[MAAGE_PATH_CAPACITY];
    maage_status result = maage_storage_join(path, sizeof(path), r->root, "config/client-builds.lock");
    *output = NULL;
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_ACQUIRE);
    return maage_storage_acquire(path, output, trace);
}
static maage_status maage_client_unlock(maage_storage_lock *lock, maage_status result, unsigned char operation, maage_status_trace *trace) {
    maage_status closed;
    MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, operation);
    closed = maage_storage_release(lock);
    if (maage_status_failed(closed)) {
        MAAGE_STATUS_PROPAGATE(trace, closed, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELEASE);
        if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_LAUNCHER, operation, 0);
    }
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_LAUNCHER, operation);
}
maage_status maage_clients_remove(maage_client_registry *r, const char *id, char error[256], maage_status_trace *trace) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    maage_storage_lock *lock = NULL;
    maage_client_registry *current = (maage_client_registry *) calloc(1, sizeof(*current));
    char directory[MAAGE_PATH_CAPACITY];
    int index, ok = 0;
    child_result = maage_client_lock(r, &lock, trace);
    if (maage_status_failed(child_result)) goto done;
    if (!current) child_result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REMOVE, 0);
    if (!current) goto done;
    if (maage_status_failed(child_result = maage_clients_load(current, r->root, error, trace))) goto done;
    index = maage_clients_find(current, id);
    if (index < 0 || strcmp(current->builds[index].origin, "local") || maage_status_failed(child_result = maage_client_directory(current, id, directory, trace))) goto done;
    --current->count;
    memmove(&current->builds[index], &current->builds[index + 1], (current->count - (unsigned) index) * sizeof(current->builds[0]));
    if (maage_status_failed(child_result = maage_client_save(current, trace))) goto done;
    maage_clients_dispose(r);
    *r = *current;
    current->builds = NULL;
    ok = 1;
    if (maage_client_file_kind(directory) && maage_status_failed(child_result = maage_client_remove_tree(directory, trace))) {
        strcpy(error, "Client unregistered; some package files could not be removed. Shared Minecraft resources are unchanged.");
    }
done:
    if (!ok && !*error) strcpy(error, "Cannot remove Client: registry busy, build absent, or storage unavailable.");
    maage_clients_dispose(current);
    free(current);
    if (ok) {
        if (maage_status_failed(child_result)) {
            MAAGE_STATUS_PROPAGATE(trace, child_result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REMOVE);
            child_result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REMOVE, 0);
        } else
            child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REMOVE, 0);
    } else if (!maage_status_failed(child_result)) child_result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REMOVE, 0);
    return maage_client_unlock(lock, child_result, MAAGE_OPERATION_REMOVE, trace);
}
#if MAAGE_LAUNCHER_DEVELOPMENT
static int maage_client_nested(const char *parent, const char *path) {
    size_t n = strlen(parent);
#ifdef _WIN32
    if (_strnicmp(parent, path, n)) return 0;
#else
    if (strncmp(parent, path, n)) return 0;
#endif
    return !path[n] || path[n] == '/';
}
maage_status maage_clients_probe(const char *source, maage_client_candidate *c, char error[256], maage_status_trace *trace) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    char *slash;
    memset(c, 0, sizeof(*c));
    if (maage_status_failed(child_result = maage_client_absolute(source, c->source, trace))) {
        strcpy(error, "Cannot resolve the selected Client package.");
        return MAAGE_STATUS_PROPAGATE(trace, child_result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    }
    if (maage_client_file_kind(c->source) != 1 || !(slash = strrchr(c->source, '/')) || strcmp(slash + 1, MAAGE_CLIENT_FILENAME)) {
        strcpy(error, "Select the standalone " MAAGE_CLIENT_FILENAME " executable with its adjacent maage-client.json manifest.");
        return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE, 0), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
    }
    *slash = 0;
    return MAAGE_STATUS_PROPAGATE(trace, maage_client_inspect(c->source, &c->build, error, trace), MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_VALIDATE);
}
maage_status maage_clients_import(maage_client_registry *r, const maage_client_candidate *c, char error[256], maage_status_trace *trace) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    maage_storage_lock *lock = NULL;
    maage_client_registry *current = (maage_client_registry *) calloc(1, sizeof(*current));
    maage_client_build verified;
    char staging[MAAGE_PATH_CAPACITY], destination[MAAGE_PATH_CAPACITY], package[MAAGE_PATH_CAPACITY], managed[MAAGE_PATH_CAPACITY];
    int ok = 0, promoted = 0, staging_owned = 0;
    staging[0] = 0;
    memset(&verified, 0, sizeof(verified));
    child_result = maage_client_lock(r, &lock, trace);
    if (maage_status_failed(child_result)) goto done;
    if (!current) child_result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER, 0);
    if (!current || maage_status_failed(child_result = maage_clients_load(current, r->root, error, trace))) goto done;

    if (maage_status_failed(child_result = maage_client_inspect(c->source, &verified, error, trace)) || strcmp(verified.id, c->build.id)) {
        strcpy(error, "Client changed since confirmation. Inspect and confirm it again.");
        goto done;
    }
    if (maage_clients_find(current, verified.id) >= 0) {
        strcpy(error, "This exact Client build is already registered. Select it, or remove it before importing a replacement.");
        goto done;
    }
    if (maage_status_failed(child_result = maage_storage_join(staging, sizeof(staging), r->root, "clients")) || maage_status_failed(child_result = maage_storage_directories(staging)) || maage_status_failed(child_result = maage_client_directory(current, verified.id, destination, trace))) goto done;
    /* A stale unregistered package must never be silently overwritten. */
    if (maage_client_file_kind(destination)) {
        strcpy(error, "An unregistered package already occupies this build directory. Resolve it before retrying import.");
        goto done;
    }
    if (maage_status_failed(child_result = maage_client_absolute(staging, managed, trace))) goto done;
    if (maage_client_nested(c->source, managed) || maage_client_nested(managed, c->source)) {
        strcpy(error, "Import a developer package outside MaaGE managed storage. Source and destination trees must be separate.");
        goto done;
    }
    if (maage_status_failed(child_result = maage_storage_join(staging, sizeof(staging), r->root, "clients/.import"))) goto done;
    if (maage_client_file_kind(staging) && maage_status_failed(child_result = maage_client_remove_tree(staging, trace))) goto done;
    staging_owned = 1;
    if (maage_status_failed(child_result = maage_storage_directories(staging)) || maage_status_failed(child_result = maage_storage_join(package, sizeof(package), staging, MAAGE_CLIENT_PAYLOAD)) || maage_status_failed(child_result = maage_client_copy_tree(c->source, package, trace))) {
        strcpy(error, "Cannot copy Client package. Escaping links, reparse points, special files, oversized trees and unreadable files are unsupported; check package permissions.");
        goto done;
    }
    memset(&verified, 0, sizeof(verified));
    if (maage_status_failed(child_result = maage_client_inspect(package, &verified, error, trace)) || strcmp(verified.id, c->build.id)) goto done;
    if (maage_status_failed(child_result = maage_storage_replace(staging, destination))) goto done;
    promoted = 1;
    verified.imported = difftime(time(NULL), (time_t) 0);
    strcpy(verified.origin, "local");
    if (current->count >= current->capacity) {
        unsigned capacity = current->capacity ? current->capacity * 2 : 8;
        maage_client_build *grown;
        if (capacity <= current->capacity || (size_t) capacity > (size_t) - 1 / sizeof(*grown) || !(grown = (maage_client_build *) realloc(current->builds, (size_t) capacity * sizeof(*grown)))) {
            strcpy(error, "Cannot grow Client registry; check available memory.");
            goto done;
        }
        current->builds = grown;
        current->capacity = capacity;
    }
    current->builds[current->count++] = verified;
    if (maage_status_failed(child_result = maage_client_save(current, trace))) goto done;
    /* Registry ownership transfers to the caller. The worker hands this array
     * to the Launcher UI; disposing current must not free that live storage. */
    maage_clients_dispose(r);
    *r = *current;
    current->builds = NULL;
    ok = 1;
    maage_log_info("maage.launcher.clients", "Imported Client %s (%s); developer source preserved.", verified.version, verified.build);
done:
    if (!ok) {
        if (!maage_status_failed(child_result)) child_result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER, 0);
        MAAGE_STATUS_PROPAGATE(trace, child_result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER);
        if (staging_owned) maage_client_remove_tree(promoted ? destination : staging, trace);
        if (!*error) strcpy(error, "Client import failed. Close other Launchers and check data-folder permissions.");
    }
    maage_clients_dispose(current);
    free(current);
    if (ok) child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
    else if (!maage_status_failed(child_result)) child_result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER, 0);
    return maage_client_unlock(lock, child_result, MAAGE_OPERATION_REGISTER, trace);
}
#endif
