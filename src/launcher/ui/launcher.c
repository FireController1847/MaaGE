#include "launcher.h"
#include "../clients/launch.h"
#include "maage/log.h"
#include "maage/time.h"
#include "maage/thread.h"
#include "maage/download.h"
#include "../access/verify.h"
#include <json-c/json.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#define MAAGE_LAUNCHER_PREFERENCES_BYTES (8UL * 1024UL * 1024UL)
struct maage_launcher_startup {
    maage_launcher_ui loaded;
    maage_thread *thread;
    maage_mutex *mutex;
    int done, services_ready, cancelled;
    maage_status result;
};
static maage_status maage_launcher_catalog_load(maage_launcher_ui *ui);
static maage_status maage_launcher_client_start(maage_launcher_ui *ui, int mode, const char *source, const char *instance_id);
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
    if (!ui->catalog_ready) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_SAVE, 0);
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
    if (!ui->catalog_ready || ui->client_job) {
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
    if (!ui->catalog_ready || ui->client_job) {
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
    ui->account_explicit = ui->account_bound = 1;
    /* A preference write failure does not undo the live account selection. */
    return ui->catalog_ready ? maage_launcher_ui_save(ui) : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SELECT, 0);
}
maage_status maage_launcher_account_restore(maage_launcher_ui *ui) {
    unsigned i;
    maage_status result;
    if (!ui) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SELECT, 0);
    result = maage_access_snapshot_get(ui->access, &ui->accounts);
    if (maage_status_failed(result)) return result;
    if (!ui->catalog_ready || !ui->accounts.metadata_ready) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SELECT, 0);
    if (ui->account[0]) {
        result = maage_access_select(ui->access, ui->account);
        if (!maage_status_failed(result)) {
            ui->account_bound = 1;
            return result;
        }
    }
    ui->account[0] = 0;
    for (i = 0; i < ui->accounts.count; ++i)
        if (ui->accounts.profiles[i].connected && ui->accounts.profiles[i].state == MAAGE_ACCESS_VERIFIED) {
            return maage_launcher_account_select(ui, ui->accounts.profiles[i].uuid);
        }
    for (i = 0; i < ui->accounts.count; ++i)
        if (ui->accounts.profiles[i].connected) return maage_launcher_account_select(ui, ui->accounts.profiles[i].uuid);
    return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SELECT, 0);
}
static void maage_launcher_startup_worker(void *context) {
    maage_launcher_startup *startup = (maage_launcher_startup *) context;
    maage_status result, auth_result;
    int cancelled, services_ready;
    double started = maage_time_monotonic_seconds();
    result = maage_download_initialize();
    services_ready = !maage_status_failed(result);
    maage_mutex_lock(startup->mutex);
    cancelled = startup->cancelled;
    maage_mutex_unlock(startup->mutex);
    MAAGE_LOG_DEBUG_EVENT("maage.launcher.startup", maage_log_debug("maage.launcher.startup", "Network service initialization: %.3f s.", maage_time_monotonic_seconds() - started));
    if (maage_status_failed(result)) maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.startup", result, "Network initialization failed; local account restoration will still run.");
    if (!cancelled) {
        auth_result = maage_access_restore(startup->loaded.access);
        if (maage_status_failed(auth_result)) maage_log_status(MAAGE_LOG_WARNING, "maage.launcher.access", auth_result, "Could not start account restoration; Accounts remains available for retry.");
    }
    maage_mutex_lock(startup->mutex);
    startup->services_ready = services_ready;
    cancelled = startup->cancelled;
    maage_mutex_unlock(startup->mutex);
    started = maage_time_monotonic_seconds();
    if (!cancelled) result = maage_launcher_catalog_load(&startup->loaded);
    else result = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_LOAD, 0);
    MAAGE_LOG_DEBUG_EVENT("maage.launcher.startup", maage_log_debug("maage.launcher.startup", "Installation/catalog loading: %.3f s; installations=%u; clients=%u; result=0x%08lx.", maage_time_monotonic_seconds() - started, startup->loaded.count, startup->loaded.clients ? startup->loaded.clients->count : 0, (unsigned long) result));
    maage_mutex_lock(startup->mutex);
    startup->result = result;
    startup->done = 1;
    maage_mutex_unlock(startup->mutex);
}
maage_status maage_launcher_ui_presented(maage_launcher_ui *ui) {
    maage_launcher_startup *startup;
    maage_status result;
    if (!ui || ui->presented) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0);
    ui->presented = 1;
    MAAGE_LOG_DEBUG_EVENT("maage.launcher.startup", maage_log_debug("maage.launcher.startup", "Main window first presentation: %.3f s since Launcher entry.", maage_time_monotonic_seconds() - ui->started_at));
    startup = (maage_launcher_startup *) calloc(1, sizeof(*startup));
    result = startup ? maage_mutex_create(&startup->mutex) : maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_CREATE, 0);
    if (!maage_status_failed(result)) {
        startup->loaded.access = ui->access;
        startup->loaded.assets = ui->assets;
        result = maage_thread_start(maage_launcher_startup_worker, startup, &startup->thread);
    }
    if (maage_status_failed(result)) {
        if (startup) maage_mutex_destroy(startup->mutex);
        free(startup);
        ui->catalog_finished = 1;
        strcpy(ui->error, "Cannot start Launcher services. Close other applications and reopen MaaGE.");
        return MAAGE_STATUS_PROPAGATE(&ui->trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE);
    }
    ui->startup = startup;
    return result;
}
static maage_status maage_launcher_startup_poll(maage_launcher_ui *ui) {
    maage_launcher_startup *startup = ui->startup;
    maage_launcher_ui *loaded;
    maage_status result;
    int done;
    if (!startup) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_POLL, 0);
    maage_mutex_lock(startup->mutex);
    done = startup->done;
    ui->services_ready = startup->services_ready;
    maage_mutex_unlock(startup->mutex);
    if (!done) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_POLL, 0);
    result = maage_thread_join(startup->thread);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(&ui->trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_WAIT);
    loaded = &startup->loaded;
    ui->catalog_finished = 1;
    ui->catalog_ready = !maage_status_failed(startup->result);
    result = startup->result;
    ui->trace = loaded->trace;
    if (ui->catalog_ready) {
        maage_clients_dispose(ui->clients);
        free(ui->clients);
        ui->clients = loaded->clients;
        loaded->clients = NULL;
        ui->clients_valid = loaded->clients_valid;
        ui->installations = loaded->installations;
        loaded->installations = NULL;
        ui->count = loaded->count;
        ui->capacity = loaded->capacity;
        ui->selected = loaded->selected;
        ui->default_id = loaded->default_id;
        ui->next_id = loaded->next_id;
        ui->appearance = loaded->appearance;
        if (!ui->account_explicit) strcpy(ui->account, loaded->account);
        ui->account_bound = 0;
        ++ui->client_revision;
    }
    if (*loaded->error) strcpy(ui->error, loaded->error);
    maage_mutex_destroy(startup->mutex);
    free(startup);
    ui->startup = NULL;
    if (ui->catalog_ready && ui->account_explicit) {
        maage_status saved = maage_launcher_ui_save(ui);
        if (maage_status_failed(saved)) result = saved;
    }
    return result;
}
maage_status maage_launcher_resources(maage_launcher_ui *ui, maage_assets_mode mode) {
    if (!ui || mode < MAAGE_ASSETS_PREPARE || mode > MAAGE_ASSETS_REPAIR) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT, 0);
    if (ui->progress.busy || ui->client_process || ui->client_operation == 4 || ui->launch_pending) {
        strcpy(ui->error, "Finish the current resource operation and close the Client before resource maintenance.");
        return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_SUBMIT, 0);
    }
    ui->resource_mode = mode;
    ui->resources_pending = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
