#include "verify.h"
#include "maage/log.h"
#include <curl/curl.h>
#include <openssl/crypto.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
struct json_object *maage_access_json(const char *data, size_t size)
{
    struct json_tokener *tokener;
    struct json_object *object;
    size_t end;
    if (size > INT_MAX || memchr(data, 0, size)) return NULL;
    tokener = json_tokener_new_ex(32);
    if (!tokener) return NULL;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    object = json_tokener_parse_ex(tokener, data, (int)size);
    end = json_tokener_get_parse_end(tokener);
    while (end < size && (data[end] == ' ' || data[end] == '\n' || data[end] == '\r' || data[end] == '\t')) ++end;
    if (json_tokener_get_error(tokener) != json_tokener_success || end != size || !json_object_is_type(object, json_type_object)) {
        if (object) json_object_put(object);
        object = NULL;
    }
    json_tokener_free(tokener); return object;
}
const char *maage_access_json_string(struct json_object *object, const char *key)
{
    struct json_object *value;
    const char *text;
    if (!json_object_is_type(object, json_type_object) || !json_object_object_get_ex(object, key, &value) || !json_object_is_type(value, json_type_string)) return "";
    text = json_object_get_string(value);
    return strlen(text) == (size_t)json_object_get_string_len(value) ? text : "";
}
typedef struct maage_access_http {
    char data[65537];
    size_t used;
    int (*cancelled)(void *);
    void *context;
} maage_access_http;
static size_t maage_access_http_write(char *data, size_t size, size_t count, void *context)
{
    maage_access_http *http = (maage_access_http *)context;
    size_t bytes;
    if (size && count > (sizeof(http->data) - 1 - http->used) / size) return 0;
    bytes = size * count;
    memcpy(http->data + http->used, data, bytes); http->used += bytes; http->data[http->used] = 0;
    return bytes;
}
static int maage_access_http_progress(void *context, curl_off_t total, curl_off_t now, curl_off_t up_total, curl_off_t up_now)
{
    maage_access_http *http = (maage_access_http *)context;
    (void)total; (void)now; (void)up_total; (void)up_now;
    return http->cancelled(http->context);
}
static struct json_object *maage_access_get(const char *endpoint, const char *token,
    maage_access_http *http, maage_access_state *state, char detail[256])
{
    CURL *curl;
    CURLcode result = CURLE_FAILED_INIT;
    struct curl_slist *headers = NULL, *entry;
    struct json_object *json = NULL;
    long status = 0;
    size_t length = strlen(token);
    char *authorization;
    curl = curl_easy_init();
    authorization = (char *)malloc(length + 32);
    if (!curl || !authorization) { if (curl) curl_easy_cleanup(curl); free(authorization); strcpy(detail, "Not enough memory to verify this account."); return NULL; }
    strcpy(authorization, "Authorization: Bearer "); strcat(authorization, token);
    headers = curl_slist_append(NULL, authorization);
    OPENSSL_cleanse(authorization, length + 32); free(authorization);
    http->used = 0;
    if (!headers) goto done;
#define MAAGE_ACCESS_CURL(option, value) if (curl_easy_setopt(curl, option, value) != CURLE_OK) goto done
    MAAGE_ACCESS_CURL(CURLOPT_URL, endpoint);
    MAAGE_ACCESS_CURL(CURLOPT_HTTPHEADER, headers);
    MAAGE_ACCESS_CURL(CURLOPT_PROTOCOLS_STR, "https");
    MAAGE_ACCESS_CURL(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    MAAGE_ACCESS_CURL(CURLOPT_FOLLOWLOCATION, 0L);
    MAAGE_ACCESS_CURL(CURLOPT_SSL_VERIFYPEER, 1L);
    MAAGE_ACCESS_CURL(CURLOPT_SSL_VERIFYHOST, 2L);
    MAAGE_ACCESS_CURL(CURLOPT_CONNECTTIMEOUT, 10L);
    MAAGE_ACCESS_CURL(CURLOPT_TIMEOUT, 30L);
    MAAGE_ACCESS_CURL(CURLOPT_NOSIGNAL, 1L);
    MAAGE_ACCESS_CURL(CURLOPT_USERAGENT, "MaaGE-Launcher");
    MAAGE_ACCESS_CURL(CURLOPT_WRITEFUNCTION, maage_access_http_write);
    MAAGE_ACCESS_CURL(CURLOPT_WRITEDATA, http);
    MAAGE_ACCESS_CURL(CURLOPT_XFERINFOFUNCTION, maage_access_http_progress);
    MAAGE_ACCESS_CURL(CURLOPT_XFERINFODATA, http);
    MAAGE_ACCESS_CURL(CURLOPT_NOPROGRESS, 0L);
#undef MAAGE_ACCESS_CURL
    if (http->cancelled(http->context)) goto done;
    result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
done:
    /* Cleanup curl's header references before wiping our explicit header copy. */
    curl_easy_cleanup(curl);
    for (entry = headers; entry; entry = entry->next) OPENSSL_cleanse(entry->data, strlen(entry->data));
    curl_slist_free_all(headers);
    if (http->cancelled(http->context)) strcpy(detail, "Verification cancelled. No access was granted.");
    else if (result != CURLE_OK) strcpy(detail, "Cannot reach Minecraft Services securely. Check your connection and retry.");
    else if (status == 401) { *state = MAAGE_ACCESS_EXPIRED; strcpy(detail, "The Minecraft session expired or was rejected. Sign in again in the original launcher, then retry."); }
    else if (status == 403) strcpy(detail, "Minecraft Services rejected this session. It may be application-bound. Refresh it in the original launcher; access remains unverified.");
    else if (status != 200) strcpy(detail, "Minecraft Services could not confirm access. Refresh your original launcher session and retry later.");
    else {
        json = maage_access_json(http->data, http->used);
        if (!json) strcpy(detail, "Minecraft Services returned an unsupported response. Access remains unverified.");
    }
    OPENSSL_cleanse(http->data, sizeof(http->data));
    return json;
}
maage_access_state maage_access_verify(const char *token, const char *uuid,
    char name[17], char detail[256], int (*cancelled)(void *), void *context)
{
    maage_access_http *http;
    struct json_object *profile = NULL, *entitlements = NULL, *items, *item;
    maage_access_state state = MAAGE_ACCESS_FAILED;
    char normalized[33];
    const char *text;
    size_t i, length = strlen(token), count;
    int game = 0;
    strcpy(detail, "The original launcher has no compatible Minecraft Services token. Refresh its session and retry.");
    if (length < 16 || length >= MAAGE_ACCESS_TOKEN_CAPACITY) return state;
    for (i = 0; i < length; ++i) {
        int c = (unsigned char)token[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '~' || c == '=' || c == '+' || c == '/')) return state;
    }
    http = (maage_access_http *)calloc(1, sizeof(*http));
    if (!http) { strcpy(detail, "Not enough memory to verify access."); return state; }
    http->cancelled = cancelled; http->context = context;
    maage_log_info("maage.launcher.access", "Checking authenticated Minecraft profile with Minecraft Services.");
    profile = maage_access_get("https://api.minecraftservices.com/minecraft/profile", token, http, &state, detail);
    if (!profile) goto done;
    text = maage_access_json_string(profile, "name");
    if (!maage_access_username(text) || !maage_access_uuid(maage_access_json_string(profile, "id"), normalized) || strcmp(normalized, uuid)) {
        strcpy(detail, "The authenticated Minecraft profile does not match the selected account. Rescan and select it again."); goto done;
    }
    strcpy(name, text);
    maage_log_info("maage.launcher.access", "Authenticated profile matched; checking Minecraft Java entitlements.");
    entitlements = maage_access_get("https://api.minecraftservices.com/entitlements/mcstore", token, http, &state, detail);
    if (!entitlements) goto done;
    strcpy(detail, "Minecraft Java access was not confirmed by the entitlement response. Check access in the original launcher and retry.");
    if (!json_object_object_get_ex(entitlements, "items", &items) || !json_object_is_type(items, json_type_array)) goto done;
    count = json_object_array_length(items);
    if (count > 256) goto done;
    for (i = 0; i < count; ++i) {
        item = json_object_array_get_idx(items, i);
        if (!json_object_is_type(item, json_type_object)) goto done;
        text = maage_access_json_string(item, "name");
        if (!*text || !*maage_access_json_string(item, "signature")) goto done;
        if (!strcmp(text, "game_minecraft")) game = 1;
    }
    /* The authenticated TLS endpoint is the authority; cached flags and local
     * entitlement objects are never consulted. Do not interpret unrelated SKUs. */
    if (game && !cancelled(context)) {
        state = MAAGE_ACCESS_VERIFIED;
        strcpy(detail, "Minecraft Java access verified. The account service can retain a bounded local verification lease.");
    }
done:
    if (profile) json_object_put(profile);
    if (entitlements) json_object_put(entitlements);
    OPENSSL_cleanse(http, sizeof(*http)); free(http);
    return state;
}
