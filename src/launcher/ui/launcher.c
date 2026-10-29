#include "launcher.h"
#include "../clients/launch.h"
#include "maage/log.h"
#include "../access/verify.h"
#include <json-c/json.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#define MAAGE_LAUNCHER_PREFERENCES_BYTES (8UL * 1024UL * 1024UL)
#if MAAGE_LAUNCHER_DEVELOPMENT
static maage_status maage_launcher_client_finish_replace(maage_launcher_ui *ui);
#endif

static maage_status maage_launcher_reserve(maage_launcher_ui *ui, unsigned wanted) {
    unsigned capacity, previous_capacity = ui->capacity;
    maage_installation *installations;
    if (wanted <= ui->capacity) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_REGISTER, 0);
    capacity = ui->capacity ? ui->capacity : 8;
    while (capacity < wanted) {
        if (capacity > UINT_MAX / 2) {
            capacity = wanted;
            break;
        }
        capacity *= 2;
    }
    if ((size_t) capacity > (size_t) - 1 / sizeof(*installations)) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER);
    installations = (maage_installation *) realloc(ui->installations, (size_t) capacity * sizeof(*installations));
    if (!installations) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER);
    memset(installations + previous_capacity, 0, (size_t) (capacity - previous_capacity) * sizeof(*installations));
    ui->installations = installations;
    ui->capacity = capacity;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
