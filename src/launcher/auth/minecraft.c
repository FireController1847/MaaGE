#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int maage_auth_uuid(const char *text)
{
    size_t index;
    if (!text || strlen(text) != 32) return 0;
    for (index = 0; index < 32; ++index)
        if (!((text[index] >= '0' && text[index] <= '9') || (text[index] >= 'a' && text[index] <= 'f') || (text[index] >= 'A' && text[index] <= 'F'))) return 0;
    return 1;
}
static int maage_auth_player_name(const char *text)
{
    size_t index, size;
    if (!text || !(size = strlen(text)) || size > 16) return 0;
    for (index = 0; index < size; ++index) if (!((text[index] >= 'a' && text[index] <= 'z') ||
        (text[index] >= 'A' && text[index] <= 'Z') || (text[index] >= '0' && text[index] <= '9') || text[index] == '_')) return 0;
    return 1;
}
maage_auth_error maage_auth_minecraft(maage_auth *auth, maage_auth_record *record, const char *token, const char *hash)
{
    maage_auth_json *request = NULL, *response = NULL, *items;
    char *identity = NULL, *body = NULL;
    char url[1024], request_id[37], hex[33];
    unsigned char random[16];
    const char *text, *uuid, *name;
    maage_auth_error result = MAAGE_AUTH_PROTOCOL;
    double expires;
    long status;
    size_t index;
    if (strlen(auth->config.minecraft) > 800) return result;
    identity = malloc(strlen(token) + strlen(hash) + 16);
    if (!identity) return result;
    sprintf(identity, "XBL3.0 x=%s;%s", hash, token);
    request = maage_auth_json_object();
    maage_auth_json_string(request, "xtoken", identity);
    maage_auth_json_string(request, "platform", "PC_LAUNCHER");
    body = maage_auth_json_encode(request);
    if (!body) goto cleanup;
    sprintf(url, "%s/launcher/login", auth->config.minecraft);
    maage_auth_progress(auth, "Signing in to Minecraft Services...");
    result = maage_auth_http(auth, url, "application/json", body, NULL, &status, &response);
    if (result != MAAGE_AUTH_OK) goto cleanup;
    if (status != 200) { result = status == 401 || status == 403 ? MAAGE_AUTH_CONFIGURATION : MAAGE_AUTH_PROTOCOL; goto cleanup; }
    expires = maage_auth_json_double(response, "expires_in");
    if (!(expires > 60 && expires <= 86400 * 31) || !maage_auth_copy(record->minecraft, sizeof(record->minecraft), maage_auth_json_text(response, "access_token")) || !*record->minecraft) {
        result = MAAGE_AUTH_PROTOCOL; goto cleanup;
    }
    record->expires_at = (double)time(NULL) + expires;
    maage_auth_json_put(response); response = NULL;
    if (!maage_auth_random(random, sizeof(random))) { result = MAAGE_AUTH_PROTOCOL; goto cleanup; }
    random[6] = (unsigned char)((random[6] & 15) | 64); random[8] = (unsigned char)((random[8] & 63) | 128);
    for (index = 0; index < sizeof(random); ++index) sprintf(hex + index * 2, "%02x", (unsigned int)random[index]);
    sprintf(request_id, "%.8s-%.4s-%.4s-%.4s-%.12s", hex, hex + 8, hex + 12, hex + 16, hex + 20);
    sprintf(url, "%s/entitlements/license?requestId=%s", auth->config.minecraft, request_id);
    maage_auth_progress(auth, "Checking Minecraft Java ownership...");
    result = maage_auth_http(auth, url, "application/json", NULL, record->minecraft, &status, &response);
    if (result != MAAGE_AUTH_OK) goto cleanup;
    items = maage_auth_json_get(response, "items");
    if (status != 200 || !maage_auth_json_is_array(items)) { result = status == 401 ? MAAGE_AUTH_SESSION : MAAGE_AUTH_PROTOCOL; goto cleanup; }
    record->entitled = record->owns_java = 0;
    for (index = 0; index < maage_auth_json_count(items); ++index) {
        text = maage_auth_json_text(maage_auth_json_at(items, index), "name");
        if (text && !strcmp(text, "game_minecraft")) record->entitled = 1;
        if (text && !strcmp(text, "product_minecraft")) record->owns_java = 1;
    }
    maage_auth_json_put(response); response = NULL;
    sprintf(url, "%s/minecraft/profile", auth->config.minecraft);
    maage_auth_progress(auth, "Retrieving Minecraft player name and UUID...");
    result = maage_auth_http(auth, url, "application/json", NULL, record->minecraft, &status, &response);
    if (result != MAAGE_AUTH_OK) goto cleanup;
    if (status == 404 && (!record->entitled || !record->owns_java)) { result = MAAGE_AUTH_OK; goto cleanup; }
    if (status == 404) { result = MAAGE_AUTH_PROFILE; goto cleanup; }
    uuid = maage_auth_json_text(response, "id"); name = maage_auth_json_text(response, "name");
    if (status != 200 || !maage_auth_uuid(uuid) || !maage_auth_player_name(name)) { result = status == 401 ? MAAGE_AUTH_SESSION : MAAGE_AUTH_PROTOCOL; goto cleanup; }
    strcpy(record->uuid, uuid); strcpy(record->name, name); record->profile_valid = 1;
    maage_auth_avatar(auth, record, response);
cleanup:
    maage_auth_json_put(request); maage_auth_json_put(response);
    maage_auth_free_secret(body); maage_auth_free_secret(identity);
    if (result != MAAGE_AUTH_OK) { maage_auth_wipe(record->minecraft, sizeof(record->minecraft)); record->expires_at = 0; record->profile_valid = 0; }
    return result;
}
