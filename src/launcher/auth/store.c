#include "internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void maage_auth_store_key(const maage_auth_record *record, char key[128])
{
    sprintf(key, "MaaGE/Microsoft/%s/%s", record->client_id, record->id);
}
static int maage_auth_valid_id(const char *id)
{
    size_t index;
    if (!id || strncmp(id, "xbox:", 5) || !id[5] || strlen(id) > 37) return 0;
    for (index = 5; id[index]; ++index) if (id[index] < '0' || id[index] > '9') return 0;
    return 1;
}
int maage_auth_store_load(maage_auth *auth)
{
    char *text = NULL;
    const char *id, *client, *name, *uuid;
    maage_auth_json *document, *items, *item;
    maage_auth_record *record;
    size_t index, previous;
    int read = maage_auth_metadata_read(&text);
    if (read <= 0) return read;
    document = maage_auth_json_parse(text); free(text);
    if (!document) return -1;
    items = maage_auth_json_get(document, "accounts");
    if (maage_auth_json_double(document, "version") != 1 || !maage_auth_json_is_array(items) || maage_auth_json_count(items) > MAAGE_AUTH_MAX_ACCOUNTS) { maage_auth_json_put(document); return -1; }
    auth->count = 0;
    for (index = 0; index < maage_auth_json_count(items); ++index) {
        item = maage_auth_json_at(items, index);
        id = maage_auth_json_text(item, "id"); client = maage_auth_json_text(item, "client_id");
        if (!maage_auth_valid_id(id) || !client || strcmp(client, auth->config.client_id)) continue;
        for (previous = 0; previous < auth->count; ++previous) if (!strcmp(auth->records[previous].id, id)) break;
        if (previous != auth->count) continue;
        record = &auth->records[auth->count++]; memset(record, 0, sizeof(*record));
        strcpy(record->id, id); strcpy(record->client_id, client);
        name = maage_auth_json_text(item, "name"); uuid = maage_auth_json_text(item, "uuid");
        maage_auth_copy(record->name, sizeof(record->name), name ? name : "Microsoft account");
        maage_auth_copy(record->uuid, sizeof(record->uuid), uuid ? uuid : "");
        /* Saved display metadata never establishes authentication or ownership. */
        record->error = MAAGE_AUTH_REAUTH;
        record->session_only = maage_auth_json_double(item, "remember") == 0;
    }
    id = maage_auth_json_text(document, "selected");
    if (maage_auth_valid_id(id)) maage_auth_copy(auth->selected, sizeof(auth->selected), id);
    for (index = 0; index < auth->count; ++index) if (!strcmp(auth->selected, auth->records[index].id)) break;
    if (index == auth->count) auth->selected[0] = '\0';
    if (!auth->selected[0] && auth->count) strcpy(auth->selected, auth->records[0].id);
    maage_auth_json_put(document);
    return 1;
}
int maage_auth_store_save(maage_auth *auth)
{
    maage_auth_json *document = maage_auth_json_object(), *items = maage_auth_json_array(), *item;
    char *text;
    size_t index;
    int result;
    maage_auth_json_number(document, "version", 1);
    maage_auth_json_string(document, "selected", auth->selected);
    for (index = 0; index < auth->count; ++index) {
        item = maage_auth_json_object();
        maage_auth_json_string(item, "id", auth->records[index].id);
        maage_auth_json_string(item, "client_id", auth->records[index].client_id);
        maage_auth_json_string(item, "name", auth->records[index].name);
        maage_auth_json_string(item, "uuid", auth->records[index].uuid);
        maage_auth_json_number(item, "remember", auth->records[index].session_only ? 0 : 1);
        maage_auth_json_append(items, item);
    }
    maage_auth_json_add(document, "accounts", items);
    text = maage_auth_json_encode(document);
    result = text ? maage_auth_metadata_write(text) : 0;
    free(text); maage_auth_json_put(document);
    return result;
}
