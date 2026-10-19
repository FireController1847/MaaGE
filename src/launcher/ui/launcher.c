#include "launcher.h"
#include "maage/log.h"
#include "../access/verify.h"
#include <json-c/json.h>
#include <string.h>
#include <stdlib.h>

static unsigned maage_launcher_number(struct json_object *object, const char *key)
{
    struct json_object *value;
    int number;
    if (!json_object_object_get_ex(object, key, &value) || !json_object_is_type(value, json_type_int)) return 0;
    number = json_object_get_int(value);
    return number > 0 ? (unsigned)number : 0;
}
static int maage_launcher_name(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;
    int nonblank = 0;
    if (!name || !*name || strlen(name) >= 96) return 0;
    while (*p) { if (*p < 32 || *p == 127) return 0; if (*p != ' ') nonblank = 1; ++p; }
    return nonblank;
}
static int maage_launcher_path(maage_launcher_ui *ui, char *path, const char *file)
{
    char directory[MAAGE_PATH_CAPACITY];
    return maage_storage_join(directory, sizeof(directory), ui->root, "config") &&
        maage_storage_directories(directory) && maage_storage_join(path, MAAGE_PATH_CAPACITY, directory, file);
}
int maage_launcher_ui_save(maage_launcher_ui *ui)
{
    char path[MAAGE_PATH_CAPACITY], temporary[MAAGE_PATH_CAPACITY], lock_path[MAAGE_PATH_CAPACITY];
    struct json_object *root, *list, *item;
    const char *text;
    maage_storage_lock *lock;
    FILE *file;
    size_t length;
    unsigned i;
    int ok = 0;
    if (!maage_launcher_path(ui, path, "launcher.json") || !maage_launcher_path(ui, temporary, "launcher.json.tmp") ||
        !maage_launcher_path(ui, lock_path, "launcher.lock")) goto failed;
    lock = maage_storage_acquire(lock_path);
    if (!lock) goto failed;
    root = json_object_new_object(); list = json_object_new_array();
    json_object_object_add(root, "schema", json_object_new_int(2));
    json_object_object_add(root, "selected", json_object_new_int((int)ui->selected));
    json_object_object_add(root, "default", json_object_new_int((int)ui->default_id));
    json_object_object_add(root, "nextId", json_object_new_int((int)ui->next_id));
    json_object_object_add(root, "appearance", json_object_new_int(ui->appearance));
    json_object_object_add(root, "activeAccount", json_object_new_string(ui->account));
    for (i = 0; i < ui->count; ++i) {
        item = json_object_new_object();
        json_object_object_add(item, "id", json_object_new_int((int)ui->installations[i].id));
        json_object_object_add(item, "name", json_object_new_string(ui->installations[i].name));
        json_object_object_add(item, "client", json_object_new_string(ui->installations[i].client));
        json_object_array_add(list, item);
    }
    json_object_object_add(root, "installations", list);
    text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY); length = strlen(text);
    file = maage_storage_open(temporary, "wb");
    if (file) {
        ok = fwrite(text, 1, length, file) == length;
        if (fclose(file)) ok = 0;
        if (ok) ok = maage_storage_replace(temporary, path);
    }
    if (!ok) maage_storage_remove(temporary);
    json_object_put(root); maage_storage_release(lock);
    if (ok) { ui->error[0] = 0; return 1; }
