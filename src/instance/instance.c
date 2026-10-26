#include "maage/instance.h"
#include "maage/folder_link.h"
#include <json-c/json.h>
#include <json-c/json_c_version.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#if JSON_C_VERSION_NUM < 0x000f00
#error Instance storage requires json-c 0.15 or newer
#endif

#define MAAGE_INSTANCE_MANIFEST_MAX 65536
typedef struct maage_instance_association {
    maage_instance_mapping mode;
    char target[MAAGE_PATH_CAPACITY];
} maage_instance_association;
struct maage_instance {
    maage_instance_info info;
    char root[MAAGE_PATH_CAPACITY], directory[MAAGE_PATH_CAPACITY];
    maage_instance_association folders[MAAGE_INSTANCE_FOLDER_COUNT];
    struct json_object *manifest;
    maage_storage_lock *use;
    int archived;
};
static const char *const maage_instance_folder_names[MAAGE_INSTANCE_FOLDER_COUNT] = {
    "worlds", "mods", "config", "resourcepacks", "shaderpacks", "screenshots", "logs"
};
static maage_status maage_instance_problem(maage_status result, unsigned char operation,
    char error[256], const char *message, maage_status_trace *trace,
    const char *file, unsigned long line)
{
    if (error && message) strcpy(error, message);
    return maage_status_trace_propagate(trace, result, MAAGE_CONTEXT_INSTANCE,
        operation, file, line);
}
#define MAAGE_INSTANCE_ERROR(code, operation, error, message, trace) \
    maage_instance_problem(maage_status_make((code), MAAGE_CONTEXT_INSTANCE, (operation), \
        (code) == MAAGE_STATUS_INVALID_ARGUMENT ? MAAGE_DETAIL_ARGUMENT_UNKNOWN : 0), \
        (operation), (error), (message), (trace), __FILE__, (unsigned long)__LINE__)
#define MAAGE_INSTANCE_FORWARD(result, operation, error, message, trace) \
    maage_instance_problem((result), (operation), (error), (message), (trace), \
        __FILE__, (unsigned long)__LINE__)

static int maage_instance_text(const char *text, size_t capacity)
{
    const unsigned char *p = (const unsigned char *)text;
    unsigned long point, minimum;
    unsigned int remaining;
    size_t length;
    if (!text || !(length = strlen(text)) || length >= capacity) return 0;
    while (*p) {
        if (*p < 32 || *p == 127) return 0;
        if (*p < 128) { ++p; continue; }
        if (*p >= 194 && *p <= 223) { point = *p & 31; remaining = 1; minimum = 128; }
        else if (*p >= 224 && *p <= 239) { point = *p & 15; remaining = 2; minimum = 2048; }
        else if (*p >= 240 && *p <= 244) { point = *p & 7; remaining = 3; minimum = 65536UL; }
        else return 0;
        ++p;
        while (remaining--) {
            if (*p < 128 || *p > 191) return 0;
            point = (point << 6) | (*p++ & 63);
        }
        if (point < minimum || (point >= 55296UL && point <= 57343UL) || point > 1114111UL) return 0;
    }
    return 1;
}
static int maage_instance_build(const char *build)
{
    size_t i, length;
    if (!build || (length = strlen(build)) >= 65) return 0;
    for (i = 0; i < length; ++i)
        if (!((build[i] >= 'a' && build[i] <= 'z') || (build[i] >= 'A' && build[i] <= 'Z') ||
            (build[i] >= '0' && build[i] <= '9') || build[i] == '-' || build[i] == '_' || build[i] == '.')) return 0;
    return 1;
}
static int maage_instance_string(struct json_object *object, const char *key,
    char *output, size_t capacity)
{
    struct json_object *value;
    const char *text;
    size_t length;
    if (!object || !json_object_is_type(object,json_type_object) ||
        !json_object_object_get_ex(object, key, &value) || !json_object_is_type(value, json_type_string)) return 0;
    text = json_object_get_string(value); length = (size_t)json_object_get_string_len(value);
    if (length >= capacity || strlen(text) != length) return 0;
    memcpy(output, text, length + 1); return 1;
}
static int maage_instance_regular_path(const char *path, int allow_missing)
{
    char parent[MAAGE_PATH_CAPACITY], *separator;
    int status;
    if (!path || strlen(path) >= sizeof(parent)) return 0;
    strcpy(parent,path); separator = strrchr(parent,'/');
    if (!separator || separator == parent) return 0;
    *separator = 0;
    if (!maage_folder_link_managed_path(parent,0) || !maage_storage_directory_exists(parent)) return 0;
    status = maage_storage_status(path);
    return status == 1 || (allow_missing && status == 0);
}
static maage_status maage_instance_json_read(const char *path, struct json_object **output,
    char error[256], maage_status_trace *trace)
{
    struct json_tokener *tokener;
    struct json_object *object;
    FILE *file;
    char *data;
    size_t count, position;
    int failed, kind;
    maage_status result;
    if (output) *output = NULL;
    if (!path || !output) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_OPERATION_LOAD, error, "Missing instance configuration arguments.", trace);
    if (!maage_instance_regular_path(path, 0)) {
        kind = maage_storage_status(path);
        return MAAGE_INSTANCE_ERROR(kind == 0 ? MAAGE_STATUS_NOT_FOUND : MAAGE_STATUS_ACCESS_DENIED,
            MAAGE_OPERATION_LOAD, error, "Instance configuration is missing, unsafe, or inaccessible.", trace);
    }
    file = maage_storage_open(path, "rb");
    if (!file) return MAAGE_INSTANCE_FORWARD(maage_status_from_errno(errno, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN),
        MAAGE_OPERATION_LOAD, error, "Instance configuration could not be opened.", trace);
    data = (char *)malloc(MAAGE_INSTANCE_MANIFEST_MAX + 1);
    if (!data) { fclose(file); return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_OPERATION_LOAD, error, "Not enough memory to read instance configuration.", trace); }
    count = fread(data, 1, MAAGE_INSTANCE_MANIFEST_MAX + 1, file);
    failed = ferror(file);
    if (fclose(file)) failed = 1;
    if (failed || count > MAAGE_INSTANCE_MANIFEST_MAX || memchr(data, 0, count) != NULL) {
        free(data); return MAAGE_INSTANCE_ERROR(failed ? MAAGE_STATUS_IO_ERROR : MAAGE_STATUS_MALFORMED_INPUT,
            MAAGE_OPERATION_LOAD, error, "Instance configuration is oversized or could not be read.", trace);
    }
    tokener = json_tokener_new_ex(32);
    if (!tokener) { free(data); return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_OPERATION_PARSE, error, "Not enough memory to decode instance configuration.", trace); }
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    object = json_tokener_parse_ex(tokener, data, (int)count);
    position = json_tokener_get_parse_end(tokener);
    failed = json_tokener_get_error(tokener) != json_tokener_success || !object ||
        !json_object_is_type(object, json_type_object);
    for (; position < count; ++position)
        if (data[position] != ' ' && data[position] != '\t' && data[position] != '\r' && data[position] != '\n') failed = 1;
    json_tokener_free(tokener); free(data);
    if (failed) {
        if (object) json_object_put(object);
        result = MAAGE_INSTANCE_ERROR(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_PARSE,
            error, "Instance configuration is corrupt or is not a JSON object.", trace);
        return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_LOAD, NULL, NULL, trace);
    }
    *output = object;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
}
static maage_status maage_instance_roots(const char *root, char normalized[MAAGE_PATH_CAPACITY],
    char instances[MAAGE_PATH_CAPACITY], int create, char error[256], maage_status_trace *trace)
{
    maage_status result;
    if (!root || !maage_folder_link_managed_path(root, 0))
        return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_RESOLVE,
            error, "The MaaGE instance root is missing, unsafe, or inaccessible.", trace);
    if (create) {
        result = maage_storage_directories(root);
        if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_RESOLVE,
            error, "The MaaGE instance root could not be created.", trace);
    } else if (!maage_storage_directory_exists(root))
        return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE,
            error, "The MaaGE instance root is missing or inaccessible.", trace);
    result = maage_folder_link_canonical(root, normalized, MAAGE_PATH_CAPACITY);
    if (!maage_status_failed(result)) result = maage_storage_join(instances, MAAGE_PATH_CAPACITY, normalized, "instances");
    if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_RESOLVE,
        error, "The MaaGE instance root could not be resolved.", trace);
    if (!maage_folder_link_managed_path(instances, 0))
        return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_RESOLVE,
            error, "The MaaGE instance directory is unsafe.", trace);
    if (create) {
        result = maage_storage_directories(instances);
        if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_RESOLVE,
            error, "The MaaGE instance directory could not be created.", trace);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
