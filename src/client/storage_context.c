#include "storage_context.h"
#include "maage/log.h"
#include "maage/folder_link.h"
#include <stdio.h>
#include <string.h>

static maage_instance *maage_client_active_instance;
static char maage_client_data_root[MAAGE_PATH_CAPACITY];
static char maage_client_log_path[MAAGE_PATH_CAPACITY];
static int maage_client_migrate_requested;

static int maage_client_storage_map(maage_instance *instance, const char *mapping, char error[256])
{
    static const char *const names[3] = {"resourcepacks:", "shaderpacks:", "screenshots:"};
    static const maage_instance_folder folders[3] = {MAAGE_INSTANCE_FOLDER_RESOURCEPACKS,
        MAAGE_INSTANCE_FOLDER_SHADERPACKS, MAAGE_INSTANCE_FOLDER_SCREENSHOTS};
    maage_instance_mapping mode;
    const char *value = NULL, *custom = NULL;
    unsigned int i;
    for (i = 0; i < 3; ++i)
        if (!strncmp(mapping, names[i], strlen(names[i]))) { value = mapping + strlen(names[i]); break; }
    if (!value) { strcpy(error, "Only resourcepacks, shaderpacks and screenshots support folder mappings."); return 0; }
    if (!strcmp(value, "private")) mode = MAAGE_INSTANCE_PRIVATE;
    else if (!strcmp(value, "shared")) mode = MAAGE_INSTANCE_SHARED;
    else if (!strncmp(value, "custom:", 7) && value[7]) { mode = MAAGE_INSTANCE_CUSTOM; custom = value + 7; }
    else { strcpy(error, "Folder mapping mode must be private, shared, or custom:<absolute directory>."); return 0; }
    return maage_instance_remap(instance, folders[i], mode, custom, custom != NULL, error);
}

maage_instance *maage_client_storage_instance(void) { return maage_client_active_instance; }
const char *maage_client_storage_data_root(void) { return maage_client_data_root; }
int maage_client_storage_migrate_requested(void) { return maage_client_migrate_requested; }

int maage_application_configure_log(int argc, char *const argv[], maage_log_config *config)
{
    const char *root = NULL, *id = NULL, *mapping = NULL;
    char error[256];
    int i;
    for (i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "--data-directory=", 17)) {
            if (root) goto arguments;
            root = argv[i] + 17;
        } else if (!strncmp(argv[i], "--instance=", 11)) {
            if (id) goto arguments;
            id = argv[i] + 11;
        } else if (!strncmp(argv[i], "--map-folder=", 13)) {
            if (mapping) goto arguments;
            mapping = argv[i] + 13;
        } else if (!strcmp(argv[i], "--migrate-legacy-worlds")) maage_client_migrate_requested = 1;
        else if (!strcmp(argv[i], "--instance") || !strcmp(argv[i], "--data-directory") ||
            !strcmp(argv[i], "--map-folder")) goto arguments;
    }
    if (root) {
        if (!*root || strlen(root) >= sizeof(maage_client_data_root)) goto arguments;
        strcpy(maage_client_data_root, root);
    } else if (!maage_storage_data_path(maage_client_data_root, sizeof(maage_client_data_root))) goto arguments;
    if ((id && !maage_storage_uuid_valid(id)) ||
        !maage_folder_link_managed_path(maage_client_data_root, 0)) goto arguments;
    if (mapping) {
        if (!id) goto arguments;
        if (!maage_instance_select(maage_client_data_root, id, 0, &maage_client_active_instance, error)) {
            fprintf(stderr, "MaaGE Client instance: %s\n", error); return 0;
        }
        i = maage_client_storage_map(maage_client_active_instance, mapping, error);
        maage_instance_close(maage_client_active_instance); maage_client_active_instance = NULL;
        if (!i) { fprintf(stderr, "MaaGE instance folder mapping: %s\n", error); return 0; }
    }
    if (!maage_instance_select(maage_client_data_root, id, 1, &maage_client_active_instance, error)) {
        fprintf(stderr, "MaaGE Client instance: %s\n", error); return 0;
    }
    if (!maage_instance_resolve_path(maage_client_active_instance, MAAGE_INSTANCE_FOLDER_LOGS,
        "client.log", maage_client_log_path, sizeof(maage_client_log_path))) {
        fprintf(stderr, "MaaGE Client cannot resolve its private instance log.\n");
        maage_instance_close(maage_client_active_instance); maage_client_active_instance = NULL; return 0;
    }
    config->file = 1; config->file_path = maage_client_log_path;
    return 1;
arguments:
    fprintf(stderr, "Use --instance=<UUID> for an existing instance, optionally --data-directory=<absolute MaaGE root>. Folder changes require an explicit --instance and one --map-folder=<kind>:private|shared|custom:<absolute directory>. Open the Launcher to create/select an instance.\n");
    return 0;
}

void maage_application_finish(void)
{
    maage_instance_close(maage_client_active_instance); maage_client_active_instance = NULL;
}