failed:
    strcpy(ui->error, "Cannot save Launcher preferences. Check data-folder permissions or close another Launcher.");
    maage_log_error("maage.launcher.ui", "%s", ui->error); return 0;
}
static int maage_launcher_commit(maage_launcher_ui *ui, const maage_launcher_ui *previous)
{
    char error[256];
    if (maage_launcher_ui_save(ui)) return 1;
    strcpy(error, ui->error); *ui = *previous; strcpy(ui->error, error); return 0;
}
int maage_launcher_installation_write(maage_launcher_ui *ui, int index, const char *name, const char *client)
{
    maage_launcher_ui previous = *ui;
    if (!maage_launcher_name(name)) { strcpy(ui->error, "Enter a name of 1–95 UTF-8 bytes without control characters."); return 0; }
    if (ui->client_job) { strcpy(ui->error, "Wait for the Client operation to finish."); return 0; }
    if (*client && maage_clients_find(ui->clients, client) < 0 &&
        (index < 0 || (unsigned)index >= ui->count || strcmp(ui->installations[index].client, client))) { strcpy(ui->error, "Choose a registered Client build."); return 0; }
    if (index < 0) {
        if (ui->count == MAAGE_INSTALLATIONS || ui->next_id >= 2147483647U) { strcpy(ui->error, "Installation limit reached."); return 0; }
        index = (int)ui->count++; ui->installations[index].id = ui->next_id++;
    }
    if ((unsigned)index >= ui->count) return 0;
    strcpy(ui->installations[index].name, name);
    strcpy(ui->installations[index].client, client);
    return maage_launcher_commit(ui, &previous);
}
int maage_launcher_installation_remove(maage_launcher_ui *ui, unsigned index)
{
    maage_launcher_ui previous = *ui;
    unsigned id;
    if (index >= ui->count) return 0;
    if (ui->count == 1) { strcpy(ui->error, "Keep at least one MaaGE configuration."); return 0; }
    id = ui->installations[index].id;
    --ui->count;
    memmove(&ui->installations[index], &ui->installations[index + 1], (ui->count - index) * sizeof(ui->installations[0]));
    if (ui->default_id == id) ui->default_id = ui->installations[0].id;
    if (ui->selected == id) ui->selected = ui->default_id;
    return maage_launcher_commit(ui, &previous);
}
int maage_launcher_installation_select(maage_launcher_ui *ui, unsigned index)
{
    maage_launcher_ui previous = *ui;
    if (index >= ui->count) return 0;
    ui->selected = ui->installations[index].id;
    return maage_launcher_commit(ui, &previous);
}
int maage_launcher_installation_default(maage_launcher_ui *ui, unsigned index)
{
    maage_launcher_ui previous = *ui;
    if (index >= ui->count) return 0;
    ui->default_id = ui->installations[index].id;
    return maage_launcher_commit(ui, &previous);
}
int maage_launcher_account_select(maage_launcher_ui *ui, const char *uuid)
{
    if (!maage_access_select(ui->access, uuid)) return 0;
    strcpy(ui->account, uuid);
    /* A preference write failure does not undo the live account selection. */
    return maage_launcher_ui_save(ui);
}
void maage_launcher_account_restore(maage_launcher_ui *ui)
{
    unsigned i;
    maage_access_snapshot_get(ui->access, &ui->accounts);
    if (ui->account[0] && maage_access_select(ui->access, ui->account)) return;
    ui->account[0] = 0;
    for (i = 0; i < ui->accounts.count; ++i) if (ui->accounts.profiles[i].connected && ui->accounts.profiles[i].state == MAAGE_ACCESS_VERIFIED) {
        maage_launcher_account_select(ui, ui->accounts.profiles[i].uuid); break;
    }
}
void maage_launcher_ui_refresh(maage_launcher_ui *ui)
{
    unsigned i;
    int job_result;
    if (ui->client_job) {
        job_result = maage_client_job_take(&ui->client_job, &ui->client_candidate, ui->clients, ui->error);
        if (job_result) {
            if (job_result > 0 && ui->client_operation == 1) ui->client_confirmation = 1;
            else { ++ui->client_revision; }
            ui->client_operation = 0;
        }
    }
    strcpy(ui->client_status, "Select a Client build in Installations.");
    for (i = 0; i < ui->count; ++i) if (ui->installations[i].id == ui->selected) {
        const char *client = maage_launcher_installation_client(ui, i);
        sprintf(ui->client_status, "%.380s\nClient launching is not implemented in this build.", client);
        break;
    }
    if (ui->client_job) strcpy(ui->client_status, "Processing Client package locally…");
    if (!ui->clients_valid) strcpy(ui->client_status, "Client registry unavailable; restore config/client-builds.json.");
    maage_access_snapshot_get(ui->access, &ui->accounts);
    maage_assets_snapshot(ui->assets, &ui->progress);
    for (i = 0; i < ui->accounts.count; ++i) if (!strcmp(ui->accounts.profiles[i].uuid, ui->account)) break;
    if (*ui->account && i == ui->accounts.count) {
        ui->account[0] = 0; maage_launcher_account_restore(ui);
    }
    strcpy(ui->account_status, "No connected Minecraft account. Open Manage Minecraft Accounts to add a Microsoft account.");
    for (i = 0; i < ui->accounts.count; ++i) if (!strcmp(ui->accounts.profiles[i].uuid, ui->account)) {
        maage_access_profile *profile = &ui->accounts.profiles[i];
        sprintf(ui->account_status, "%s · %s\n%s", profile->name, maage_access_state_name(profile->state), profile->detail);
        break;
    }
    if (!ui->account[0] || !ui->accounts.allowed) ui->readiness = "No verified Minecraft account";
    else if (ui->progress.stage == MAAGE_SETUP_COMPLETE) ui->readiness = "Resources ready";
    else if (ui->progress.stage == MAAGE_SETUP_FAILED) ui->readiness = "Repair required";
    else if (ui->progress.stage == MAAGE_SETUP_CANCELLED || ui->progress.stage == MAAGE_SETUP_NOT_STARTED) ui->readiness = "Resources missing";
    else ui->readiness = "Preparing resources";
}
int maage_launcher_ui_init(maage_launcher_ui *ui, maage_access *access, maage_assets *assets)
{
    char path[MAAGE_PATH_CAPACITY], data[16385];
    FILE *file;
    size_t size;
    struct json_object *root = NULL, *list, *item;
    unsigned i, j, id, max_id = 0;
    const char *name, *account, *client;
    int legacy = 1;
    memset(ui, 0, sizeof(*ui)); ui->access = access; ui->assets = assets;
    if (!maage_storage_data_path(ui->root, sizeof(ui->root)) ||
        !maage_storage_join(ui->resources, sizeof(ui->resources), ui->root, "minecraft/assets/reconst")) return 0;
    ui->clients = (maage_client_registry *)calloc(1, sizeof(*ui->clients));
    if (!ui->clients) return 0;
    ui->clients_valid = maage_clients_load(ui->clients, ui->root, ui->error);
    ui->client_revision = 1;
    if (maage_launcher_path(ui, path, "launcher.json") && (file = maage_storage_open(path, "rb")) != NULL) {
        size = fread(data, 1, sizeof(data), file); fclose(file);
        if (size < sizeof(data)) { data[size] = 0; root = maage_access_json(data, size); }
    }
    if (root && (maage_launcher_number(root, "schema") == 1 || maage_launcher_number(root, "schema") == 2)) {
        legacy = maage_launcher_number(root, "schema") == 1;
        ui->selected = maage_launcher_number(root, "selected"); ui->default_id = maage_launcher_number(root, "default");
        ui->next_id = maage_launcher_number(root, "nextId"); ui->appearance = (int)maage_launcher_number(root, "appearance");
        if (ui->appearance > 2) ui->appearance = 0;
        account = maage_access_json_string(root, "activeAccount");
        if (strlen(account) == 32 && strspn(account, "0123456789abcdef") == 32) strcpy(ui->account, account);
        if (json_object_object_get_ex(root, "installations", &list) && json_object_is_type(list, json_type_array)) {
            for (i = 0; i < json_object_array_length(list) && ui->count < MAAGE_INSTALLATIONS; ++i) {
                item = json_object_array_get_idx(list, i); id = maage_launcher_number(item, "id"); name = maage_access_json_string(item, "name");
                if (!id || id >= 2147483647U || !maage_launcher_name(name)) continue;
                for (j = 0; j < ui->count; ++j) if (ui->installations[j].id == id) break;
                if (j < ui->count) continue;
                client = maage_access_json_string(item, "client");
                if (strlen(client) == 64 && strspn(client, "0123456789abcdef") == 64) strcpy(ui->installations[ui->count].client, client);
                ui->installations[ui->count].id = id; strcpy(ui->installations[ui->count++].name, name);
                if (id > max_id) max_id = id;
            }
        }
    }
    if (root) json_object_put(root);
    if (!ui->count) {
        ui->count = 1; ui->installations[0].id = 1; strcpy(ui->installations[0].name, "Default Installation"); max_id = 1;
    }
    for (i = 0; i < ui->count; ++i) if (ui->installations[i].id == ui->default_id) break;
    if (i == ui->count) ui->default_id = ui->installations[0].id;
    for (i = 0; i < ui->count; ++i) if (ui->installations[i].id == ui->selected) break;
    if (i == ui->count) ui->selected = ui->default_id;
    if (ui->next_id <= max_id) ui->next_id = max_id + 1;
    if (legacy && ui->clients_valid) {
        unsigned available = 0, chosen = 0;
        for (i = 0; i < ui->clients->count; ++i) if (ui->clients->builds[i].available) { ++available; chosen = i; }
        if (available == 1) for (i = 0; i < ui->count; ++i) if (!*ui->installations[i].client)
            strcpy(ui->installations[i].client, ui->clients->builds[chosen].id);
        maage_launcher_ui_save(ui);
    }
    maage_launcher_account_restore(ui); maage_launcher_ui_refresh(ui); return 1;
}

