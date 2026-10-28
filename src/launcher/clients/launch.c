#include "launch.h"
#include "maage/log.h"
#include "maage/instance.h"
#include "../access/skin_cache.h"
#include <stdlib.h>
#include <string.h>
/* One quoting grammar for every native editor; this never invokes a shell.
 * Backslashes escape quotes, backslashes and spaces, except in single quotes. */
#define MAAGE_CLIENT_ARGUMENT_COUNT (MAAGE_INSTANCE_ARGUMENTS_CAPACITY / 2 + 1)
static maage_status maage_client_arguments_parse(const char *text, char *storage,
    const char **argv, unsigned *count, char error[256], maage_status_trace *trace)
{
    const char *p = text;
    char *out = storage, quote;
    unsigned char c;
    *count = 0;
    if (!text || strlen(text) >= MAAGE_INSTANCE_ARGUMENTS_CAPACITY) {
        strcpy(error, "Client arguments must fit within 4095 UTF-8 bytes."); goto invalid;
    }
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        argv[(*count)++] = out;
        quote = 0;
        while (*p && (quote || *p != ' ')) {
            c = (unsigned char)*p++;
            if (c < 32 || c == 127) {
                strcpy(error, "Client arguments cannot contain control characters."); goto invalid;
            }
            if (c == '\\' && quote != '\'' &&
                (*p == '\\' || *p == '"' || (!quote && (*p == '\'' || *p == ' ')))) {
                *out++ = *p++;
            } else if (c == '"' || c == '\'') {
                if (!quote) quote = (char)c;
                else if (quote == (char)c) quote = 0;
                else *out++ = (char)c;
            } else *out++ = (char)c;
        }
        if (quote) { strcpy(error, "Close the quoted value in Client arguments."); goto invalid; }
        *out++ = 0;
        if (!strcmp(argv[*count - 1], "--player-appearance") ||
            !strncmp(argv[*count - 1], "--player-appearance=", 20) ||
            !strcmp(argv[*count - 1], "--instance") ||
            !strncmp(argv[*count - 1], "--instance=", 11) ||
            !strcmp(argv[*count - 1], "--data-directory") ||
            !strncmp(argv[*count - 1], "--data-directory=", 17)) {
            strcpy(error, "The Launcher supplies --instance, --data-directory and --player-appearance automatically."); goto invalid;
        }
    }
    argv[*count] = NULL;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PARSE, 0);
invalid:
    return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_PARSE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_PARSE);
}
maage_status maage_client_arguments_validate(const char *text, char error[256], maage_status_trace *trace)
{
    char storage[MAAGE_INSTANCE_ARGUMENTS_CAPACITY];
    const char *argv[MAAGE_CLIENT_ARGUMENT_COUNT];
    unsigned count;
    return maage_client_arguments_parse(text, storage, argv, &count, error, trace);
}
maage_status maage_client_launch(const char *root, const char *id,
    const char *instance_id, maage_access *access, maage_process **process,
    char error[256], maage_status_trace *trace)
{
    maage_client_registry *registry = NULL;
    char executable[MAAGE_PATH_CAPACITY], directory[MAAGE_PATH_CAPACITY], logs[MAAGE_PATH_CAPACITY];
    char log[MAAGE_PATH_CAPACITY], data_argument[MAAGE_PATH_CAPACITY + 18], instance_argument[48];
    char arguments[MAAGE_INSTANCE_ARGUMENTS_CAPACITY];
    char appearance[MAAGE_SKIN_ARGUMENT_CAPACITY], appearance_argument[MAAGE_SKIN_ARGUMENT_CAPACITY+24];
    maage_skin_reference player, selected;
    maage_status skin_result;
    maage_status_trace skin_trace;
    const char *argv[MAAGE_CLIENT_ARGUMENT_COUNT + 4];
    unsigned argument_count;
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
    result = maage_access_player(access,&player);
    if (maage_status_failed(result)) { strcpy(error,"Select a verified Minecraft account before starting a Client."); goto done; }
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
    maage_status_trace_init(&skin_trace);
    skin_result = maage_launcher_skin_cached(root,player.uuid,player.name,&selected,&skin_trace);
    if (!maage_status_failed(skin_result)) player = selected;
    else if (maage_status_code(skin_result)!=MAAGE_STATUS_NOT_FOUND)
        maage_log_trace_status(MAAGE_LOG_WARNING,"maage.launcher.skins",skin_result,&skin_trace,
            "Selected skin cache unavailable; starting with the original default appearance.");
    result = maage_skin_reference_format(&player,appearance);
    if (maage_status_failed(result)) goto done;
    sprintf(appearance_argument,"--player-appearance=%s",appearance);
    sprintf(data_argument, "--data-directory=%s", root);
    sprintf(instance_argument, "--instance=%s", instance_id);
    argv[0] = executable; argv[1] = data_argument; argv[2] = instance_argument; argv[3] = appearance_argument;
    result = maage_client_arguments_parse(maage_instance_get_info(instance)->client_arguments,
        arguments, argv + 4, &argument_count, error, trace);
    if (maage_status_failed(result)) goto done;
    result = maage_access_player(access,&selected);
    if (maage_status_failed(result) || strcmp(player.uuid,selected.uuid)) {
        result=maage_status_make(MAAGE_STATUS_ACCESS_DENIED,MAAGE_CONTEXT_AUTH,MAAGE_OPERATION_SELECT,0);
        strcpy(error,"Minecraft account selection or verification changed before launch. Retry Play."); goto done;
    }
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