maage_status maage_launcher_resources_cancel(maage_launcher_ui *ui) {
    if (!ui) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_CANCEL, 0);
    ui->resources_pending = 0;
    ui->launch_pending = 0;
    return maage_assets_cancel(ui->assets);
}
maage_launcher_page maage_launcher_attention_page(const maage_launcher_ui *ui) {
    if (!ui->catalog_ready || !ui->clients_valid) return MAAGE_PAGE_INSTALLATIONS;
    if (!*ui->account || !ui->accounts.allowed) return MAAGE_PAGE_ACCOUNTS;
    if (!ui->can_play && ui->progress.stage == MAAGE_SETUP_COMPLETE && !ui->progress.busy && !ui->resources_pending && !ui->client_job && !ui->client_process) return MAAGE_PAGE_INSTALLATIONS;
    return MAAGE_PAGE_SETTINGS;
}
maage_status maage_launcher_ui_refresh(maage_launcher_ui *ui) {
    unsigned i;
    int completed, operation, exit_code, selected_build = -1;
    maage_status job_result, process_result, result;
    maage_process_state process_state;
    if (!ui) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_POLL, 0);
    result = maage_launcher_startup_poll(ui);
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
    strcpy(ui->client_status, "Choose a Client build in Installations.");
    strcpy(ui->active_summary, "Loading installations");
    for (i = 0; i < ui->count; ++i) {
        if (ui->installations[i].id != ui->selected) continue;
        sprintf(ui->client_status, "%.380s", maage_launcher_installation_client(ui, i));
        sprintf(ui->active_summary, "%.95s", ui->installations[i].name);
        selected_build = maage_clients_find(ui->clients, ui->installations[i].client);
        break;
    }
    if (ui->catalog_finished && !ui->catalog_ready) strcpy(ui->active_summary, "Installations unavailable");
    if (ui->client_job) strcpy(ui->client_status, ui->client_operation == 4 ? "Starting the selected Client…" : "Processing Client package…");
    else if (ui->catalog_ready && !ui->clients_valid) strcpy(ui->client_status, "Client registry unavailable. Restore config/client-builds.json.");
    if (ui->client_process) strcpy(ui->client_status, "Client running. You can close the Launcher.");
    job_result = maage_access_snapshot_get(ui->access, &ui->accounts);
    if (maage_status_failed(job_result)) {
        ui->accounts.allowed = 0;
        return MAAGE_STATUS_PROPAGATE(&ui->trace, job_result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_SNAPSHOT);
    }
    for (i = 0; i < ui->accounts.count; ++i)
        if (!strcmp(ui->accounts.profiles[i].uuid, ui->account)) break;
    if (!ui->account_bound || !*ui->account || (ui->accounts.metadata_ready && i == ui->accounts.count)) {
        job_result = maage_launcher_account_restore(ui);
        if (maage_status_failed(job_result)) result = job_result;
        maage_access_snapshot_get(ui->access, &ui->accounts);
    }
    strcpy(ui->account_status, ui->accounts.busy && !ui->accounts.restored ? "Restoring saved accounts…" : "Sign in with Microsoft in Accounts to verify Minecraft Java Edition access.");
    for (i = 0; i < ui->accounts.count; ++i) {
        maage_access_profile *profile = &ui->accounts.profiles[i];
        if (strcmp(profile->uuid, ui->account)) continue;
        sprintf(ui->account_status, "%s · %s\n%s", profile->name, maage_access_state_name(profile->state), profile->detail);
        strcat(ui->active_summary, " · ");
        strcat(ui->active_summary, profile->name);
        break;
    }
    if (i == ui->accounts.count) strcat(ui->active_summary, " · No account selected");
    sprintf(ui->authentication_status, "%.480s", ui->accounts.status);
    if (*ui->accounts.device_code) sprintf(ui->authentication_status, "%.480s\n%.210s  ·  %.32s", ui->accounts.status, ui->accounts.device_url, ui->accounts.device_code);
    maage_assets_snapshot(ui->assets, &ui->progress);
    if (ui->progress.stage == MAAGE_SETUP_FAILED && maage_status_code(ui->progress.result) == MAAGE_STATUS_ACCESS_DENIED && maage_status_context(ui->progress.result) == MAAGE_CONTEXT_AUTH && ui->accounts.allowed) {
        ui->resources_pending = 1;
    }
    if (ui->services_ready && ui->catalog_ready && ui->resources_pending && ui->accounts.allowed && !ui->progress.busy && !ui->client_process && ui->client_operation != 4) {
        ui->resources_pending = 0;
        job_result = maage_assets_start(ui->assets, ui->resource_mode);
        if (maage_status_failed(job_result)) result = job_result;
        maage_assets_snapshot(ui->assets, &ui->progress);
    }
    if (ui->launch_pending) {
        for (i = 0; i < ui->count; ++i)
            if (ui->installations[i].id == ui->selected) break;
        if (!ui->accounts.allowed || strcmp(ui->pending_account, ui->account) || i == ui->count || strcmp(ui->pending_instance, ui->installations[i].uuid) || strcmp(ui->pending_client, ui->installations[i].client)) {
            ui->launch_pending = 0;
            strcpy(ui->error, "Play cancelled because the selected account, installation or authorization changed. Review the selection and retry Play.");
        } else if (!ui->resources_pending && !ui->progress.busy) {
            ui->launch_pending = 0;
            if (ui->progress.stage == MAAGE_SETUP_COMPLETE) {
                job_result = maage_launcher_client_start(ui, 4, ui->pending_client, ui->pending_instance);
                if (maage_status_failed(job_result)) result = job_result;
                else {
                    strcpy(ui->running_client, ui->pending_client);
                    strcpy(ui->running_instance, ui->pending_instance);
                }
            }
        }
    }
    if (ui->resources_pending) strcpy(ui->resource_status, ui->accounts.allowed ? "Resource preparation queued…" : "Waiting for a verified Minecraft Java account. Manage accounts while resources wait.");
    else if (ui->progress.cancellation_requested && ui->progress.busy) strcpy(ui->resource_status, "Stopping resources safely. Verified files will be kept.");
    else if (ui->progress.stage == MAAGE_SETUP_FAILED) sprintf(ui->resource_status, "%.510s", ui->progress.error);
    else if (ui->progress.stage == MAAGE_SETUP_CANCELLED) strcpy(ui->resource_status, "Resource preparation cancelled. Retry to continue.");
    else if (ui->progress.stage == MAAGE_SETUP_COMPLETE) strcpy(ui->resource_status, ui->progress.reused_receipt ? "Resources ready · verified installation unchanged" : "Resources ready · verification complete");
    else if (ui->progress.percent >= 0) sprintf(ui->resource_status, "%s · %d%%\n%lu / %lu objects · %lu / %lu indexed files · %.1f MiB downloaded", ui->progress.activity, ui->progress.percent, ui->progress.verified_objects, ui->progress.total_objects, ui->progress.reconstructed_files, ui->progress.total_logical_assets, (ui->progress.downloaded_bytes + ui->progress.client_downloaded_bytes) / 1048576.0);
    else sprintf(ui->resource_status, "%.255s", *ui->progress.activity ? ui->progress.activity : "Resources have not been checked.");
    if (!ui->catalog_finished) ui->readiness = "Loading installations";
    else if (!ui->catalog_ready || !ui->clients_valid) ui->readiness = "Installations unavailable";
    else if (!ui->services_ready) ui->readiness = "Services unavailable";
    else if (!*ui->account || !ui->accounts.allowed) ui->readiness = ui->accounts.busy ? "Verifying account" : "Account needs attention";
    else if (ui->progress.busy || ui->resources_pending) ui->readiness = "Preparing resources";
    else if (ui->progress.stage != MAAGE_SETUP_COMPLETE) ui->readiness = "Resources need attention";
    else if (selected_build < 0 || !ui->clients->builds[selected_build].available) ui->readiness = "Choose a Client build";
    else if (ui->client_process) ui->readiness = "Client running";
    else if (ui->client_job || ui->launch_pending) ui->readiness = "Client operation in progress";
    else {
        ui->readiness = "Ready to play";
        ui->can_play = 1;
    }
    if (ui->can_play && !ui->play_timed) {
        ui->play_timed = 1;
        MAAGE_LOG_DEBUG_EVENT("maage.launcher.startup", maage_log_debug("maage.launcher.startup", "Play first available: %.3f s since Launcher entry.", maage_time_monotonic_seconds() - ui->started_at));
    }
    if (ui->can_play) strcpy(ui->launch_status, "Ready. Start your selected installation.");
    else if (!ui->catalog_finished) strcpy(ui->launch_status, "Loading installations in the background. You can explore the Launcher.");
    else if (!ui->catalog_ready) strcpy(ui->launch_status, "Installations could not be loaded. See the message below and the Launcher log.");
    else if (!ui->services_ready) strcpy(ui->launch_status, "Launcher services could not start. Reopen MaaGE to retry.");
    else if (!*ui->account || !ui->accounts.allowed) strcpy(ui->launch_status, ui->accounts.busy ? "Checking Minecraft Java access. Follow sign-in progress in Accounts." : "Open Accounts to sign in or refresh Minecraft Java access.");
    else if (ui->progress.busy || ui->resources_pending) strcpy(ui->launch_status, "Preparing Minecraft resources. Progress and controls are in Settings.");
    else if (ui->progress.stage != MAAGE_SETUP_COMPLETE) strcpy(ui->launch_status, "Open Settings to retry, verify or repair Minecraft resources.");
    else if (selected_build < 0 || !ui->clients->builds[selected_build].available) strcpy(ui->launch_status, "Open Installations and assign an available Client build.");
    else if (ui->client_process) strcpy(ui->launch_status, "Your Client runs independently. It is safe to close this Launcher.");
    else strcpy(ui->launch_status, "Wait for the Client operation to finish.");
    return result;
}
maage_status maage_launcher_ui_init(maage_launcher_ui *ui, maage_access *access, maage_assets *assets) {
    maage_status result;
    if (!ui || !access || !assets) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0);
    memset(ui, 0, sizeof(*ui));
    ui->started_at = maage_time_monotonic_seconds();
    ui->access = access;
    ui->assets = assets;
    ui->resources_pending = 1;
    ui->progress.percent = -1;
    ui->readiness = "Starting Launcher";
    result = maage_storage_data_path(ui->root, sizeof(ui->root));
    if (!maage_status_failed(result)) result = maage_storage_join(ui->resources, sizeof(ui->resources), ui->root, "minecraft/assets/reconst");
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(&ui->trace, result, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE);
    ui->clients = (maage_client_registry *) calloc(1, sizeof(*ui->clients));
    if (!ui->clients) return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_INITIALIZE);
    return maage_launcher_ui_refresh(ui);
}

