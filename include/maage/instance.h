#ifndef MAAGE_INSTANCE_H
#define MAAGE_INSTANCE_H
#include "maage/storage.h"

typedef enum maage_instance_folder {
    MAAGE_INSTANCE_FOLDER_WORLDS = 0,
    MAAGE_INSTANCE_FOLDER_MODS,
    MAAGE_INSTANCE_FOLDER_CONFIG,
    MAAGE_INSTANCE_FOLDER_RESOURCEPACKS,
    MAAGE_INSTANCE_FOLDER_SHADERPACKS,
    MAAGE_INSTANCE_FOLDER_SCREENSHOTS,
    MAAGE_INSTANCE_FOLDER_LOGS,
    MAAGE_INSTANCE_FOLDER_COUNT
} maage_instance_folder;
typedef enum maage_instance_mapping {
    MAAGE_INSTANCE_PRIVATE = 0,
    MAAGE_INSTANCE_SHARED,
    MAAGE_INSTANCE_CUSTOM
} maage_instance_mapping;
#define MAAGE_INSTANCE_ARGUMENTS_CAPACITY 4096
typedef struct maage_instance_info {
    char id[37], name[96], client_build[65];
    char client_arguments[MAAGE_INSTANCE_ARGUMENTS_CAPACITY];
} maage_instance_info;
typedef struct maage_instance maage_instance;

/* Schema-1 manifests are authoritative; Launcher keeps only UUID references.
 * Empty client_build is an explicitly unassigned instance. Creation uses OS
 * random UUIDs; names never determine paths. Returned handle is read-only.
 * client_arguments is a UTF-8 string (empty means none), at most 4095 bytes
 * without control characters. Launcher owns argument parsing and invocation. */
maage_status maage_instance_create(const char *data_root, const char *name, const char *client_build, const char *client_arguments, maage_instance **instance, char error[256], maage_status_trace *trace);
/* acquire_use holds the UUID-scoped exclusive lock until close. One running
 * Client per instance is currently supported; other instances remain independent.
 * Read-only opening inspects manifests while a Client runs. Use opening checks
 * mappings and creates required private directories/managed links at startup. */
maage_status maage_instance_open(const char *data_root, const char *id, int acquire_use, maage_instance **instance, char error[256], maage_status_trace *trace);
/* NULL requested_id reads selected/default UUID from schema-3 launcher.json.
 * Missing/invalid selection fails; it never invents a random default. */
maage_status maage_instance_select(const char *data_root, const char *requested_id, int acquire_use, maage_instance **instance, char error[256], maage_status_trace *trace);
maage_status maage_instance_close(maage_instance *instance);
const maage_instance_info *maage_instance_get_info(const maage_instance *instance);
const char *maage_instance_directory(const maage_instance *instance);
/* Cached startup paths; no filesystem probes or identity lookup. NULL/empty
 * relative returns the folder root. Relative paths reject traversal and aliases. */
maage_status maage_instance_resolve_path(const maage_instance *instance, maage_instance_folder folder, const char *relative, char *path, size_t capacity);
maage_instance_mapping maage_instance_folder_mode(const maage_instance *instance, maage_instance_folder folder);
/* Administrative operations acquire the same nonblocking use lock, reload the
 * latest manifest, and fail while active. Modpack/unknown root metadata survives.
 * Config/worlds/mods/logs remain private; only the three content folders map.
 * Remaps stage links and retain exact old links/empty folders for rollback.
 * Interrupted changes preserve .<folder>.mapping-* entries for manual recovery. */
maage_status maage_instance_update(maage_instance *instance, const char *name, const char *client_build, const char *client_arguments, char error[256], maage_status_trace *trace);
maage_status maage_instance_remap(maage_instance *instance, maage_instance_folder folder, maage_instance_mapping mode, const char *custom_path, int user_selected, char error[256], maage_status_trace *trace);
/* Deletion archives the complete physical directory as .deleted-<uuid> beneath
 * instances, without recursive deletion or touching linked target contents. */
maage_status maage_instance_archive(maage_instance *instance, char error[256], maage_status_trace *trace);
#endif