static unsigned maage_launcher_number(struct json_object *object, const char *key) {
    struct json_object *value;
    int number;
    if (!json_object_object_get_ex(object, key, &value) || !json_object_is_type(value, json_type_int)) return 0;
    number = json_object_get_int(value);
    return number > 0 ? (unsigned) number : 0;
}
static int maage_launcher_name(const char *name) {
    const unsigned char *p = (const unsigned char *) name;
    int nonblank = 0;
    if (!name || !*name || strlen(name) >= 96) return 0;
    while (*p) {
        if (*p < 32 || *p == 127) return 0;
        if (*p != ' ') nonblank = 1;
        ++p;
    }
    return nonblank;
}
static maage_status maage_launcher_path(maage_launcher_ui *ui, char *path, const char *file) {
    char directory[MAAGE_PATH_CAPACITY];
    maage_status result = maage_storage_join(directory, sizeof(directory), ui->root, "config");
    if (!maage_status_failed(result)) result = maage_storage_directories(directory);
    if (!maage_status_failed(result)) result = maage_storage_join(path, MAAGE_PATH_CAPACITY, directory, file);
    return MAAGE_STATUS_PROPAGATE(&ui->trace, result, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_RESOLVE);
}
static maage_status maage_launcher_json_set(struct json_object *object, const char *key, struct json_object *value) {
    if (!value) return MAAGE_STATUS_PROPAGATE(NULL, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER);
    if (json_object_object_add(object, key, value)) {
        json_object_put(value);
        return MAAGE_STATUS_PROPAGATE(NULL, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_launcher_ui_save(maage_launcher_ui *ui) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    char path[MAAGE_PATH_CAPACITY], temporary[MAAGE_PATH_CAPACITY], lock_path[MAAGE_PATH_CAPACITY];
    struct json_object *root = NULL, *list = NULL, *item;
    const char *text;
    maage_storage_lock *lock = NULL;
    FILE *file;
    size_t length;
    unsigned i;
    int ok = 0;
    if (maage_status_failed(child_result = maage_launcher_path(ui, path, "launcher.json")) || maage_status_failed(child_result = maage_launcher_path(ui, temporary, "launcher.json.tmp")) || maage_status_failed(child_result = maage_launcher_path(ui, lock_path, "launcher.lock"))) goto failed;
    child_result = maage_storage_acquire(lock_path, &lock, &ui->trace);
    if (maage_status_failed(child_result)) goto failed;
    root = json_object_new_object();
    list = json_object_new_array();
    if (!root || !list || maage_status_failed(child_result = maage_launcher_json_set(root, "schema", json_object_new_int(3)))) goto release;
    for (i = 0; i < ui->count; ++i) {
        if (ui->installations[i].id == ui->selected && maage_status_failed(child_result = maage_launcher_json_set(root, "selected", json_object_new_string(ui->installations[i].uuid)))) goto release;
        if (ui->installations[i].id == ui->default_id && maage_status_failed(child_result = maage_launcher_json_set(root, "default", json_object_new_string(ui->installations[i].uuid)))) goto release;
    }
    if (maage_status_failed(child_result = maage_launcher_json_set(root, "appearance", json_object_new_int(ui->appearance))) || maage_status_failed(child_result = maage_launcher_json_set(root, "activeAccount", json_object_new_string(ui->account)))) goto release;
    for (i = 0; i < ui->count; ++i) {
        item = json_object_new_object();
        if (!item) goto release;
        if (maage_status_failed(child_result = maage_launcher_json_set(item, "uuid", json_object_new_string(ui->installations[i].uuid))) || json_object_array_add(list, item)) {
            json_object_put(item);
            goto release;
        }
    }
    if (json_object_object_add(root, "installations", list)) goto release;
    list = NULL;
    text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    if (!text || (length = strlen(text)) > MAAGE_LAUNCHER_PREFERENCES_BYTES) goto release;
    file = maage_storage_open(temporary, "wb");
    if (!file) child_result = maage_status_from_errno(errno ? errno : EIO, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN);
    if (file) {
        ok = fwrite(text, 1, length, file) == length && !maage_status_failed(child_result = maage_storage_sync(file));
        if (fclose(file)) {
            ok = 0;
            if (!maage_status_failed(child_result)) child_result = maage_status_from_errno(errno ? errno : EIO, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_CLOSE);
        }
        if (ok) ok = !maage_status_failed(child_result = maage_storage_replace(temporary, path));
    }
    if (!ok) maage_storage_remove(temporary);
release:
    if (list) json_object_put(list);
    if (root) json_object_put(root);
    maage_storage_release(lock);
    if (ok) {
        ui->error[0] = 0;
        return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SAVE, 0);
    }
failed:
    strcpy(ui->error, "Cannot save Launcher preferences. Check data-folder permissions, free memory, or close another Launcher.");
    if (!maage_status_failed(child_result)) child_result = maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SAVE, 0);
    MAAGE_STATUS_PROPAGATE(&ui->trace, child_result, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SAVE);
    maage_log_trace_status(MAAGE_LOG_ERROR, "maage.launcher.ui", child_result, &ui->trace, "%s", ui->error);
    return child_result;
}
static maage_status maage_launcher_commit(maage_launcher_ui *ui, const maage_launcher_ui *previous) {
    char error[256];
    maage_status result = maage_launcher_ui_save(ui);
    maage_status_trace trace;
    if (!maage_status_failed(result)) return result;
    trace = ui->trace;
    strcpy(error, ui->error);
    *ui = *previous;
    ui->trace = trace;
    ui->result = result;
    strcpy(ui->error, error);
    return result;
}
static void maage_launcher_installation_info(maage_installation *installation, const maage_instance *instance) {
    const maage_instance_info *info = maage_instance_get_info(instance);
    strcpy(installation->uuid, info->id);
    strcpy(installation->name, info->name);
    strcpy(installation->client, info->client_build);
    strcpy(installation->arguments, info->client_arguments);
}
maage_status maage_launcher_installation_write(maage_launcher_ui *ui, int index, const char *name, const char *client, const char *arguments) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    maage_launcher_ui previous;
    maage_instance *instance = NULL;
    char error[256];
    int created = index < 0, ok;
    child_result = maage_client_arguments_validate(arguments, ui->error, &ui->trace);
    if (maage_status_failed(child_result)) return child_result;
    if (!maage_launcher_name(name)) {
        strcpy(ui->error, "Enter a name of 1–95 UTF-8 bytes without control characters.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
    }
    if (ui->client_job) {
        strcpy(ui->error, "Wait for the Client operation to finish.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
    }
    if (!client) client = "";
    if (*client && maage_clients_find(ui->clients, client) < 0 && (index < 0 || (unsigned) index >= ui->count || strcmp(ui->installations[index].client, client))) {
        strcpy(ui->error, "Choose a registered Client build.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
    }
    if (created) {
        if (ui->next_id >= 2147483647U) {
            strcpy(ui->error, "Instance identity exhausted.");
            return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
        }
        if (maage_status_failed(child_result = maage_launcher_reserve(ui, ui->count + 1))) {
            strcpy(ui->error, "Cannot allocate another Launcher installation.");
            return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
        }
        previous = *ui;
        if (maage_status_failed(child_result = maage_instance_create(ui->root, name, client, arguments, &instance, ui->error, &ui->trace))) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
        index = (int) ui->count++;
        ui->installations[index].id = ui->next_id++;
    } else {
        if ((unsigned) index >= ui->count) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
        previous = *ui;
        if (maage_status_failed(child_result = maage_instance_open(ui->root, ui->installations[index].uuid, 0, &instance, ui->error, &ui->trace)) || maage_status_failed(child_result = maage_instance_update(instance, name, client, arguments, ui->error, &ui->trace))) {
            maage_instance_close(instance);
            return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
        }
    }
    maage_launcher_installation_info(&ui->installations[index], instance);
    ok = created ? (!maage_status_failed(child_result = maage_launcher_commit(ui, &previous))) : 1;
    if (!ok) {
        strcpy(error, ui->error);
        maage_instance_archive(instance, ui->error, &ui->trace);
        strcpy(ui->error, error);
    } else
        ui->error[0] = 0;
    maage_instance_close(instance);
    return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(((ok) ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_INVALID_STATE), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
}
maage_status maage_launcher_installation_remove(maage_launcher_ui *ui, unsigned index) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    maage_launcher_ui previous = *ui;
    maage_instance *instance = NULL;
    maage_installation removed;
    char error[256];
    unsigned id;
    if (index >= ui->count) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
    if (ui->count == 1) {
        strcpy(ui->error, "Keep at least one MaaGE instance.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
    }
    if (ui->client_job) {
        strcpy(ui->error, "Wait for the Client operation to finish.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
    }
    if (maage_status_failed(child_result = maage_instance_open(ui->root, ui->installations[index].uuid, 0, &instance, ui->error, &ui->trace))) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
    id = ui->installations[index].id;
    removed = ui->installations[index];
    --ui->count;
    memmove(&ui->installations[index], &ui->installations[index + 1], (ui->count - index) * sizeof(ui->installations[0]));
    if (ui->default_id == id) ui->default_id = ui->installations[0].id;
    if (ui->selected == id) ui->selected = ui->default_id;
    if (maage_status_failed(child_result = maage_launcher_commit(ui, &previous))) {
        memmove(&ui->installations[index + 1], &ui->installations[index], (ui->count - index - 1) * sizeof(ui->installations[0]));
        ui->installations[index] = removed;
        maage_instance_close(instance);
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
    }
    if (maage_status_failed(child_result = maage_instance_archive(instance, ui->error, &ui->trace))) {
        strcpy(error, ui->error);
        *ui = previous;
        memmove(&ui->installations[index + 1], &ui->installations[index], (ui->count - index - 1) * sizeof(ui->installations[0]));
        ui->installations[index] = removed;
        maage_launcher_ui_save(ui);
        strcpy(ui->error, error);
        maage_instance_close(instance);
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
    }
    maage_instance_close(instance);
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0);
}
maage_status maage_launcher_installation_select(maage_launcher_ui *ui, unsigned index) {
    maage_launcher_ui previous = *ui;
    if (index >= ui->count) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_NAVIGATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_NAVIGATE);
    if (ui->selected == ui->installations[index].id) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SELECT, 0);
    ui->selected = ui->installations[index].id;
    return maage_launcher_commit(ui, &previous);
}
maage_status maage_launcher_installation_default(maage_launcher_ui *ui, unsigned index) {
    maage_launcher_ui previous = *ui;
    if (index >= ui->count) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_NAVIGATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_NAVIGATE);
    if (ui->default_id == ui->installations[index].id) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SELECT, 0);
    ui->default_id = ui->installations[index].id;
    return maage_launcher_commit(ui, &previous);
}
maage_status maage_launcher_installation_directory(maage_launcher_ui *ui, unsigned index, char path[MAAGE_PATH_CAPACITY]) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    maage_instance *instance = NULL;
    path[0] = 0;
    if (index >= ui->count) {
        strcpy(ui->error, "Select a MaaGE installation first.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_QUERY, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_QUERY);
    }
    if (maage_status_failed(child_result = maage_instance_open(ui->root, ui->installations[index].uuid, 0, &instance, ui->error, &ui->trace))) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_QUERY, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_QUERY);
    strcpy(path, maage_instance_directory(instance));
    maage_instance_close(instance);
    ui->error[0] = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_QUERY, 0);
}
maage_status maage_launcher_account_select(maage_launcher_ui *ui, const char *uuid) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    if (maage_status_failed(child_result = maage_access_select(ui->access, uuid))) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SELECT, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SELECT);
    if (uuid) strcpy(ui->account, uuid);
    else
        ui->account[0] = 0;
    /* A preference write failure does not undo the live account selection. */
    return maage_launcher_ui_save(ui);
}
maage_status maage_launcher_account_restore(maage_launcher_ui *ui) {
    unsigned i;
    maage_status result;
    if (!ui) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SELECT, 0);
    result = maage_access_snapshot_get(ui->access, &ui->accounts);
    if (maage_status_failed(result)) return result;
    if (ui->account[0]) {
        result = maage_access_select(ui->access, ui->account);
        if (!maage_status_failed(result)) return result;
    }
    ui->account[0] = 0;
    for (i = 0; i < ui->accounts.count; ++i)
        if (ui->accounts.profiles[i].connected && ui->accounts.profiles[i].state == MAAGE_ACCESS_VERIFIED) {
            return maage_launcher_account_select(ui, ui->accounts.profiles[i].uuid);
        }
    return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SELECT, 0);
}
maage_status maage_launcher_ui_refresh(maage_launcher_ui *ui) {
    unsigned i;
    int completed, operation, exit_code, selected_build = -1;
    maage_status job_result, process_result, result;
    maage_process_state process_state;
    if (!ui) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_POLL, 0);
    result = maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_POLL, 0);
    if (ui->client_job) {
        job_result = maage_client_job_take(&ui->client_job, &ui->client_candidate, ui->clients, &ui->client_process, ui->error, &completed, &ui->trace);
        if (completed) {
            ui->result = result = job_result;
            if (maage_status_failed(job_result)) maage_log_trace_status(MAAGE_LOG_ERROR, "maage.launcher.clients", job_result, &ui->trace, "%s", ui->error);
            operation = ui->client_operation;
            ui->client_operation = 0;
            if (!maage_status_failed(job_result) && operation == 1) ui->client_confirmation = 1;
            else if (operation != 4) {
                ++ui->client_revision;
            }
            if (maage_status_failed(job_result) && operation == 4) ui->running_client[0] = ui->running_instance[0] = 0;
#if MAAGE_LAUNCHER_DEVELOPMENT
            if (!maage_status_failed(job_result) && operation == 2 && *ui->replacing_client)
                maage_launcher_client_finish_replace(ui);
            else if (maage_status_failed(job_result) || operation == 3) ui->replacing_client[0] = 0;
#endif
        }
    }
    if (ui->client_process) {
        process_result = maage_process_poll(ui->client_process, &exit_code, &process_state, ui->error, &ui->trace);
        if (maage_status_failed(process_result)) ui->result = result = process_result;
        if (process_state == MAAGE_PROCESS_EXITED || process_state == MAAGE_PROCESS_FAILED) {
            if (process_state == MAAGE_PROCESS_EXITED) {
                maage_log_info("maage.launcher.clients", "Client process exited (code=%d).", exit_code);
                if (exit_code) sprintf(ui->error, "Client exited with code %d. Check the latest Client log in the installation's logs folder.", exit_code);
            } else
                maage_log_trace_status(MAAGE_LOG_ERROR, "maage.launcher.clients", process_result, &ui->trace, "%s", ui->error);
            maage_process_close(ui->client_process);
            ui->client_process = NULL;
            ui->running_client[0] = ui->running_instance[0] = 0;
        }
    }
    ui->can_play = 0;
    strcpy(ui->client_status, "Select a Client build in Installations.");
    for (i = 0; i < ui->count; ++i)
        if (ui->installations[i].id == ui->selected) {
            const char *client = maage_launcher_installation_client(ui, i);
            sprintf(ui->client_status, "%.380s", client);
            selected_build = maage_clients_find(ui->clients, ui->installations[i].client);
            break;
        }
    if (ui->client_job) strcpy(ui->client_status, "Processing Client package locally…");
    if (!ui->clients_valid) strcpy(ui->client_status, "Client registry unavailable; restore config/client-builds.json.");
    if (ui->client_process) strcpy(ui->client_status, "Client process active. Live output is written to the instance's logs/client.log.");
    maage_access_snapshot_get(ui->access, &ui->accounts);
    maage_assets_snapshot(ui->assets, &ui->progress);
    for (i = 0; i < ui->accounts.count; ++i)
        if (!strcmp(ui->accounts.profiles[i].uuid, ui->account)) break;
    if (*ui->account && i == ui->accounts.count) {
        ui->account[0] = 0;
        maage_launcher_account_restore(ui);
    }
    strcpy(ui->account_status, "No connected Minecraft account. Open Manage Minecraft Accounts to add a Microsoft account.");
    for (i = 0; i < ui->accounts.count; ++i)
        if (!strcmp(ui->accounts.profiles[i].uuid, ui->account)) {
            maage_access_profile *profile = &ui->accounts.profiles[i];
            sprintf(ui->account_status, "%s · %s\n%s", profile->name, maage_access_state_name(profile->state), profile->detail);
            break;
        }
    if (!ui->account[0] || !ui->accounts.allowed) ui->readiness = "No verified Minecraft account";
    else if (ui->progress.stage == MAAGE_SETUP_COMPLETE) ui->readiness = "Resources ready";
    else if (ui->progress.stage == MAAGE_SETUP_FAILED) ui->readiness = "Repair required";
    else if (ui->progress.stage == MAAGE_SETUP_CANCELLED || ui->progress.stage == MAAGE_SETUP_NOT_STARTED) ui->readiness = "Resources missing";
    else
        ui->readiness = "Preparing resources";
    ui->can_play = ui->clients_valid && selected_build >= 0 && ui->clients->builds[selected_build].available && !ui->client_job && !ui->client_process && ui->account[0] && ui->accounts.allowed && ui->progress.stage == MAAGE_SETUP_COMPLETE;
    return result;
}
maage_status maage_launcher_ui_init(maage_launcher_ui *ui, maage_access *access, maage_assets *assets) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    char path[MAAGE_PATH_CAPACITY], *data = NULL;
    char selected_uuid[37] = "", default_uuid[37] = "";
    FILE *file;
    size_t size;
    long file_size;
    struct json_object *root = NULL, *list, *item;
    maage_instance *instance = NULL;
    unsigned i, j, id;
    const char *account, *uuid;
    int file_present = 0;
    memset(ui, 0, sizeof(*ui));
    ui->access = access;
    ui->assets = assets;
    if (maage_status_failed(child_result = maage_storage_data_path(ui->root, sizeof(ui->root))) || maage_status_failed(child_result = maage_storage_join(ui->resources, sizeof(ui->resources), ui->root, "minecraft/assets/reconst"))) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE);
    ui->clients = (maage_client_registry *) calloc(1, sizeof(*ui->clients));
    if (!ui->clients) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE);
    ui->clients_valid = !maage_status_failed(child_result = maage_clients_load(ui->clients, ui->root, ui->error, &ui->trace));
    ui->client_revision = 1;
    if (maage_status_failed(child_result = maage_launcher_path(ui, path, "launcher.json"))) goto failed;
    file_present = maage_storage_status(path) != 0;
    file = maage_storage_open(path, "rb");
    if (file) {
        if (!fseek(file, 0, SEEK_END) && (file_size = ftell(file)) >= 0 && (unsigned long) file_size <= MAAGE_LAUNCHER_PREFERENCES_BYTES && !fseek(file, 0, SEEK_SET)) {
            data = (char *) malloc((size_t) file_size + 1);
            if (data) {
                size = fread(data, 1, (size_t) file_size, file);
                if (size == (size_t) file_size && !ferror(file)) maage_access_json(data, size, &root);
            }
        }
        fclose(file);
        free(data);
        data = NULL;
    }
    if (file_present && !root) {
        strcpy(ui->error, "Launcher preferences are unreadable or invalid; existing instance references were preserved.");
        goto failed;
    }
    if (root) {
        if (maage_launcher_number(root, "schema") != 3) {
            strcpy(ui->error, "Unsupported Launcher preferences; recreate config/launcher.json.");
            goto failed;
        }
        uuid = maage_access_json_string(root, "selected");
        if (maage_storage_uuid_valid(uuid)) strcpy(selected_uuid, uuid);
        uuid = maage_access_json_string(root, "default");
        if (maage_storage_uuid_valid(uuid)) strcpy(default_uuid, uuid);
        ui->appearance = (int) maage_launcher_number(root, "appearance");
        if (ui->appearance > 2) ui->appearance = 0;
        account = maage_access_json_string(root, "activeAccount");
        if (strlen(account) == 32 && strspn(account, "0123456789abcdef") == 32) strcpy(ui->account, account);
        if (!json_object_object_get_ex(root, "installations", &list) || !json_object_is_type(list, json_type_array) || json_object_array_length(list) >= 2147483647UL) {
            strcpy(ui->error, "Invalid Launcher instance reference list.");
            goto failed;
        }
        if (maage_status_failed(child_result = maage_launcher_reserve(ui, (unsigned) json_object_array_length(list)))) {
            strcpy(ui->error, "Cannot allocate Launcher installation registry.");
            goto failed;
        }
        for (i = 0; i < json_object_array_length(list); ++i) {
            item = json_object_array_get_idx(list, i);
            uuid = maage_access_json_string(item, "uuid");
            for (j = 0; j < ui->count; ++j)
                if (!strcmp(ui->installations[j].uuid, uuid)) break;
            if (j < ui->count) {
                strcpy(ui->error, "Duplicate instance UUID reference.");
                goto failed;
            }
            if (maage_status_failed(child_result = maage_instance_open(ui->root, uuid, 0, &instance, ui->error, &ui->trace))) goto failed;
            id = i + 1;
            maage_launcher_installation_info(&ui->installations[ui->count], instance);
            maage_instance_close(instance);
            instance = NULL;
            if (!strcmp(uuid, selected_uuid)) ui->selected = id;
            if (!strcmp(uuid, default_uuid)) ui->default_id = id;
            ui->installations[ui->count++].id = id;
        }
        json_object_put(root);
        root = NULL;
    }
    if (!ui->count) {
        if (file_present) {
            strcpy(ui->error, "No instance references remain; recreate Launcher preferences.");
            goto failed;
        }
        if (maage_status_failed(child_result = maage_launcher_reserve(ui, 1))) {
            strcpy(ui->error, "Cannot allocate initial Launcher installation.");
            goto failed;
        }
        if (maage_status_failed(child_result = maage_instance_create(ui->root, "Default Installation", "", "", &instance, ui->error, &ui->trace))) goto failed;
        ui->count = 1;
        ui->installations[0].id = 1;
        maage_launcher_installation_info(&ui->installations[0], instance);
        maage_instance_close(instance);
        instance = NULL;
    }
    for (i = 0; i < ui->count; ++i)
        if (ui->installations[i].id == ui->default_id) break;
    if (i == ui->count) ui->default_id = ui->installations[0].id;
    for (i = 0; i < ui->count; ++i)
        if (ui->installations[i].id == ui->selected) break;
    if (i == ui->count) ui->selected = ui->default_id;
    ui->next_id = ui->count + 1;
    if (!file_present && maage_status_failed(child_result = maage_launcher_ui_save(ui))) goto failed;
    maage_launcher_account_restore(ui);
    maage_launcher_ui_refresh(ui);
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0);
failed:
    if (root) json_object_put(root);
    maage_instance_close(instance);
    maage_log_error("maage.launcher.ui", "%s", *ui->error ? ui->error : "Cannot load instance preferences.");
    maage_access_snapshot_release(&ui->accounts);
    maage_clients_dispose(ui->clients);
    free(ui->clients);
    ui->clients = NULL;
    free(ui->installations);
    ui->installations = NULL;
    ui->count = ui->capacity = 0;
    return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE);
}