static maage_status maage_instance_lock(const maage_instance *instance, maage_storage_lock **output,
    char error[256], maage_status_trace *trace)
{
    char directory[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY], relative[64];
    maage_status result;
    *output = NULL;
    result = maage_storage_join(directory, sizeof(directory), instance->root, "instances/.locks");
    if (!maage_status_failed(result) && !maage_folder_link_managed_path(directory, 0))
        result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_ACQUIRE, 0);
    if (!maage_status_failed(result)) result = maage_storage_directories(directory);
    if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_ACQUIRE,
        error, "Could not prepare the instance ownership lock.", trace);
    strcpy(relative, instance->info.id); strcat(relative, ".lock");
    result = maage_storage_join(path, sizeof(path), directory, relative);
    if (!maage_status_failed(result) && !maage_instance_regular_path(path, 1))
        result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_ACQUIRE, 0);
    if (!maage_status_failed(result)) result = maage_storage_acquire(path, output, trace);
    if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_ACQUIRE,
        error, "This instance is in use or its ownership lock is inaccessible.", trace);
    return result;
}

static int maage_instance_shareable(unsigned int folder)
{
    return folder == MAAGE_INSTANCE_FOLDER_RESOURCEPACKS ||
        folder == MAAGE_INSTANCE_FOLDER_SHADERPACKS || folder == MAAGE_INSTANCE_FOLDER_SCREENSHOTS;
}
static int maage_instance_ancestor(const char *root, const char *path)
{
    return maage_folder_link_path_within(root,path);
}
static maage_status maage_instance_target(maage_instance *instance, unsigned int folder,
    maage_instance_mapping mode, const char *custom, int require_existing,
    char error[256], maage_status_trace *trace)
{
    char target[MAAGE_PATH_CAPACITY], canonical[MAAGE_PATH_CAPACITY];
    maage_instance_association *association = &instance->folders[folder];
    maage_status result;
    if (mode == MAAGE_INSTANCE_PRIVATE) {
        result = maage_storage_join(target, sizeof(target), instance->directory, maage_instance_folder_names[folder]);
        if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_RESOLVE,
            error, "A private instance folder path could not be resolved.", trace);
        if (!maage_folder_link_managed_path(target, require_existing && maage_instance_shareable(folder)))
            return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_RESOLVE,
                error, "A private instance folder contains an unsafe link or path.", trace);
    } else if (!maage_instance_shareable(folder)) {
        return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_RESOLVE,
            error, "Worlds, mods, config, and logs must remain private to their instance.", trace);
    } else if (mode == MAAGE_INSTANCE_SHARED) {
        result = maage_storage_join(target, sizeof(target), instance->root, "shared");
        if (!maage_status_failed(result)) result = maage_storage_join(target, sizeof(target), target, maage_instance_folder_names[folder]);
        if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_RESOLVE,
            error, "The managed shared folder path could not be resolved.", trace);
        if (!maage_folder_link_managed_path(target, 0)) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ACCESS_DENIED,
            MAAGE_OPERATION_RESOLVE, error, "The managed shared folder path is unsafe.", trace);
    } else if (mode == MAAGE_INSTANCE_CUSTOM) {
        if (!custom || strlen(custom) >= sizeof(target) || !maage_folder_link_managed_path(custom, 0))
            return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE,
                error, "A custom association requires a selected absolute directory without linked parents.", trace);
        strcpy(target, custom);
        result = maage_folder_link_canonical(custom, canonical, sizeof(canonical));
        if (!maage_status_failed(result)) strcpy(target, canonical);
        else if (require_existing) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_RESOLVE,
            error, "The selected custom folder is missing or inaccessible.", trace);
        if (maage_instance_ancestor(instance->root, target) || maage_instance_ancestor(target, instance->directory))
            return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_RESOLVE,
                error, "Custom mappings must select external folders; managed MaaGE data and directory loops are not allowed.", trace);
    } else return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE,
        error, "Unknown instance folder association mode.", trace);
    association->mode = mode; strcpy(association->target, target);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