static maage_status maage_launcher_catalog_load(maage_launcher_ui *ui) {
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
    ui->catalog_ready = 1;
    if (!file_present && maage_status_failed(child_result = maage_launcher_ui_save(ui))) goto failed;
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
    if (ui->startup) {
        maage_mutex_lock(ui->startup->mutex);
        ui->startup->cancelled = 1;
        maage_mutex_unlock(ui->startup->mutex);
        result = maage_thread_join(ui->startup->thread);
        if (maage_status_failed(result)) return result;
        ui->services_ready = ui->startup->services_ready;
        maage_clients_dispose(ui->startup->loaded.clients);
        free(ui->startup->loaded.clients);
        free(ui->startup->loaded.installations);
        maage_mutex_destroy(ui->startup->mutex);
        free(ui->startup);
        ui->startup = NULL;
    }
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
    maage_client_build_label(&ui->clients->builds[build], label);
    return label;
}
static maage_status maage_launcher_client_start(maage_launcher_ui *ui, int mode, const char *source, const char *instance_id) {
    if (!ui->catalog_ready || ui->client_job || ui->launch_pending || !ui->clients_valid) {
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
    strcpy(ui->client_status, mode == 1 ? "Inspecting Client package…" : mode == 2 ? "Copying Client package into MaaGE…"
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
            /* Recheck disk readiness asynchronously for every launch. Selection
             * is captured before that work and must still match at submission. */
            child_result = maage_launcher_resources(ui, MAAGE_ASSETS_PREPARE);
            if (maage_status_failed(child_result)) return child_result;
            strcpy(ui->pending_client, ui->installations[i].client);
            strcpy(ui->pending_instance, ui->installations[i].uuid);
            strcpy(ui->pending_account, ui->account);
            ui->launch_pending = 1;
            ui->can_play = 0;
            return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0);
        }
    return MAAGE_STATUS_PROPAGATE(&ui->trace, maage_status_failed(child_result) ? child_result : maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD, 0), MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_RELOAD);
}
