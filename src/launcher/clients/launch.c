#include "launch.h"
#include "maage/log.h"
#include "maage/instance.h"
#include <stdlib.h>
#include <string.h>
maage_status maage_client_launch(const char *root, const char *id,
    const char *instance_id, maage_access *access, maage_process **process,
    char error[256], maage_status_trace *trace)
{
    maage_client_registry *registry = NULL;
    char executable[MAAGE_PATH_CAPACITY], directory[MAAGE_PATH_CAPACITY], logs[MAAGE_PATH_CAPACITY];
    char log[MAAGE_PATH_CAPACITY], data_argument[MAAGE_PATH_CAPACITY + 18], instance_argument[48];
    const char *argv[4];
    maage_instance *instance = NULL;
    maage_status result = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_START, 0);
    if (process) *process = NULL;
    if (!root || !id || !instance_id || !process || !error) return result;
    error[0] = 0;
    if (!access || !maage_access_allowed(access)) {
        strcpy(error, "Verify the selected Minecraft account before starting a Client.");
        result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_VERIFY, 0); goto done;
    }
    registry = (maage_client_registry *)calloc(1, sizeof(*registry));
    if (!registry) { result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_LOAD, 0); goto done; }
    result = maage_clients_load(registry, root, error, trace);
    if (maage_status_failed(result)) goto done;
    result = maage_clients_path(registry, id, executable, trace);
    if (maage_status_failed(result)) goto done;
    result = maage_instance_open(root, instance_id, 0, &instance, error, trace);
    if (maage_status_failed(result)) goto done;
    if (strcmp(maage_instance_get_info(instance)->client_build, id)) {
        strcpy(error, "Instance Client assignment changed. Select the instance again before launching.");
        result = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SELECT, 0); goto done;
    }
    strcpy(directory, maage_instance_directory(instance));
    result = maage_instance_resolve_path(instance, MAAGE_INSTANCE_FOLDER_LOGS, NULL, logs, sizeof(logs));
    if (maage_status_failed(result)) goto done;
    result = maage_storage_directories(logs);
    if (maage_status_failed(result)) goto done;
    result = maage_storage_join(log, sizeof(log), logs, "client.log");
    if (maage_status_failed(result)) goto done;
    if (!maage_access_allowed(access)) { result = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_VERIFY, 0); goto done; }
    sprintf(data_argument, "--data-directory=%s", root);
    sprintf(instance_argument, "--instance=%s", instance_id);
    argv[0] = executable; argv[1] = data_argument; argv[2] = instance_argument; argv[3] = NULL;
    result = maage_process_start(executable, argv, directory, log, log, process, error, trace);
    if (!maage_status_failed(result)) maage_log_info("maage.launcher.clients",
        "Starting selected Client %.12s directly; combined live log=%s.", id, log);
done:
    if (maage_status_failed(result) && !*error)
        strcpy(error, "Cannot start Client. Check current Minecraft access and permissions for the Client and logs folders.");
    if (!maage_status_is_success(result)) MAAGE_STATUS_TRACE(trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_START);
    maage_instance_close(instance);
    maage_clients_dispose(registry); free(registry);
    return result;
}