static maage_status maage_instance_conflicts(const maage_instance *instance,
    char error[256], maage_status_trace *trace)
{
    unsigned int i, j;
    for (i = 0; i < MAAGE_INSTANCE_FOLDER_COUNT; ++i) for (j = i + 1; j < MAAGE_INSTANCE_FOLDER_COUNT; ++j)
        if (maage_instance_ancestor(instance->folders[i].target, instance->folders[j].target) ||
            maage_instance_ancestor(instance->folders[j].target, instance->folders[i].target))
            return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_VALIDATE,
                error, "Instance folder associations overlap or conflict.", trace);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_VALIDATE, 0);
}
static maage_status maage_instance_decode(maage_instance *instance, char error[256], maage_status_trace *trace)
{
    struct json_object *value, *folders, *association;
    struct json_object_iterator iterator, end;
    char id[37], mode[16], library[32], custom[MAAGE_PATH_CAPACITY];
    unsigned int i;
    maage_status result;
    if (!json_object_object_get_ex(instance->manifest, "schema", &value) ||
        !json_object_is_type(value, json_type_int) || json_object_get_int(value) != 1 ||
        !maage_instance_string(instance->manifest, "id", id, sizeof(id)) || strcmp(id, instance->info.id) ||
        !maage_instance_string(instance->manifest, "name", instance->info.name, sizeof(instance->info.name)) ||
        !maage_instance_text(instance->info.name, sizeof(instance->info.name)) ||
        !maage_instance_string(instance->manifest, "clientBuildId", instance->info.client_build, sizeof(instance->info.client_build)) ||
        !maage_instance_build(instance->info.client_build) ||
        !json_object_object_get_ex(instance->manifest, "folders", &folders) || !json_object_is_type(folders, json_type_object))
        return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE,
            error, "The instance manifest has an unsupported schema or invalid identity.", trace);
    instance->info.client_arguments[0] = 0;
    if (json_object_object_get_ex(instance->manifest, "clientArguments", &value) &&
        (!maage_instance_string(instance->manifest, "clientArguments", instance->info.client_arguments,
            sizeof(instance->info.client_arguments)) ||
         (*instance->info.client_arguments && !maage_instance_text(instance->info.client_arguments,
            sizeof(instance->info.client_arguments)))))
        return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE,
            error, "The instance Client arguments must be valid UTF-8 within 4095 bytes, without control characters.", trace);
    iterator = json_object_iter_begin(folders); end = json_object_iter_end(folders);
    while (!json_object_iter_equal(&iterator, &end)) {
        int recognized = 0;
        const char *key = json_object_iter_peek_name(&iterator);
        for (i = 0; i < MAAGE_INSTANCE_FOLDER_COUNT; ++i)
            if (!strcmp(key, maage_instance_folder_names[i])) { recognized = 1; break; }
        if (!recognized) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE,
            error, "The instance manifest names an unsupported folder kind.", trace);
        json_object_iter_next(&iterator);
    }
    for (i = 0; i < MAAGE_INSTANCE_FOLDER_COUNT; ++i) {
        if (!json_object_object_get_ex(folders, maage_instance_folder_names[i], &association))
            result = maage_instance_target(instance, i, MAAGE_INSTANCE_PRIVATE, NULL, 0, error, trace);
        else {
            if (!json_object_is_type(association, json_type_object) || !maage_instance_string(association, "mode", mode, sizeof(mode)))
                return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE,
                    error, "An instance folder association is invalid.", trace);
            if (!strcmp(mode, "private")) result = maage_instance_target(instance, i, MAAGE_INSTANCE_PRIVATE, NULL, 0, error, trace);
            else if (!strcmp(mode, "shared")) {
                if (!maage_instance_string(association, "library", library, sizeof(library)) || strcmp(library, maage_instance_folder_names[i]))
                    return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE,
                        error, "A shared association must use its matching managed library.", trace);
                result = maage_instance_target(instance, i, MAAGE_INSTANCE_SHARED, NULL, 0, error, trace);
            } else if (!strcmp(mode, "custom")) {
                if (!maage_instance_string(association, "path", custom, sizeof(custom)) ||
                    !json_object_object_get_ex(association, "userSelected", &value) ||
                    !json_object_is_type(value, json_type_boolean) || !json_object_get_boolean(value))
                    return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE,
                        error, "A custom association must reference an explicitly selected safe directory.", trace);
                result = maage_instance_target(instance, i, MAAGE_INSTANCE_CUSTOM, custom, 0, error, trace);
            } else return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE,
                error, "Unknown instance folder association mode.", trace);
        }
        if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_DECODE, NULL, NULL, trace);
    }
    result = maage_instance_conflicts(instance, error, trace);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_DECODE);
}
static maage_status maage_instance_manifest_write(maage_instance *instance, char error[256], maage_status_trace *trace)
{
    char path[MAAGE_PATH_CAPACITY], temporary[MAAGE_PATH_CAPACITY];
    const char *text;
    FILE *file;
    size_t length;
    maage_status result;
    if (!maage_folder_link_managed_path(instance->directory, 0) || !maage_storage_directory_exists(instance->directory))
        return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_SAVE,
            error, "The instance manifest directory is unsafe or inaccessible.", trace);
    result = maage_storage_join(path, sizeof(path), instance->directory, "instance.json");
    if (!maage_status_failed(result)) result = maage_storage_join(temporary, sizeof(temporary), instance->directory, "instance.json.tmp");
    if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_SAVE,
        error, "Could not resolve the atomic instance manifest path.", trace);
    if (!maage_instance_regular_path(path, 1)) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ACCESS_DENIED,
        MAAGE_OPERATION_SAVE, error, "The instance manifest path is unsafe.", trace);
    result = maage_storage_remove(temporary);
    if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_SAVE,
        error, "Could not prepare an atomic instance manifest write.", trace);
    text = json_object_to_json_string_ext(instance->manifest, JSON_C_TO_STRING_PRETTY);
    if (!text || (length = strlen(text)) > MAAGE_INSTANCE_MANIFEST_MAX)
        return MAAGE_INSTANCE_ERROR(!text ? MAAGE_STATUS_ALLOCATION_FAILED : MAAGE_STATUS_BUFFER_OVERFLOW,
            MAAGE_OPERATION_SAVE, error, "Could not encode the instance manifest.", trace);
    file = maage_storage_open(temporary, "wb");
    if (!file) return MAAGE_INSTANCE_FORWARD(maage_status_from_errno(errno, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN),
        MAAGE_OPERATION_SAVE, error, "Could not open the staged instance manifest.", trace);
    result = fwrite(text, 1, length, file) == length ? maage_storage_sync(file) :
        maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_WRITE, 0);
    if (fclose(file) && !maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_CLOSE, 0);
    if (!maage_status_failed(result)) result = maage_storage_private_file(temporary);
    if (!maage_status_failed(result)) result = maage_storage_replace(temporary, path);
    if (maage_status_failed(result)) {
        maage_storage_remove(temporary);
        return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_SAVE, error,
            "The instance manifest could not be saved; its previous file was preserved.", trace);
    }
    return result;
}
static maage_status maage_instance_prepare(maage_instance *instance, char error[256], maage_status_trace *trace)
{
    char destination[MAAGE_PATH_CAPACITY];
    maage_folder_link_kind kind;
    unsigned int i;
    maage_status result;
    for (i = 0; i < MAAGE_INSTANCE_FOLDER_COUNT; ++i) {
        result = maage_storage_join(destination, sizeof(destination), instance->directory, maage_instance_folder_names[i]);
        if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_INITIALIZE,
            error, "An instance folder path is too long.", trace);
        if (instance->folders[i].mode == MAAGE_INSTANCE_PRIVATE) result = maage_storage_directories(destination);
        else {
            if (instance->folders[i].mode == MAAGE_INSTANCE_SHARED) result = maage_storage_directories(instance->folders[i].target);
            else result = maage_storage_directory_exists(instance->folders[i].target) ?
                maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0) :
                maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_QUERY, 0);
            if (!maage_status_failed(result)) {
                kind = maage_folder_link_status(destination);
                if (kind == MAAGE_FOLDER_LINK_MISSING) result = maage_folder_link_create(destination, instance->folders[i].target);
                else if ((kind != MAAGE_FOLDER_LINK_LINK && kind != MAAGE_FOLDER_LINK_BROKEN) ||
                    !maage_folder_link_matches(destination, instance->folders[i].target))
                    result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_INITIALIZE, 0);
            }
        }
        if (maage_status_failed(result)) return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_INITIALIZE,
            error, "An instance folder is inaccessible or conflicts with its manifest; existing data was preserved.", trace);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