maage_status maage_launcher_ui_destroy(maage_launcher_ui *ui) {
    maage_status result, child;
    if (!ui) return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_DESTROY, 0);
    result = maage_client_job_finish(&ui->client_job);
    /* A failed join retains its worker-owned context and borrowed service inputs. */
    if (maage_status_failed(result)) return result;
    child = maage_process_close(ui->client_process);
    if (maage_status_failed(child)) result = child;
    ui->client_process = NULL;
    maage_access_snapshot_release(&ui->accounts);
    maage_clients_dispose(ui->clients);
    free(ui->clients);
    ui->clients = NULL;
    free(ui->installations);
    ui->installations = NULL;
    ui->count = ui->capacity = 0;
    return MAAGE_STATUS_PROPAGATE(&ui->trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_DESTROY);
}
const char *maage_launcher_installation_client(maage_launcher_ui *ui, unsigned index) {
    int build;
    static char label[384];
    if (index >= ui->count || !*ui->installations[index].client) return ui->clients->count ? "Select a Client build in the installation editor" : "No Client installed. A managed Client installation is required";
    build = maage_clients_find(ui->clients, ui->installations[index].client);
    if (build < 0) return "Referenced Client missing; select another build";
    if (ui->clients->builds[build].available) {
        char path[MAAGE_PATH_CAPACITY];
        if (maage_status_failed(maage_clients_path(ui->clients, ui->installations[index].client, path, &ui->trace))) {
            ui->clients->builds[build].available = 0;
            strcpy(ui->clients->builds[build].status, "Missing; reimport required");
            ++ui->client_revision;
        }
    }
    maage_client_build_label(&ui->clients->builds[build], label);
    return label;
}
static maage_status maage_launcher_client_start(maage_launcher_ui *ui, int mode, const char *source, const char *instance_id) {
    if (ui->client_job || !ui->clients_valid) {
        strcpy(ui->error, "Client registry is busy or unavailable.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SUBMIT, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SUBMIT);
    }
    ui->error[0] = 0;
    maage_status_trace_init(&ui->trace);
    ui->result = maage_client_job_start(ui->root, mode, source, &ui->client_candidate, ui->access, instance_id, &ui->client_job, &ui->trace);
    if (maage_status_failed(ui->result)) {
        strcpy(ui->error, "Cannot start Client package worker.");
        return ui->result;
    }
    ui->client_operation = mode;
    strcpy(ui->error, mode == 1 ? "Inspecting Client package…" : mode == 2 ? "Copying Client package into MaaGE…"
                                                             : mode == 4   ? "Starting selected Client…"
                                                                           : "Removing managed Client package…");
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SUBMIT, 0);
}
static maage_status maage_launcher_installation_reload(maage_launcher_ui *ui, unsigned index) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    maage_instance *instance = NULL;
    if (maage_status_failed(child_result = maage_instance_open(ui->root, ui->installations[index].uuid, 0, &instance, ui->error, &ui->trace))) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_QUERY, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_QUERY);
    maage_launcher_installation_info(&ui->installations[index], instance);
    maage_instance_close(instance);
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_QUERY, 0);
}
#if MAAGE_LAUNCHER_DEVELOPMENT
static maage_status maage_launcher_installation_reassign(maage_launcher_ui *ui, unsigned index, const char *client) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    maage_instance *instance = NULL;
    int ok;
    if (maage_status_failed(child_result = maage_instance_open(ui->root, ui->installations[index].uuid, 0, &instance, ui->error, &ui->trace))) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
    ok = !maage_status_failed(child_result = maage_instance_update(instance, maage_instance_get_info(instance)->name, client, maage_instance_get_info(instance)->client_arguments, ui->error, &ui->trace));
    if (ok) maage_launcher_installation_info(&ui->installations[index], instance);
    maage_instance_close(instance);
    return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(((ok) ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_INVALID_STATE), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_MUTATE);
}
#endif
maage_status maage_launcher_client_remove(maage_launcher_ui *ui, unsigned index) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    unsigned i;
    if (index >= ui->clients->count) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    if (!strcmp(ui->running_client, ui->clients->builds[index].id)) {
        strcpy(ui->error, "Close the running Client before removing its files.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    }
    for (i = 0; i < ui->count; ++i) {
        if (maage_status_failed(child_result = maage_launcher_installation_reload(ui, i))) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
        if (!strcmp(ui->installations[i].client, ui->clients->builds[index].id)) {
            strcpy(ui->error, "This Client is used by an instance. Change its Client selection before removing the build.");
            return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
        }
    }
    return maage_launcher_client_start(ui, 3, ui->clients->builds[index].id, NULL);
}
#if MAAGE_LAUNCHER_DEVELOPMENT
maage_status maage_launcher_client_probe(maage_launcher_ui *ui, const char *path) {
    if (ui->client_job) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    ui->replacing_client[0] = 0;
    ui->client_confirmation = 0;
    return maage_launcher_client_start(ui, 1, path, NULL);
}
maage_status maage_launcher_client_replace(maage_launcher_ui *ui, unsigned index, const char *path) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    char id[65];
    if (index >= ui->clients->count || ui->client_job) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    if (strcmp(ui->clients->builds[index].origin, "local")) {
        strcpy(ui->error, "Only locally imported Clients can be replaced with a local package.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    }
    strcpy(id, ui->clients->builds[index].id);
    if (!strcmp(ui->running_client, id)) {
        strcpy(ui->error, "Close the running Client before replacing it.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    }
    if (maage_status_failed(child_result = maage_launcher_client_probe(ui, path))) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    strcpy(ui->replacing_client, id);
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0);
}
maage_status maage_launcher_client_cancel_import(maage_launcher_ui *ui) {
    if (!ui) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_CANCEL, 0);
    if (!ui->client_confirmation && !ui->replacing_client[0]) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CANCEL, 0);
    ui->client_confirmation = 0;
    ui->replacing_client[0] = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CANCEL, 0);
}
static maage_status maage_launcher_client_finish_replace(maage_launcher_ui *ui) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    char error[256];
    unsigned i, j;
    int *changed;
    int old = maage_clients_find(ui->clients, ui->replacing_client);
    int replacement = maage_clients_find(ui->clients, ui->client_candidate.build.id);

    if (old < 0 || replacement < 0 || !ui->clients->builds[replacement].available || !strcmp(ui->running_client, ui->replacing_client)) {
        strcpy(ui->error, "Replacement stopped: the old Client is running or a required build is unavailable.");
        ui->replacing_client[0] = 0;
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    }
    if (old == replacement) {
        strcpy(ui->error, "This exact Client build is already installed.");
        ui->replacing_client[0] = 0;
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    }
    changed = (int *) calloc(ui->count ? ui->count : 1, sizeof(*changed));
    if (!changed) {
        strcpy(ui->error, "Cannot allocate Client replacement history.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    }
    for (i = 0; i < ui->count; ++i) {
        if (maage_status_failed(child_result = maage_launcher_installation_reload(ui, i))) break;
        if (strcmp(ui->installations[i].client, ui->replacing_client)) continue;
        if (maage_status_failed(child_result = maage_launcher_installation_reassign(ui, i, ui->client_candidate.build.id))) break;
        changed[i] = 1;
    }
    if (i < ui->count) {
        strcpy(error, ui->error);
        for (j = 0; j < i; ++j)
            if (changed[j])
                maage_launcher_installation_reassign(ui, j, ui->replacing_client);
        sprintf(ui->error, "Both Client builds were kept; instance assignments could not all be saved. %.144s", error);
        ui->replacing_client[0] = 0;
        free(changed);
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    }
    free(changed);
    /* Every manifest is durable before removing the old package. Interrupted
     * reassignment leaves valid references because both builds still exist. */
    if (maage_status_failed(child_result = maage_launcher_client_remove(ui, (unsigned) old))) {
        ui->replacing_client[0] = 0;
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0);
}

maage_status maage_launcher_client_import(maage_launcher_ui *ui) {
    if (ui->client_job) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    ui->client_confirmation = 0;
    /* Also permits retrying a replacement after copying succeeded but saving
     * assignments failed, or choosing a build that is already installed. */
    if (*ui->replacing_client && maage_clients_find(ui->clients, ui->client_candidate.build.id) >= 0) return maage_launcher_client_finish_replace(ui);
    return maage_launcher_client_start(ui, 2, NULL, NULL);
}
#endif

maage_status maage_launcher_client_play(maage_launcher_ui *ui) {
    maage_status child_result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_QUERY, 0);
    unsigned i;
    maage_launcher_ui_refresh(ui);
    if (!ui->can_play || !maage_access_allowed(ui->access)) {
        strcpy(ui->error, "A verified account, prepared resources and an available selected Client are required. Close any running Client before starting another.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
    }
    for (i = 0; i < ui->count; ++i)
        if (ui->installations[i].id == ui->selected) {
            if (maage_status_failed(child_result = maage_launcher_client_start(ui, 4, ui->installations[i].client, ui->installations[i].uuid))) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
            strcpy(ui->running_client, ui->installations[i].client);
            strcpy(ui->running_instance, ui->installations[i].uuid);
            ui->can_play = 0;
            return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0);
        }
    return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
}
