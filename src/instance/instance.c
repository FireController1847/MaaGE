#include "maage/instance.h"
#include "maage/folder_link.h"
#include <json-c/json.h>
#include <json-c/json_c_version.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
static int maage_instance_error(char error[256], const char *message)
{
    if (error) strcpy(error, message);
    return 0;
}
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
static struct json_object *maage_instance_json_read(const char *path, char error[256])
{
    struct json_tokener *tokener;
    struct json_object *object;
    FILE *file;
    char *data;
    size_t count, position;
    int failed;
    if (!maage_instance_regular_path(path,0) ||
        !(file = maage_storage_open(path, "rb"))) {
        maage_instance_error(error, "Instance configuration is missing, unsafe, or inaccessible."); return NULL;
    }
    data = (char *)malloc(MAAGE_INSTANCE_MANIFEST_MAX + 1);
    if (!data) { fclose(file); maage_instance_error(error, "Not enough memory to read instance configuration."); return NULL; }
    count = fread(data, 1, MAAGE_INSTANCE_MANIFEST_MAX + 1, file);
    failed = ferror(file) || count > MAAGE_INSTANCE_MANIFEST_MAX || memchr(data, 0, count) != NULL;
    if (fclose(file)) failed = 1;
    if (failed || !(tokener = json_tokener_new_ex(32))) {
        free(data); maage_instance_error(error, "Instance configuration is oversized or could not be read."); return NULL;
    }
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
        maage_instance_error(error, "Instance configuration is corrupt or is not a JSON object."); return NULL;
    }
    return object;
}
static int maage_instance_roots(const char *root, char normalized[MAAGE_PATH_CAPACITY],
    char instances[MAAGE_PATH_CAPACITY], int create, char error[256])
{
    if (!root || !maage_folder_link_managed_path(root, 0) ||
        (create ? !maage_storage_directories(root) : !maage_storage_directory_exists(root)) ||
        !maage_folder_link_canonical(root, normalized, MAAGE_PATH_CAPACITY) ||
        !maage_storage_join(instances, MAAGE_PATH_CAPACITY, normalized, "instances") ||
        !maage_folder_link_managed_path(instances, 0) ||
        (create && !maage_storage_directories(instances)))
        return maage_instance_error(error, "The MaaGE instance root is missing, unsafe, or inaccessible.");
    return 1;
}
static maage_storage_lock *maage_instance_lock(const maage_instance *instance, char error[256])
{
    char directory[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY], relative[64];
    maage_storage_lock *lock;
    if (!maage_storage_join(directory, sizeof(directory), instance->root, "instances/.locks") ||
        !maage_folder_link_managed_path(directory, 0) || !maage_storage_directories(directory)) {
        maage_instance_error(error, "Could not prepare the instance ownership lock."); return NULL;
    }
    strcpy(relative, instance->info.id); strcat(relative, ".lock");
    if (!maage_storage_join(path, sizeof(path), directory, relative) ||
        !maage_instance_regular_path(path,1) || !(lock = maage_storage_acquire(path))) {
        maage_instance_error(error, "This instance is in use or its ownership lock is inaccessible."); return NULL;
    }
    return lock;
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
static int maage_instance_target(maage_instance *instance, unsigned int folder,
    maage_instance_mapping mode, const char *custom, int require_existing, char error[256])
{
    char target[MAAGE_PATH_CAPACITY], canonical[MAAGE_PATH_CAPACITY];
    maage_instance_association *association = &instance->folders[folder];
    if (mode == MAAGE_INSTANCE_PRIVATE) {
        if (!maage_storage_join(target, sizeof(target), instance->directory, maage_instance_folder_names[folder]) ||
            !maage_folder_link_managed_path(target, require_existing && maage_instance_shareable(folder)))
            return maage_instance_error(error, "A private instance folder contains an unsafe link or path.");
    } else if (!maage_instance_shareable(folder)) {
        return maage_instance_error(error, "Worlds, mods, config, and logs must remain private to their instance.");
    } else if (mode == MAAGE_INSTANCE_SHARED) {
        if (!maage_storage_join(target, sizeof(target), instance->root, "shared") ||
            !maage_storage_join(target, sizeof(target), target, maage_instance_folder_names[folder]) ||
            !maage_folder_link_managed_path(target, 0))
            return maage_instance_error(error, "The managed shared folder path is unsafe.");
    } else if (mode == MAAGE_INSTANCE_CUSTOM) {
        if (!custom || strlen(custom) >= sizeof(target) || !maage_folder_link_managed_path(custom, 0))
            return maage_instance_error(error, "A custom association requires a selected absolute directory without linked parents.");
        strcpy(target, custom);
        if (maage_folder_link_canonical(custom, canonical, sizeof(canonical))) strcpy(target, canonical);
        else if (require_existing) return maage_instance_error(error, "The selected custom folder is missing or inaccessible.");
        if (maage_instance_ancestor(instance->root, target) || maage_instance_ancestor(target, instance->directory))
            return maage_instance_error(error, "Custom mappings must select external folders; managed MaaGE data and directory loops are not allowed.");
    } else return maage_instance_error(error, "Unknown instance folder association mode.");
    association->mode = mode; strcpy(association->target, target); return 1;
}
static int maage_instance_conflicts(const maage_instance *instance, char error[256])
{
    unsigned int i, j;
    for (i = 0; i < MAAGE_INSTANCE_FOLDER_COUNT; ++i) for (j = i + 1; j < MAAGE_INSTANCE_FOLDER_COUNT; ++j)
        if (maage_instance_ancestor(instance->folders[i].target, instance->folders[j].target) ||
            maage_instance_ancestor(instance->folders[j].target, instance->folders[i].target))
            return maage_instance_error(error, "Instance folder associations overlap or conflict.");
    return 1;
}
static int maage_instance_decode(maage_instance *instance, char error[256])
{
    struct json_object *value, *folders, *association;
    struct json_object_iterator iterator, end;
    char id[37], mode[16], library[32], custom[MAAGE_PATH_CAPACITY];
    unsigned int i;
    if (!json_object_object_get_ex(instance->manifest, "schema", &value) ||
        !json_object_is_type(value, json_type_int) || json_object_get_int(value) != 1 ||
        !maage_instance_string(instance->manifest, "id", id, sizeof(id)) || strcmp(id, instance->info.id) ||
        !maage_instance_string(instance->manifest, "name", instance->info.name, sizeof(instance->info.name)) ||
        !maage_instance_text(instance->info.name, sizeof(instance->info.name)) ||
        !maage_instance_string(instance->manifest, "clientBuildId", instance->info.client_build, sizeof(instance->info.client_build)) ||
        !maage_instance_build(instance->info.client_build) ||
        !json_object_object_get_ex(instance->manifest, "folders", &folders) ||
        !json_object_is_type(folders, json_type_object))
        return maage_instance_error(error, "The instance manifest has an unsupported schema or invalid identity.");
    iterator = json_object_iter_begin(folders); end = json_object_iter_end(folders);
    while (!json_object_iter_equal(&iterator,&end)) {
        int recognized = 0;
        const char *key = json_object_iter_peek_name(&iterator);
        for (i = 0; i < MAAGE_INSTANCE_FOLDER_COUNT; ++i)
            if (!strcmp(key, maage_instance_folder_names[i])) { recognized = 1; break; }
        if (!recognized) return maage_instance_error(error, "The instance manifest names an unsupported folder kind.");
        json_object_iter_next(&iterator);
    }
    for (i = 0; i < MAAGE_INSTANCE_FOLDER_COUNT; ++i) {
        if (!json_object_object_get_ex(folders, maage_instance_folder_names[i], &association)) {
            if (!maage_instance_target(instance,i,MAAGE_INSTANCE_PRIVATE,NULL,0,error)) return 0;
            continue;
        }
        if (!json_object_is_type(association,json_type_object) ||
            !maage_instance_string(association,"mode",mode,sizeof(mode)))
            return maage_instance_error(error, "An instance folder association is invalid.");
        if (!strcmp(mode,"private")) {
            if (!maage_instance_target(instance,i,MAAGE_INSTANCE_PRIVATE,NULL,0,error)) return 0;
        } else if (!strcmp(mode,"shared")) {
            if (!maage_instance_string(association,"library",library,sizeof(library)) || strcmp(library,maage_instance_folder_names[i]) ||
                !maage_instance_target(instance,i,MAAGE_INSTANCE_SHARED,NULL,0,error))
                return maage_instance_error(error, "A shared association must use its matching managed library.");
        } else if (!strcmp(mode,"custom")) {
            if (!maage_instance_string(association,"path",custom,sizeof(custom)) ||
                !json_object_object_get_ex(association,"userSelected",&value) ||
                !json_object_is_type(value,json_type_boolean) || !json_object_get_boolean(value) ||
                !maage_instance_target(instance,i,MAAGE_INSTANCE_CUSTOM,custom,0,error))
                return maage_instance_error(error, "A custom association must reference an explicitly selected safe directory.");
        } else return maage_instance_error(error, "Unknown instance folder association mode.");
    }
    return maage_instance_conflicts(instance,error);
}
static int maage_instance_manifest_write(maage_instance *instance, char error[256])
{
    char path[MAAGE_PATH_CAPACITY], temporary[MAAGE_PATH_CAPACITY];
    const char *text;
    FILE *file;
    size_t length;
    int ok;
    if (!maage_folder_link_managed_path(instance->directory,0) || !maage_storage_directory_exists(instance->directory) ||
        !maage_storage_join(path,sizeof(path),instance->directory,"instance.json") ||
        !maage_storage_join(temporary,sizeof(temporary),instance->directory,"instance.json.tmp") ||
        !maage_instance_regular_path(path,1) || !maage_storage_remove(temporary))
        return maage_instance_error(error,"Could not prepare an atomic instance manifest write.");
    text = json_object_to_json_string_ext(instance->manifest,JSON_C_TO_STRING_PRETTY);
    if (!text || (length = strlen(text)) > MAAGE_INSTANCE_MANIFEST_MAX || !(file = maage_storage_open(temporary,"wb")))
        return maage_instance_error(error,"Could not encode or write the instance manifest.");
    ok = fwrite(text,1,length,file) == length && maage_storage_sync(file);
    if (fclose(file)) ok = 0;
    if (ok) ok = maage_storage_private_file(temporary) && maage_storage_replace(temporary,path);
    if (!ok) { maage_storage_remove(temporary); return maage_instance_error(error,"The instance manifest could not be saved; its previous file was preserved."); }
    return 1;
}
static int maage_instance_prepare(maage_instance *instance, char error[256])
{
    char destination[MAAGE_PATH_CAPACITY];
    maage_folder_link_kind kind;
    unsigned int i;
    for (i = 0; i < MAAGE_INSTANCE_FOLDER_COUNT; ++i) {
        if (!maage_storage_join(destination,sizeof(destination),instance->directory,maage_instance_folder_names[i]))
            return maage_instance_error(error,"An instance folder path is too long.");
        if (instance->folders[i].mode == MAAGE_INSTANCE_PRIVATE) {
            if (!maage_storage_directories(destination)) return maage_instance_error(error,"A private instance folder is unsafe or inaccessible.");
            continue;
        }
        if (instance->folders[i].mode == MAAGE_INSTANCE_SHARED) {
            if (!maage_storage_directories(instance->folders[i].target)) return maage_instance_error(error,"The managed shared folder is unsafe or inaccessible.");
        } else if (!maage_storage_directory_exists(instance->folders[i].target))
            return maage_instance_error(error,"A custom folder target is missing or inaccessible; select a valid association.");
        kind = maage_folder_link_status(destination);
        if (kind == MAAGE_FOLDER_LINK_MISSING) {
            if (!maage_folder_link_create(destination,instance->folders[i].target))
                return maage_instance_error(error,"Could not establish the configured instance folder link.");
        } else if ((kind != MAAGE_FOLDER_LINK_LINK && kind != MAAGE_FOLDER_LINK_BROKEN) ||
            !maage_folder_link_matches(destination,instance->folders[i].target))
            return maage_instance_error(error,"An instance folder conflicts with its manifest; existing data was preserved.");
    }
    return 1;
}
int maage_instance_open(const char *root, const char *id, int acquire_use,
    maage_instance **output, char error[256])
{
    maage_instance *instance;
    char instances[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY];
    if (output) *output = NULL;
    if (error) error[0] = 0;
    if (!output || !maage_storage_uuid_valid(id)) return maage_instance_error(error,"Select a canonical instance UUID.");
    instance = (maage_instance *)calloc(1,sizeof(*instance));
    if (!instance) return maage_instance_error(error,"Not enough memory to open the instance.");
    strcpy(instance->info.id,id);
    if (!maage_instance_roots(root,instance->root,instances,0,error) ||
        !maage_storage_join(instance->directory,sizeof(instance->directory),instances,id) ||
        !maage_folder_link_managed_path(instance->directory,0) || !maage_storage_directory_exists(instance->directory)) {
        maage_instance_error(error,"The selected physical instance is missing or unsafe."); goto failed;
    }
    if (acquire_use && !(instance->use = maage_instance_lock(instance,error))) goto failed;
    if (!maage_storage_join(path,sizeof(path),instance->directory,"instance.json") ||
        !(instance->manifest = maage_instance_json_read(path,error)) || !maage_instance_decode(instance,error) ||
        (acquire_use && !maage_instance_prepare(instance,error))) goto failed;
    *output = instance; return 1;
failed:
    maage_instance_close(instance); return 0;
}
int maage_instance_create(const char *root, const char *name, const char *build,
    maage_instance **output, char error[256])
{
    maage_instance *instance;
    maage_storage_lock *lock = NULL;
    char instances[MAAGE_PATH_CAPACITY], created[32];
    struct json_object *folders, *association;
    time_t now;
    struct tm *utc;
    unsigned int i;
    int ok = 0;
    if (output) *output = NULL;
    if (error) error[0] = 0;
    if (!output || !maage_instance_text(name,96) || !maage_instance_build(build))
        return maage_instance_error(error,"Instance names and Client build identifiers must be valid and within their size limits.");
    instance = (maage_instance *)calloc(1,sizeof(*instance));
    if (!instance) return maage_instance_error(error,"Not enough memory to create the instance.");
    if (!maage_instance_roots(root,instance->root,instances,1,error)) goto done;
    if (!maage_storage_uuid_generate(instance->info.id)) {
        maage_instance_error(error,"Could not obtain OS randomness for a new instance UUID."); goto done;
    }
    if (!maage_storage_join(instance->directory,sizeof(instance->directory),instances,instance->info.id) ||
        !(lock = maage_instance_lock(instance,error))) goto done;
    if (maage_storage_status(instance->directory) != 0 || !maage_storage_directories(instance->directory)) {
        maage_instance_error(error,"Could not safely create a unique physical instance."); goto done;
    }
    instance->manifest = json_object_new_object(); folders = json_object_new_object();
    if (!instance->manifest || !folders) {
        if (folders) json_object_put(folders);
        maage_instance_error(error,"Not enough memory to create the instance manifest."); goto done;
    }
    json_object_object_add(instance->manifest,"schema",json_object_new_int(1));
    json_object_object_add(instance->manifest,"id",json_object_new_string(instance->info.id));
    json_object_object_add(instance->manifest,"name",json_object_new_string(name));
    json_object_object_add(instance->manifest,"clientBuildId",json_object_new_string(build));
    json_object_object_add(instance->manifest,"folders",folders);
    now = time(NULL); utc = now == (time_t)-1 ? NULL : gmtime(&now);
    if (utc && strftime(created,sizeof(created),"%Y-%m-%dT%H:%M:%SZ",utc) == 20)
        json_object_object_add(instance->manifest,"createdUtc",json_object_new_string(created));
    for (i = 0; i < MAAGE_INSTANCE_FOLDER_COUNT; ++i) {
        association = json_object_new_object();
        if (!association) { maage_instance_error(error,"Not enough memory to create folder associations."); goto done; }
        json_object_object_add(association,"mode",json_object_new_string("private"));
        json_object_object_add(folders,maage_instance_folder_names[i],association);
    }
    if (!maage_instance_decode(instance,error) || !maage_instance_manifest_write(instance,error)) goto done;
    *output = instance; instance = NULL; ok = 1;
done:
    maage_storage_release(lock); maage_instance_close(instance); return ok;
}
void maage_instance_close(maage_instance *instance)
{
    if (!instance) return;
    maage_storage_release(instance->use);
    if (instance->manifest) json_object_put(instance->manifest);
    free(instance);
}
const maage_instance_info *maage_instance_get_info(const maage_instance *instance)
{ return instance ? &instance->info : NULL; }
const char *maage_instance_directory(const maage_instance *instance)
{ return instance && !instance->archived ? instance->directory : NULL; }
int maage_instance_resolve_path(const maage_instance *instance,
    maage_instance_folder folder, const char *relative, char *path, size_t capacity)
{
    const char *base;
    if (!instance || instance->archived || !path || folder < 0 || folder >= MAAGE_INSTANCE_FOLDER_COUNT) return 0;
    base = instance->folders[folder].target;
    if (relative && *relative) {
        size_t i;
        for (i = 0; relative[i]; ++i)
            if (relative[i] == '/' && (!relative[i+1] || relative[i+1] == '/')) return 0;
        return maage_storage_join(path,capacity,base,relative) && maage_folder_link_path_within(base,path);
    }
    if (strlen(base) >= capacity) return 0;
    strcpy(path,base); return 1;
}
maage_instance_mapping maage_instance_folder_mode(const maage_instance *instance,
    maage_instance_folder folder)
{
    return instance && folder >= 0 && folder < MAAGE_INSTANCE_FOLDER_COUNT ?
        instance->folders[folder].mode : MAAGE_INSTANCE_PRIVATE;
}
static void maage_instance_adopt(maage_instance *instance, maage_instance *fresh)
{
    if (instance->manifest) json_object_put(instance->manifest);
    instance->manifest = fresh->manifest; fresh->manifest = NULL;
    instance->info = fresh->info;
    memcpy(instance->folders,fresh->folders,sizeof(instance->folders));
}
static maage_instance *maage_instance_admin(maage_instance *instance,
    maage_storage_lock **lock, char error[256])
{
    maage_instance *fresh = NULL;
    *lock = NULL;
    if (!instance || instance->archived || instance->use) {
        maage_instance_error(error,"An active or archived instance cannot be edited."); return NULL;
    }
    *lock = maage_instance_lock(instance,error);
    if (!*lock) return NULL;
    if (!maage_instance_open(instance->root,instance->info.id,0,&fresh,error)) {
        maage_storage_release(*lock); *lock = NULL;
    }
    return fresh;
}
int maage_instance_update(maage_instance *instance, const char *name,
    const char *build, char error[256])
{
    maage_instance *fresh;
    maage_storage_lock *lock;
    int ok;
    if (!maage_instance_text(name,96) || !maage_instance_build(build))
        return maage_instance_error(error,"Instance names or Client build identifiers are invalid.");
    fresh = maage_instance_admin(instance,&lock,error);
    if (!fresh) return 0;
    json_object_object_add(fresh->manifest,"name",json_object_new_string(name));
    json_object_object_add(fresh->manifest,"clientBuildId",json_object_new_string(build));
    ok = maage_instance_decode(fresh,error) && maage_instance_manifest_write(fresh,error);
    if (ok) maage_instance_adopt(instance,fresh);
    maage_instance_close(fresh); maage_storage_release(lock); return ok;
}
static int maage_instance_empty_entry(const char *name, maage_folder_link_kind kind, void *context)
{
    (void)name; (void)kind; *(int *)context = 0; return 1;
}
static int maage_instance_clear_empty(const char *path)
{
    maage_folder_link_kind kind = maage_folder_link_status(path);
    int empty = 1;
    if (kind == MAAGE_FOLDER_LINK_MISSING) return 1;
    if (kind == MAAGE_FOLDER_LINK_LINK || kind == MAAGE_FOLDER_LINK_BROKEN) return maage_folder_link_remove(path);
    if (kind != MAAGE_FOLDER_LINK_DIRECTORY || !maage_folder_link_list(path,maage_instance_empty_entry,&empty) || !empty) return 0;
    maage_storage_remove_directory(path); return maage_folder_link_status(path) == MAAGE_FOLDER_LINK_MISSING;
}
static int maage_instance_remap_empty(const char *path, maage_folder_link_kind kind)
{
    int empty = 1;
    if (kind == MAAGE_FOLDER_LINK_MISSING || kind == MAAGE_FOLDER_LINK_LINK || kind == MAAGE_FOLDER_LINK_BROKEN) return 1;
    return kind == MAAGE_FOLDER_LINK_DIRECTORY &&
        maage_folder_link_list(path,maage_instance_empty_entry,&empty) && empty;
}
int maage_instance_remap(maage_instance *instance, maage_instance_folder folder,
    maage_instance_mapping mode, const char *custom, int user_selected, char error[256])
{
    maage_instance *fresh;
    maage_storage_lock *lock;
    maage_instance_association old;
    struct json_object *folders, *association;
    char destination[MAAGE_PATH_CAPACITY], staging[MAAGE_PATH_CAPACITY], backup[MAAGE_PATH_CAPACITY], relative[80];
    maage_folder_link_kind kind;
    int ok = 0, staged = 0, retained = 0, installed = 0;
    if (folder < 0 || folder >= MAAGE_INSTANCE_FOLDER_COUNT || mode < MAAGE_INSTANCE_PRIVATE || mode > MAAGE_INSTANCE_CUSTOM ||
        (mode == MAAGE_INSTANCE_CUSTOM && !user_selected))
        return maage_instance_error(error,"Custom folder changes require an explicit user directory selection.");
    fresh = maage_instance_admin(instance,&lock,error);
    if (!fresh) return 0;
    old = fresh->folders[folder];
    if (!maage_instance_target(fresh,(unsigned int)folder,mode,custom,1,error) ||
        !maage_instance_conflicts(fresh,error) ||
        !maage_storage_join(destination,sizeof(destination),fresh->directory,maage_instance_folder_names[folder])) goto done;
    if (old.mode == mode && !strcmp(old.target,fresh->folders[folder].target)) {
        ok = maage_instance_prepare(fresh,error);
        if (ok) maage_instance_adopt(instance,fresh);
        goto done;
    }
    if (mode == MAAGE_INSTANCE_SHARED && !maage_storage_directories(fresh->folders[folder].target)) {
        maage_instance_error(error,"Could not prepare the managed shared folder."); goto done;
    }
    kind = maage_folder_link_status(destination);
    if ((kind == MAAGE_FOLDER_LINK_LINK || kind == MAAGE_FOLDER_LINK_BROKEN) &&
        (old.mode == MAAGE_INSTANCE_PRIVATE || !maage_folder_link_matches(destination,old.target))) {
        maage_instance_error(error,"The existing link conflicts with its manifest; the folder was preserved."); goto done;
    }
    if (!maage_instance_remap_empty(destination,kind)) {
        maage_instance_error(error,"The folder contains data or is unsafe; no files were moved or removed."); goto done;
    }
    strcpy(relative,"."); strcat(relative,maage_instance_folder_names[folder]); strcat(relative,".mapping-new");
    if (!maage_storage_join(staging,sizeof(staging),fresh->directory,relative)) {
        maage_instance_error(error,"The folder association staging path is too long."); goto done;
    }
    strcpy(relative,"."); strcat(relative,maage_instance_folder_names[folder]); strcat(relative,".mapping-old");
    if (!maage_storage_join(backup,sizeof(backup),fresh->directory,relative)) {
        maage_instance_error(error,"The folder association recovery path is too long."); goto done;
    }
    if (maage_folder_link_status(staging) != MAAGE_FOLDER_LINK_MISSING ||
        maage_folder_link_status(backup) != MAAGE_FOLDER_LINK_MISSING) {
        maage_instance_error(error,"A previous folder-mapping operation retained recovery entries; preserve and resolve them before remapping."); goto done;
    }
    if (mode == MAAGE_INSTANCE_PRIVATE ? !maage_storage_directories(staging) :
        !maage_folder_link_create(staging,fresh->folders[folder].target)) {
        maage_instance_error(error,"Could not establish the selected folder association."); goto done;
    }
    staged = 1;
    /* Retain the exact old link, including a broken link, rather than trying to
     * reconstruct it after a failed write. Only empty directories reach here.
     * The UUID-scoped lock excludes all cooperating instance users/admins. */
    if (kind != MAAGE_FOLDER_LINK_MISSING) {
        if (!maage_storage_replace(destination,backup)) {
            maage_instance_error(error,"Could not retain the previous folder association; it was preserved."); goto done;
        }
        retained = 1;
    }
    if (!maage_storage_replace(staging,destination)) {
        maage_instance_error(error,"Could not promote the selected folder association."); goto done;
    }
    staged = 0; installed = 1;
    association = json_object_new_object();
    if (!association) { maage_instance_error(error,"Not enough memory to save the folder association."); goto done; }
    json_object_object_add(association,"mode",json_object_new_string(mode == MAAGE_INSTANCE_PRIVATE ? "private" : (mode == MAAGE_INSTANCE_SHARED ? "shared" : "custom")));
    if (mode == MAAGE_INSTANCE_SHARED)
        json_object_object_add(association,"library",json_object_new_string(maage_instance_folder_names[folder]));
    else if (mode == MAAGE_INSTANCE_CUSTOM) {
        json_object_object_add(association,"path",json_object_new_string(fresh->folders[folder].target));
        json_object_object_add(association,"userSelected",json_object_new_boolean(1));
    }
    json_object_object_get_ex(fresh->manifest,"folders",&folders);
    json_object_object_add(folders,maage_instance_folder_names[folder],association);
    if (!maage_instance_decode(fresh,error) || !maage_instance_manifest_write(fresh,error)) goto done;
    maage_instance_adopt(instance,fresh); ok = 1;
done:
    if (!ok) {
        int restored = 1;
        if (installed && !maage_instance_clear_empty(destination)) restored = 0;
        if (retained && (maage_folder_link_status(destination) != MAAGE_FOLDER_LINK_MISSING ||
            !maage_storage_replace(backup,destination))) restored = 0;
        if (staged && !maage_instance_clear_empty(staging)) restored = 0;
        if (!restored) maage_instance_error(error,"Folder mapping failed; retained mapping recovery entries and all target data were preserved.");
    } else if (retained && !maage_instance_clear_empty(backup)) {
        maage_instance_error(error,"The new mapping was saved; a retained old mapping entry requires manual cleanup.");
    }
    maage_instance_close(fresh); maage_storage_release(lock); return ok;
}
int maage_instance_archive(maage_instance *instance, char error[256])
{
    maage_instance *fresh;
    maage_storage_lock *lock;
    char relative[64], destination[MAAGE_PATH_CAPACITY], instances[MAAGE_PATH_CAPACITY];
    int ok = 0;
    fresh = maage_instance_admin(instance,&lock,error);
    if (!fresh) return 0;
    strcpy(relative,".deleted-"); strcat(relative,instance->info.id);
    if (maage_storage_join(instances,sizeof(instances),instance->root,"instances") &&
        maage_storage_join(destination,sizeof(destination),instances,relative) &&
        maage_folder_link_move(fresh->directory,destination) == 1) {
        instance->archived = 1; ok = 1;
    } else maage_instance_error(error,"Could not archive the instance safely; its files were preserved.");
    maage_instance_close(fresh); maage_storage_release(lock); return ok;
}
static struct json_object *maage_instance_launcher(const char *root, char error[256])
{
    char normalized[MAAGE_PATH_CAPACITY], instances[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY];
    struct json_object *object, *schema;
    if (!maage_instance_roots(root,normalized,instances,0,error) ||
        !maage_storage_join(path,sizeof(path),normalized,"config/launcher.json") ||
        !(object = maage_instance_json_read(path,error))) return NULL;
    if (!json_object_object_get_ex(object,"schema",&schema) || !json_object_is_type(schema,json_type_int) ||
        json_object_get_int(schema) != 3) {
        json_object_put(object); maage_instance_error(error,"Open the Launcher to choose or migrate a physical instance."); return NULL;
    }
    return object;
}
int maage_instance_select(const char *root, const char *requested, int acquire_use,
    maage_instance **output, char error[256])
{
    struct json_object *launcher, *installations;
    char selected[37], referenced[37];
    size_t i;
    int found, registered = 0;
    if (output) *output = NULL;
    if (requested) return maage_instance_open(root,requested,acquire_use,output,error);
    launcher = maage_instance_launcher(root,error);
    if (!launcher) return 0;
    found = maage_instance_string(launcher,"selected",selected,sizeof(selected)) && maage_storage_uuid_valid(selected);
    if (!found) found = maage_instance_string(launcher,"default",selected,sizeof(selected)) && maage_storage_uuid_valid(selected);
    if (found && json_object_object_get_ex(launcher,"installations",&installations) &&
        json_object_is_type(installations,json_type_array)) {
        for (i = 0; i < json_object_array_length(installations); ++i)
            if (maage_instance_string(json_object_array_get_idx(installations,i),"uuid",referenced,sizeof(referenced)) &&
                !strcmp(selected,referenced)) { registered = 1; break; }
    }
    json_object_put(launcher);
    if (!found || !registered) return maage_instance_error(error,"Select an instance in the Launcher or provide --instance=<uuid>.");
    return maage_instance_open(root,selected,acquire_use,output,error);
}
int maage_instance_only_configured(const maage_instance *instance)
{
    struct json_object *launcher, *installations, *entry;
    char id[37], error[256];
    int only = 0;
    if (!instance || !(launcher = maage_instance_launcher(instance->root,error))) return 0;
    if (json_object_object_get_ex(launcher,"installations",&installations) &&
        json_object_is_type(installations,json_type_array) && json_object_array_length(installations) == 1) {
        entry = json_object_array_get_idx(installations,0);
        only = maage_instance_string(entry,"uuid",id,sizeof(id)) && !strcmp(id,instance->info.id);
    }
    json_object_put(launcher); return only;
}