maage_status maage_instance_open(const char *root, const char *id, int acquire_use,
    maage_instance **output, char error[256], maage_status_trace *trace)
{
    maage_instance *instance;
    char instances[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY];
    maage_status result, closed;
    if (output) *output = NULL;
    if (error) error[0] = 0;
    if (!output || !maage_storage_uuid_valid(id)) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_OPERATION_OPEN, error, "Select a canonical instance UUID.", trace);
    instance = (maage_instance *)calloc(1, sizeof(*instance));
    if (!instance) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_OPERATION_OPEN, error, "Not enough memory to open the instance.", trace);
    strcpy(instance->info.id, id);
    result = maage_instance_roots(root, instance->root, instances, 0, error, trace);
    if (!maage_status_failed(result)) result = maage_storage_join(instance->directory, sizeof(instance->directory), instances, id);
    if (maage_status_failed(result)) goto failed;
    if (!maage_folder_link_managed_path(instance->directory, 0) || !maage_storage_directory_exists(instance->directory)) {
        result = MAAGE_INSTANCE_ERROR(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_OPEN,
            error, "The selected physical instance is missing or unsafe.", trace); goto failed;
    }
    if (acquire_use) {
        result = maage_instance_lock(instance, &instance->use, error, trace);
        if (maage_status_failed(result)) goto failed;
    }
    result = maage_storage_join(path, sizeof(path), instance->directory, "instance.json");
    if (!maage_status_failed(result)) result = maage_instance_json_read(path, &instance->manifest, error, trace);
    if (!maage_status_failed(result)) result = maage_instance_decode(instance, error, trace);
    if (!maage_status_failed(result) && acquire_use) result = maage_instance_prepare(instance, error, trace);
    if (maage_status_failed(result)) goto failed;
    *output = instance;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_OPEN, 0);