void maage_launcher_ui_destroy(maage_launcher_ui *ui)
{ maage_client_job_finish(&ui->client_job); free(ui->clients); ui->clients = NULL; }
const char *maage_launcher_installation_client(maage_launcher_ui *ui, unsigned index)
{
    int build;
    static char label[384];
    if (index >= ui->count || !*ui->installations[index].client)
        return ui->clients->count ? "Select a Client build in the installation editor" : "No Client installed. A managed Client installation is required";
    build = maage_clients_find(ui->clients, ui->installations[index].client);
    if (build < 0) return "Referenced Client missing; select another build";
    if (ui->clients->builds[build].available) {
        char path[MAAGE_PATH_CAPACITY];
        if (!maage_clients_path(ui->clients, ui->installations[index].client, path)) {
            ui->clients->builds[build].available = 0;
            strcpy(ui->clients->builds[build].status, "Missing; reimport required"); ++ui->client_revision;
        }
    }
    maage_client_build_label(&ui->clients->builds[build], label); return label;
}
static int maage_launcher_client_start(maage_launcher_ui *ui, int mode, const char *source)
{
    if (ui->client_job || !ui->clients_valid) { strcpy(ui->error, "Client registry is busy or unavailable."); return 0; }
    ui->error[0] = 0;
    ui->client_job = maage_client_job_start(ui->root, mode, source, &ui->client_candidate);
    if (!ui->client_job) { strcpy(ui->error, "Cannot start Client package worker."); return 0; }
    ui->client_operation = mode;
    strcpy(ui->error, mode == 1 ? "Inspecting Client package…" : mode == 2 ? "Copying Client package into MaaGE…" : "Removing managed Client package…");
    return 1;
}
int maage_launcher_client_remove(maage_launcher_ui *ui, unsigned index)
{
    unsigned i;
    if (index >= ui->clients->count) return 0;
    for (i = 0; i < ui->count; ++i) if (!strcmp(ui->installations[i].client, ui->clients->builds[index].id)) {
        strcpy(ui->error, "This Client is used by an installation. Change its Client selection before removing the build."); return 0;
    }
    return maage_launcher_client_start(ui, 3, ui->clients->builds[index].id);
}
#if MAAGE_LAUNCHER_DEVELOPMENT
int maage_launcher_client_probe(maage_launcher_ui *ui, const char *path)
{ ui->client_confirmation = 0; return maage_launcher_client_start(ui, 1, path); }
int maage_launcher_client_import(maage_launcher_ui *ui)
{ ui->client_confirmation = 0; return maage_launcher_client_start(ui, 2, NULL); }
#endif
