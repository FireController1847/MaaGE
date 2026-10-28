#include "storage_context.h"
#include "maage/log.h"
#include "maage/folder_link.h"
#include <stdio.h>
#include <string.h>

static maage_instance *maage_client_active_instance;
static char maage_client_data_root[MAAGE_PATH_CAPACITY];
static char maage_client_log_path[MAAGE_PATH_CAPACITY];
static int maage_client_storage_retained;
void maage_client_storage_retain(void) { maage_client_storage_retained = 1; }

static maage_status maage_client_storage_map(maage_instance *instance, const char *mapping,
    char error[256], maage_status_trace *trace)
{
    static const char *const names[3] = {"resourcepacks:", "shaderpacks:", "screenshots:"};
    static const maage_instance_folder folders[3] = {MAAGE_INSTANCE_FOLDER_RESOURCEPACKS,
        MAAGE_INSTANCE_FOLDER_SHADERPACKS, MAAGE_INSTANCE_FOLDER_SCREENSHOTS};
    maage_instance_mapping mode;
    const char *value = NULL, *custom = NULL;
    unsigned int i;
    for (i = 0; i < 3; ++i)
        if (!strncmp(mapping, names[i], strlen(names[i]))) { value = mapping + strlen(names[i]); break; }
    if (!value) { strcpy(error, "Only resourcepacks, shaderpacks and screenshots support folder mappings."); return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_PARSE, 1); }
    if (!strcmp(value, "private")) mode = MAAGE_INSTANCE_PRIVATE;
    else if (!strcmp(value, "shared")) mode = MAAGE_INSTANCE_SHARED;
    else if (!strncmp(value, "custom:", 7) && value[7]) { mode = MAAGE_INSTANCE_CUSTOM; custom = value + 7; }
    else { strcpy(error, "Folder mapping mode must be private, shared, or custom:<absolute directory>."); return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_PARSE, 1); }
    return MAAGE_STATUS_PROPAGATE(trace,
        maage_instance_remap(instance, folders[i], mode, custom, custom != NULL, error, trace),
        MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET);
}

static maage_skin_reference maage_client_player;
static int maage_client_has_player;
const maage_skin_reference *maage_client_storage_player(void)
{ return maage_client_has_player ? &maage_client_player : NULL; }

maage_instance *maage_client_storage_instance(void) { return maage_client_active_instance; }
const char *maage_client_storage_data_root(void) { return maage_client_data_root; }

maage_status maage_application_configure_log(int argc, char *const argv[], maage_log_config *config)
{
    const char *root = NULL, *id = NULL, *mapping = NULL, *appearance = NULL;
    char error[256];
    maage_status result;
    maage_status_trace trace;
    int i;
    maage_status_trace_init(&trace);
    MAAGE_STATUS_TRACE(&trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    error[0] = 0;
    for (i = 1; i < argc; ++i) {
        if (!strncmp(argv[i],"--player-appearance=",20)) {
            if (appearance) goto arguments;
            appearance=argv[i]+20;
        } else if (!strncmp(argv[i], "--data-directory=", 17)) {
            if (root) goto arguments;
            root = argv[i] + 17;
        } else if (!strncmp(argv[i], "--instance=", 11)) {
            if (id) goto arguments;
            id = argv[i] + 11;
        } else if (!strncmp(argv[i], "--map-folder=", 13)) {
            if (mapping) goto arguments;
            mapping = argv[i] + 13;
        } else if (!strcmp(argv[i], "--instance") || !strcmp(argv[i], "--data-directory") ||
            !strcmp(argv[i], "--map-folder") || !strcmp(argv[i],"--player-appearance")) goto arguments;
    }
    maage_client_has_player=0;
    if (appearance) {
        result=maage_skin_reference_parse(appearance,&maage_client_player);
        if (maage_status_failed(result)) goto arguments;
        maage_client_has_player=1;
    }
    if (root) {
        if (!*root || strlen(root) >= sizeof(maage_client_data_root)) goto arguments;
        strcpy(maage_client_data_root, root);
    } else {
        result = maage_storage_data_path(maage_client_data_root, sizeof(maage_client_data_root));
        if (maage_status_failed(result)) goto failed;
    }
    if ((id && !maage_storage_uuid_valid(id)) ||
        !maage_folder_link_managed_path(maage_client_data_root, 0)) goto arguments;
    if (mapping) {
        if (!id) goto arguments;
        result = maage_instance_select(maage_client_data_root, id, 0, &maage_client_active_instance, error, &trace);
        if (maage_status_failed(result)) goto failed;
        result = maage_client_storage_map(maage_client_active_instance, mapping, error, &trace);
        maage_instance_close(maage_client_active_instance); maage_client_active_instance = NULL;
        if (maage_status_failed(result)) goto failed;
    }
    result = maage_instance_select(maage_client_data_root, id, 1, &maage_client_active_instance, error, &trace);
    if (maage_status_failed(result)) goto failed;
    result = maage_instance_resolve_path(maage_client_active_instance, MAAGE_INSTANCE_FOLDER_LOGS,
        "client.log", maage_client_log_path, sizeof(maage_client_log_path));
    if (maage_status_failed(result)) goto failed;
    config->file = 1; config->file_path = maage_client_log_path; config->capture_stdio = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
arguments:
    strcpy(error, "Use --instance=<UUID> and optional --data-directory=<absolute MaaGE root>. Folder changes require --instance and --map-folder=<kind>:private|shared|custom:<absolute directory>.");
    result = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 1);
failed:
    MAAGE_STATUS_PROPAGATE(&trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    maage_log_trace_status(MAAGE_LOG_ERROR, "maage.client.instance", result, &trace,
        "Instance startup failed: %s", error[0] ? error : maage_status_string(result));
    maage_instance_close(maage_client_active_instance); maage_client_active_instance = NULL;
    return result;
}

maage_status maage_application_finish(void)
{
    maage_status result;
    if (maage_client_storage_retained) return maage_status_make(MAAGE_STATUS_FATAL, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP, 0);
    result = maage_instance_close(maage_client_active_instance); maage_client_active_instance = NULL;
    return result;
}