failed:
    closed = maage_instance_close(instance);
    MAAGE_STATUS_PROPAGATE(trace, closed, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_CLOSE);
    return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_OPEN, NULL, NULL, trace);
}
/* JSON-C values are transferred only on a successful add. Keeping this small
 * operation local makes allocation failures explicit instead of JSON nulls. */
static maage_status maage_instance_member(struct json_object *object, const char *key,
    struct json_object *value, maage_status_trace *trace)
{
    if (!value) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_OPERATION_REGISTER, NULL, NULL, trace);
    if (json_object_object_add(object, key, value)) {
        json_object_put(value);
        return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER, NULL, NULL, trace);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_instance_create(const char *root, const char *name, const char *build,
    const char *arguments, maage_instance **output, char error[256], maage_status_trace *trace)
{
    maage_instance *instance;
    maage_storage_lock *lock = NULL;
    char instances[MAAGE_PATH_CAPACITY], created[32];
    struct json_object *folders, *association;
    time_t now;
    struct tm *utc;
    unsigned int i;
    maage_status result, closed;
    if (output) *output = NULL;
    if (error) error[0] = 0;
    if (!output || !maage_instance_text(name, 96) || !maage_instance_build(build) || !arguments ||
        (*arguments && !maage_instance_text(arguments, MAAGE_INSTANCE_ARGUMENTS_CAPACITY)))
        return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE,
            error, "Instance names, Client build identifiers and arguments must be valid and within their size limits.", trace);
    instance = (maage_instance *)calloc(1, sizeof(*instance));
    if (!instance) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_OPERATION_CREATE, error, "Not enough memory to create the instance.", trace);
    result = maage_instance_roots(root, instance->root, instances, 1, error, trace);
    if (!maage_status_failed(result)) result = maage_storage_uuid_generate(instance->info.id);
    if (!maage_status_failed(result)) result = maage_storage_join(instance->directory, sizeof(instance->directory), instances, instance->info.id);
    if (!maage_status_failed(result)) result = maage_instance_lock(instance, &lock, error, trace);
    if (maage_status_failed(result)) goto done;
    if (maage_storage_status(instance->directory) != 0) {
        result = MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_CREATE,
            error, "Could not safely create a unique physical instance.", trace); goto done;
    }
    result = maage_storage_directories(instance->directory);
    if (maage_status_failed(result)) goto done;
    instance->manifest = json_object_new_object();
    if (!instance->manifest) { result = MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_OPERATION_CREATE, error, "Not enough memory to create the instance manifest.", trace); goto done; }
    result = maage_instance_member(instance->manifest, "schema", json_object_new_int(1), trace);
    if (!maage_status_failed(result)) result = maage_instance_member(instance->manifest, "id", json_object_new_string(instance->info.id), trace);
    if (!maage_status_failed(result)) result = maage_instance_member(instance->manifest, "name", json_object_new_string(name), trace);
    if (!maage_status_failed(result)) result = maage_instance_member(instance->manifest, "clientBuildId", json_object_new_string(build), trace);
    if (!maage_status_failed(result)) result = maage_instance_member(instance->manifest, "clientArguments", json_object_new_string(arguments), trace);
    if (maage_status_failed(result)) goto done;
    folders = json_object_new_object();
    result = maage_instance_member(instance->manifest, "folders", folders, trace);
    if (maage_status_failed(result)) goto done;
    now = time(NULL); utc = now == (time_t)-1 ? NULL : gmtime(&now);
    if (utc && strftime(created, sizeof(created), "%Y-%m-%dT%H:%M:%SZ", utc) == 20) {
        result = maage_instance_member(instance->manifest, "createdUtc", json_object_new_string(created), trace);
        if (maage_status_failed(result)) goto done;
    }
    for (i = 0; i < MAAGE_INSTANCE_FOLDER_COUNT; ++i) {
        association = json_object_new_object();
        if (!association) { result = MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_OPERATION_CREATE, error, "Not enough memory to create folder associations.", trace); goto done; }
        result = maage_instance_member(association, "mode", json_object_new_string("private"), trace);
        if (maage_status_failed(result)) { json_object_put(association); goto done; }
        result = maage_instance_member(folders, maage_instance_folder_names[i], association, trace);
        if (maage_status_failed(result)) goto done;
    }
    result = maage_instance_decode(instance, error, trace);
    if (!maage_status_failed(result)) result = maage_instance_manifest_write(instance, error, trace);
    if (!maage_status_failed(result)) { *output = instance; instance = NULL; }
done:
    closed = maage_storage_release(lock);
    if (maage_status_failed(closed)) {
        MAAGE_STATUS_PROPAGATE(trace, closed, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_RELEASE);
        if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_CREATE, 0);
    }
    maage_instance_close(instance);
    return MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_CREATE, error,
        maage_status_failed(result) && (!error || !*error) ? "The physical instance could not be created; existing data was preserved." : NULL, trace);
}
maage_status maage_instance_close(maage_instance *instance)
{
    maage_status result;
    if (!instance) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CLOSE, 0);
    result = maage_storage_release(instance->use);
    if (instance->manifest) json_object_put(instance->manifest);
    free(instance);
    return result;
}
const maage_instance_info *maage_instance_get_info(const maage_instance *instance)
{ return instance ? &instance->info : NULL; }
const char *maage_instance_directory(const maage_instance *instance)
{ return instance && !instance->archived ? instance->directory : NULL; }
maage_status maage_instance_resolve_path(const maage_instance *instance,
    maage_instance_folder folder, const char *relative, char *path, size_t capacity)
{
    const char *base;
    maage_status result;
    if (path && capacity) path[0] = 0;
    if (!instance || instance->archived || !path || !capacity || folder < 0 || folder >= MAAGE_INSTANCE_FOLDER_COUNT)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_RESOLVE, MAAGE_DETAIL_ARGUMENT_UNKNOWN);
    base = instance->folders[folder].target;
    if (relative && *relative) {
        size_t i;
        for (i = 0; relative[i]; ++i)
            if (relative[i] == '/' && (!relative[i + 1] || relative[i + 1] == '/'))
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_RESOLVE, 2);
        result = maage_storage_join(path, capacity, base, relative);
        if (maage_status_failed(result)) return result;
        if (!maage_folder_link_path_within(base, path)) {
            path[0] = 0;
            return maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_RESOLVE, 0);
        }
        return result;
    }
    if (strlen(base) >= capacity) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_RESOLVE, 0);
    strcpy(path, base);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESOLVE, 0);
}
maage_instance_mapping maage_instance_folder_mode(const maage_instance *instance,
    maage_instance_folder folder)
{
    return instance && folder >= 0 && folder < MAAGE_INSTANCE_FOLDER_COUNT ?
        instance->folders[folder].mode : MAAGE_INSTANCE_PRIVATE;
}
static maage_status maage_instance_adopt(maage_instance *instance, maage_instance *fresh)
{
    if (instance->manifest) json_object_put(instance->manifest);
    instance->manifest = fresh->manifest; fresh->manifest = NULL;
    instance->info = fresh->info;
    memcpy(instance->folders, fresh->folders, sizeof(instance->folders));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
static maage_status maage_instance_admin(maage_instance *instance,
    maage_storage_lock **lock, maage_instance **output, char error[256], maage_status_trace *trace)
{
    maage_status result;
    *lock = NULL; *output = NULL;
    if (!instance || instance->archived || instance->use) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_STATE,
        MAAGE_OPERATION_ACQUIRE, error, "An active or archived instance cannot be edited.", trace);
    result = maage_instance_lock(instance, lock, error, trace);
    if (maage_status_failed(result)) return result;
    result = maage_instance_open(instance->root, instance->info.id, 0, output, error, trace);
    if (maage_status_failed(result)) { maage_storage_release(*lock); *lock = NULL; }
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_ACQUIRE);
}
maage_status maage_instance_update(maage_instance *instance, const char *name,
    const char *build, const char *arguments, char error[256], maage_status_trace *trace)
{
    maage_instance *fresh;
    maage_storage_lock *lock;
    maage_status result, closed;
    if (error) error[0] = 0;
    if (!maage_instance_text(name, 96) || !maage_instance_build(build) || !arguments ||
        (*arguments && !maage_instance_text(arguments, MAAGE_INSTANCE_ARGUMENTS_CAPACITY))) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_OPERATION_UPDATE, error, "Instance names, Client build identifiers or arguments are invalid.", trace);
    result = maage_instance_admin(instance, &lock, &fresh, error, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_UPDATE);
    if (!strcmp(fresh->info.name, name) && !strcmp(fresh->info.client_build, build) &&
        !strcmp(fresh->info.client_arguments, arguments))
        result = maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    else {
        result = maage_instance_member(fresh->manifest, "name", json_object_new_string(name), trace);
        if (!maage_status_failed(result)) result = maage_instance_member(fresh->manifest, "clientBuildId", json_object_new_string(build), trace);
        if (!maage_status_failed(result)) result = maage_instance_member(fresh->manifest, "clientArguments", json_object_new_string(arguments), trace);
        if (!maage_status_failed(result)) result = maage_instance_decode(fresh, error, trace);
        if (!maage_status_failed(result)) result = maage_instance_manifest_write(fresh, error, trace);
        if (!maage_status_failed(result)) result = maage_instance_adopt(instance, fresh);
    }
    maage_instance_close(fresh);
    closed = maage_storage_release(lock);
    if (maage_status_failed(closed)) {
        MAAGE_STATUS_PROPAGATE(trace, closed, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_RELEASE);
        if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_UPDATE, 0);
    }
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_UPDATE);
}
static int maage_instance_empty_entry(const char *name, maage_folder_link_kind kind, void *context)
{
    (void)name; (void)kind; *(int *)context = 0; return 1;
}
static maage_status maage_instance_clear_empty(const char *path, maage_status_trace *trace)
{
    maage_folder_link_kind kind = maage_folder_link_status(path);
    int empty = 1;
    maage_status result;
    if (kind == MAAGE_FOLDER_LINK_MISSING) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_REMOVE, 0);
    if (kind == MAAGE_FOLDER_LINK_LINK || kind == MAAGE_FOLDER_LINK_BROKEN) result = maage_folder_link_remove(path);
    else {
        if (kind != MAAGE_FOLDER_LINK_DIRECTORY) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ACCESS_DENIED,
            MAAGE_OPERATION_REMOVE, NULL, NULL, trace);
        result = maage_folder_link_list(path, maage_instance_empty_entry, &empty);
        if (!maage_status_failed(result) && !empty) result = maage_status_make(MAAGE_STATUS_INVALID_STATE,
            MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_REMOVE, 0);
        if (!maage_status_failed(result)) result = maage_storage_remove_directory(path);
    }
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_REMOVE);
}
static int maage_instance_remap_empty(const char *path, maage_folder_link_kind kind)
{
    int empty = 1;
    if (kind == MAAGE_FOLDER_LINK_MISSING || kind == MAAGE_FOLDER_LINK_LINK || kind == MAAGE_FOLDER_LINK_BROKEN) return 1;
    return kind == MAAGE_FOLDER_LINK_DIRECTORY &&
        !maage_status_failed(maage_folder_link_list(path, maage_instance_empty_entry, &empty)) && empty;
}
maage_status maage_instance_remap(maage_instance *instance, maage_instance_folder folder,
    maage_instance_mapping mode, const char *custom, int user_selected, char error[256], maage_status_trace *trace)
{
    maage_instance *fresh;
    maage_storage_lock *lock;
    maage_instance_association old;
    struct json_object *folders, *association;
    char destination[MAAGE_PATH_CAPACITY], staging[MAAGE_PATH_CAPACITY], backup[MAAGE_PATH_CAPACITY], relative[80];
    maage_folder_link_kind kind;
    int staged = 0, retained = 0, installed = 0;
    maage_status result, closed, restored;
    if (error) error[0] = 0;
    if (folder < 0 || folder >= MAAGE_INSTANCE_FOLDER_COUNT || mode < MAAGE_INSTANCE_PRIVATE || mode > MAAGE_INSTANCE_CUSTOM ||
        (mode == MAAGE_INSTANCE_CUSTOM && !user_selected)) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_OPERATION_SET, error, "Custom folder changes require an explicit user directory selection.", trace);
    result = maage_instance_admin(instance, &lock, &fresh, error, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SET);
    old = fresh->folders[folder];
    result = maage_instance_target(fresh, (unsigned int)folder, mode, custom, 1, error, trace);
    if (!maage_status_failed(result)) result = maage_instance_conflicts(fresh, error, trace);
    if (!maage_status_failed(result)) result = maage_storage_join(destination, sizeof(destination), fresh->directory, maage_instance_folder_names[folder]);
    if (maage_status_failed(result)) goto done;
    if (old.mode == mode && !strcmp(old.target, fresh->folders[folder].target)) {
        result = maage_instance_prepare(fresh, error, trace);
        if (!maage_status_failed(result)) result = maage_instance_adopt(instance, fresh);
        goto done;
    }
    if (mode == MAAGE_INSTANCE_SHARED) {
        result = maage_storage_directories(fresh->folders[folder].target);
        if (maage_status_failed(result)) goto done;
    }
    kind = maage_folder_link_status(destination);
    if ((kind == MAAGE_FOLDER_LINK_LINK || kind == MAAGE_FOLDER_LINK_BROKEN) &&
        (old.mode == MAAGE_INSTANCE_PRIVATE || !maage_folder_link_matches(destination, old.target))) {
        result = MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SET,
            error, "The existing link conflicts with its manifest; the folder was preserved.", trace); goto done;
    }
    if (!maage_instance_remap_empty(destination, kind)) {
        result = MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_SET,
            error, "The folder contains data or is unsafe; no files were moved or removed.", trace); goto done;
    }
    strcpy(relative, "."); strcat(relative, maage_instance_folder_names[folder]); strcat(relative, ".mapping-new");
    result = maage_storage_join(staging, sizeof(staging), fresh->directory, relative);
    if (maage_status_failed(result)) goto done;
    strcpy(relative, "."); strcat(relative, maage_instance_folder_names[folder]); strcat(relative, ".mapping-old");
    result = maage_storage_join(backup, sizeof(backup), fresh->directory, relative);
    if (maage_status_failed(result)) goto done;
    if (maage_folder_link_status(staging) != MAAGE_FOLDER_LINK_MISSING || maage_folder_link_status(backup) != MAAGE_FOLDER_LINK_MISSING) {
        result = MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SET, error,
            "A previous folder-mapping operation retained recovery entries; preserve and resolve them before remapping.", trace); goto done;
    }
    result = mode == MAAGE_INSTANCE_PRIVATE ? maage_storage_directories(staging) :
        maage_folder_link_create(staging, fresh->folders[folder].target);
    if (maage_status_failed(result)) goto done;
    staged = 1;
    /* Preserve exact prior links/empty directories for rollback. The use lease
     * excludes cooperating instance users and administrators during promotion. */
    if (kind != MAAGE_FOLDER_LINK_MISSING) {
        result = maage_storage_replace(destination, backup);
        if (maage_status_failed(result)) goto done;
        retained = 1;
    }
    result = maage_storage_replace(staging, destination);
    if (maage_status_failed(result)) goto done;
    staged = 0; installed = 1;
    association = json_object_new_object();
    if (!association) { result = MAAGE_INSTANCE_ERROR(MAAGE_STATUS_ALLOCATION_FAILED,
        MAAGE_OPERATION_SET, error, "Not enough memory to save the folder association.", trace); goto done; }
    result = maage_instance_member(association, "mode", json_object_new_string(mode == MAAGE_INSTANCE_PRIVATE ?
        "private" : (mode == MAAGE_INSTANCE_SHARED ? "shared" : "custom")), trace);
    if (!maage_status_failed(result) && mode == MAAGE_INSTANCE_SHARED)
        result = maage_instance_member(association, "library", json_object_new_string(maage_instance_folder_names[folder]), trace);
    else if (!maage_status_failed(result) && mode == MAAGE_INSTANCE_CUSTOM) {
        result = maage_instance_member(association, "path", json_object_new_string(fresh->folders[folder].target), trace);
        if (!maage_status_failed(result)) result = maage_instance_member(association, "userSelected", json_object_new_boolean(1), trace);
    }
    if (maage_status_failed(result)) { json_object_put(association); goto done; }
    json_object_object_get_ex(fresh->manifest, "folders", &folders);
    result = maage_instance_member(folders, maage_instance_folder_names[folder], association, trace);
    if (!maage_status_failed(result)) result = maage_instance_decode(fresh, error, trace);
    if (!maage_status_failed(result)) result = maage_instance_manifest_write(fresh, error, trace);
    if (!maage_status_failed(result)) result = maage_instance_adopt(instance, fresh);
done:
    if (maage_status_failed(result)) {
        MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SET);
        restored = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
        if (installed) restored = maage_instance_clear_empty(destination, trace);
        if (retained) {
            if (maage_folder_link_status(destination) != MAAGE_FOLDER_LINK_MISSING) restored =
                maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SET, 0);
            else {
                closed = maage_storage_replace(backup, destination);
                if (maage_status_failed(closed)) restored = closed;
            }
        }
        if (staged) {
            closed = maage_instance_clear_empty(staging, trace);
            if (maage_status_failed(closed)) restored = closed;
        }
        if (maage_status_failed(restored)) MAAGE_INSTANCE_FORWARD(restored, MAAGE_OPERATION_SET, error,
            "Folder mapping failed; retained mapping recovery entries and all target data were preserved.", trace);
        else if (installed || retained || staged) maage_status_trace_recover(trace, restored,
            MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_RENAME, __FILE__, (unsigned long)__LINE__);
    } else if (retained) {
        closed = maage_instance_clear_empty(backup, trace);
        if (maage_status_failed(closed)) result = MAAGE_INSTANCE_ERROR(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_SET,
            error, "The new mapping was saved; a retained old mapping entry requires manual cleanup.", trace);
    }
    maage_instance_close(fresh);
    closed = maage_storage_release(lock);
    if (maage_status_failed(closed)) {
        MAAGE_STATUS_PROPAGATE(trace, closed, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_RELEASE);
        if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SET, 0);
    }
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SET);
}
maage_status maage_instance_archive(maage_instance *instance, char error[256], maage_status_trace *trace)
{
    maage_instance *fresh;
    maage_storage_lock *lock;
    char relative[64], destination[MAAGE_PATH_CAPACITY], instances[MAAGE_PATH_CAPACITY];
    maage_status result, closed;
    if (error) error[0] = 0;
    result = maage_instance_admin(instance, &lock, &fresh, error, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_REMOVE);
    strcpy(relative, ".deleted-"); strcat(relative, instance->info.id);
    result = maage_storage_join(instances, sizeof(instances), instance->root, "instances");
    if (!maage_status_failed(result)) result = maage_storage_join(destination, sizeof(destination), instances, relative);
    if (!maage_status_failed(result)) result = maage_folder_link_move(fresh->directory, destination);
    if (!maage_status_failed(result)) instance->archived = 1;
    else MAAGE_INSTANCE_FORWARD(result, MAAGE_OPERATION_REMOVE, error,
        "Could not archive the instance safely; its files were preserved.", trace);
    maage_instance_close(fresh);
    closed = maage_storage_release(lock);
    if (maage_status_failed(closed)) {
        MAAGE_STATUS_PROPAGATE(trace, closed, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_RELEASE);
        if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_REMOVE, 0);
    }
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_REMOVE);
}
static maage_status maage_instance_launcher(const char *root, struct json_object **output,
    char error[256], maage_status_trace *trace)
{
    char normalized[MAAGE_PATH_CAPACITY], instances[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY];
    struct json_object *object, *schema;
    maage_status result;
    *output = NULL;
    result = maage_instance_roots(root, normalized, instances, 0, error, trace);
    if (!maage_status_failed(result)) result = maage_storage_join(path, sizeof(path), normalized, "config/launcher.json");
    if (!maage_status_failed(result)) result = maage_instance_json_read(path, &object, error, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SELECT);
    if (!json_object_object_get_ex(object, "schema", &schema) || !json_object_is_type(schema, json_type_int) ||
        json_object_get_int(schema) != 3) {
        json_object_put(object);
        return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SELECT, error,
            "Open the Launcher to choose a physical instance.", trace);
    }
    *output = object;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SELECT, 0);
}
maage_status maage_instance_select(const char *root, const char *requested, int acquire_use,
    maage_instance **output, char error[256], maage_status_trace *trace)
{
    struct json_object *launcher, *installations;
    char selected[37], referenced[37];
    size_t i;
    int found, registered = 0;
    maage_status result;
    if (error) error[0] = 0;
    if (output) *output = NULL;
    if (!output) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SELECT,
        error, "Missing output for the selected instance.", trace);
    if (requested) {
        result = maage_instance_open(root, requested, acquire_use, output, error, trace);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SELECT);
    }
    result = maage_instance_launcher(root, &launcher, error, trace);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SELECT);
    found = maage_instance_string(launcher, "selected", selected, sizeof(selected)) && maage_storage_uuid_valid(selected);
    if (!found) found = maage_instance_string(launcher, "default", selected, sizeof(selected)) && maage_storage_uuid_valid(selected);
    if (found && json_object_object_get_ex(launcher, "installations", &installations) &&
        json_object_is_type(installations, json_type_array)) {
        for (i = 0; i < json_object_array_length(installations); ++i)
            if (maage_instance_string(json_object_array_get_idx(installations, i), "uuid", referenced, sizeof(referenced)) &&
                !strcmp(selected, referenced)) { registered = 1; break; }
    }
    json_object_put(launcher);
    if (!found || !registered) return MAAGE_INSTANCE_ERROR(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_SELECT, error,
        "Select an instance in the Launcher or provide --instance=<uuid>.", trace);
    result = maage_instance_open(root, selected, acquire_use, output, error, trace);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SELECT);
}
