#include "internal.h"
#include <curl/curl.h>
#include <json-c/json.h>
#if JSON_C_VERSION_NUM < (15 << 8)
#error MaaGE authentication requires json-c 0.15 or newer
#endif
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct maage_auth_response { char *data; size_t size; } maage_auth_response;
static size_t maage_auth_receive(char *bytes, size_t size, size_t count, void *context)
{
    maage_auth_response *response = context;
    size_t length;
    char *grown;
    if (size && count > MAAGE_AUTH_RESPONSE_LIMIT / size) return 0;
    length = size * count;
    if (length > MAAGE_AUTH_RESPONSE_LIMIT - response->size) return 0;
    grown = realloc(response->data, response->size + length + 1);
    if (!grown) return 0;
    response->data = grown;
    memcpy(grown + response->size, bytes, length);
    response->size += length; grown[response->size] = '\0';
    return length;
}
static int maage_auth_transfer(void *context, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)a; (void)b; (void)c; (void)d;
    return maage_auth_cancelled(context);
}
int maage_auth_http_init(void)
{
    const curl_version_info_data *version;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 0;
    version = curl_version_info(CURLVERSION_NOW);
    if (!version || version->version_num < 0x075500 || !(version->features & CURL_VERSION_SSL)) {
        curl_global_cleanup(); return 0;
    }
    return 1;
}
void maage_auth_http_cleanup(void) { curl_global_cleanup(); }
char *maage_auth_escape(const char *text)
{
    char *escaped = curl_easy_escape(NULL, text, 0), *copy = NULL;
    if (escaped) { copy = malloc(strlen(escaped) + 1); if (copy) strcpy(copy, escaped); curl_free(escaped); }
    return copy;
}
maage_auth_error maage_auth_http_bytes(maage_auth *auth, const char *url, const char *type, const char *body, const char *bearer, long *status, char **bytes, size_t *length)
{
    CURL *curl;
    CURLcode result;
    struct curl_slist *headers = NULL, *next;
    maage_auth_response response = { NULL, 0 };
    char authorization[MAAGE_AUTH_TOKEN_SIZE + 32], content_type[96];
    const char *header_list[4];
    size_t index, header_count = 0;
    maage_auth_error error = MAAGE_AUTH_PROTOCOL;
    *status = 0; *bytes = NULL; *length = 0;
    if (!url || strncmp(url, "https://", 8) || !type || strlen(type) > 60 || (body && strlen(body) > MAAGE_AUTH_RESPONSE_LIMIT)) return error;
    if (bearer) for (index = 0; bearer[index]; ++index)
        if ((unsigned char)bearer[index] <= 32 || (unsigned char)bearer[index] >= 127) return error;
    curl = curl_easy_init();
    if (!curl) return error;
    strcpy(content_type, "Content-Type: "); strcat(content_type, type);
    strcpy(authorization, "Authorization: Bearer ");
    if (bearer && !maage_auth_copy(authorization + 22, sizeof(authorization) - 22, bearer)) goto cleanup;
    header_list[header_count++] = !strcmp(type, "image/png") ? "Accept: image/png" : "Accept: application/json";
    header_list[header_count++] = content_type;
    if (!strcmp(url, auth->config.xbox) || !strcmp(url, auth->config.xsts)) header_list[header_count++] = "x-xbl-contract-version: 1";
    if (bearer) header_list[header_count++] = authorization;
    for (index = 0; index < header_count; ++index) {
        next = curl_slist_append(headers, header_list[index]);
        if (!next) goto cleanup;
        headers = next;
    }
#define MAAGE_CURL_SET(option, value) if (curl_easy_setopt(curl, option, value) != CURLE_OK) goto cleanup
    MAAGE_CURL_SET(CURLOPT_URL, url);
    MAAGE_CURL_SET(CURLOPT_PROTOCOLS_STR, "https");
    MAAGE_CURL_SET(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    MAAGE_CURL_SET(CURLOPT_FOLLOWLOCATION, 0L);
    MAAGE_CURL_SET(CURLOPT_SSL_VERIFYPEER, 1L);
    MAAGE_CURL_SET(CURLOPT_SSL_VERIFYHOST, 2L);
    MAAGE_CURL_SET(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    MAAGE_CURL_SET(CURLOPT_CONNECTTIMEOUT, 10L);
    MAAGE_CURL_SET(CURLOPT_TIMEOUT, 35L);
    MAAGE_CURL_SET(CURLOPT_LOW_SPEED_TIME, 15L);
    MAAGE_CURL_SET(CURLOPT_LOW_SPEED_LIMIT, 32L);
    MAAGE_CURL_SET(CURLOPT_NOSIGNAL, 1L);
    MAAGE_CURL_SET(CURLOPT_NOPROGRESS, 0L);
    MAAGE_CURL_SET(CURLOPT_XFERINFOFUNCTION, maage_auth_transfer);
    MAAGE_CURL_SET(CURLOPT_XFERINFODATA, auth);
    MAAGE_CURL_SET(CURLOPT_WRITEFUNCTION, maage_auth_receive);
    MAAGE_CURL_SET(CURLOPT_WRITEDATA, &response);
    MAAGE_CURL_SET(CURLOPT_HTTPHEADER, headers);
    MAAGE_CURL_SET(CURLOPT_USERAGENT, "MaaGE/1.0 Launcher");
    if (body) { MAAGE_CURL_SET(CURLOPT_POSTFIELDS, body); MAAGE_CURL_SET(CURLOPT_POSTFIELDSIZE, (long)strlen(body)); }
    result = curl_easy_perform(curl);
    if (maage_auth_cancelled(auth)) { error = MAAGE_AUTH_CANCELLED; goto cleanup; }
    if (result != CURLE_OK) { error = MAAGE_AUTH_NETWORK; goto cleanup; }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    if (*status == 429 || *status >= 500) { error = MAAGE_AUTH_NETWORK; goto cleanup; }
    *bytes = response.data; *length = response.size; response.data = NULL;
    error = MAAGE_AUTH_OK;
cleanup:
    curl_slist_free_all(headers); curl_easy_cleanup(curl);
    maage_auth_wipe(authorization, sizeof(authorization));
    if (response.data) { maage_auth_wipe(response.data, response.size); free(response.data); }
    return error;
#undef MAAGE_CURL_SET
}
maage_auth_error maage_auth_http(maage_auth *auth, const char *url, const char *type, const char *body, const char *bearer, long *status, maage_auth_json **json)
{
    char *bytes = NULL;
    size_t length = 0;
    maage_auth_error result = maage_auth_http_bytes(auth, url, type, body, bearer, status, &bytes, &length);
    *json = NULL;
    if (result == MAAGE_AUTH_OK) {
        if (bytes && !memchr(bytes, 0, length)) *json = maage_auth_json_parse(bytes);
        if (!*json && *status < 400) result = MAAGE_AUTH_PROTOCOL;
    }
    if (bytes) { maage_auth_wipe(bytes, length); free(bytes); }
    return result;
}
maage_auth_json *maage_auth_json_parse(const char *text)
{
    struct json_tokener *tokener;
    maage_auth_json *json;
    size_t length, end;
    if (!text || (length = strlen(text)) > MAAGE_AUTH_RESPONSE_LIMIT) return NULL;
    tokener = json_tokener_new_ex(32);
    if (!tokener) return NULL;
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    json = json_tokener_parse_ex(tokener, text, (int)length);
    end = json_tokener_get_parse_end(tokener);
    while (end < length && isspace((unsigned char)text[end])) ++end;
    if (json_tokener_get_error(tokener) != json_tokener_success || end != length) { json_object_put(json); json = NULL; }
    json_tokener_free(tokener);
    return json;
}
maage_auth_json *maage_auth_json_object(void) { return json_object_new_object(); }
maage_auth_json *maage_auth_json_array(void) { return json_object_new_array(); }
void maage_auth_json_put(maage_auth_json *json) { if (json) json_object_put(json); }
void maage_auth_json_add(maage_auth_json *json, const char *key, maage_auth_json *value)
{
    if (!json || !value) { maage_auth_json_put(value); return; }
    json_object_object_add(json, key, value);
}
void maage_auth_json_string(maage_auth_json *json, const char *key, const char *text) { maage_auth_json_add(json, key, json_object_new_string(text)); }
void maage_auth_json_number(maage_auth_json *json, const char *key, double value) { maage_auth_json_add(json, key, json_object_new_double(value)); }
void maage_auth_json_append(maage_auth_json *json, maage_auth_json *value)
{
    if (json && value) json_object_array_add(json, value); else maage_auth_json_put(value);
}
void maage_auth_json_array_string(maage_auth_json *json, const char *text) { maage_auth_json_append(json, json_object_new_string(text)); }
maage_auth_json *maage_auth_json_get(maage_auth_json *json, const char *key)
{
    maage_auth_json *value = NULL;
    if (json && json_object_is_type(json, json_type_object)) json_object_object_get_ex(json, key, &value);
    return value;
}
maage_auth_json *maage_auth_json_at(maage_auth_json *json, size_t index) { return maage_auth_json_is_array(json) ? json_object_array_get_idx(json, index) : NULL; }
const char *maage_auth_json_text(maage_auth_json *json, const char *key)
{
    maage_auth_json *value = maage_auth_json_get(json, key);
    const char *text;
    if (!value || !json_object_is_type(value, json_type_string)) return NULL;
    text = json_object_get_string(value);
    return strlen(text) == (size_t)json_object_get_string_len(value) ? text : NULL;
}
double maage_auth_json_double(maage_auth_json *json, const char *key)
{
    maage_auth_json *value = maage_auth_json_get(json, key);
    return value && (json_object_is_type(value, json_type_int) || json_object_is_type(value, json_type_double)) ? json_object_get_double(value) : -1;
}
size_t maage_auth_json_count(maage_auth_json *json) { return maage_auth_json_is_array(json) ? json_object_array_length(json) : 0; }
int maage_auth_json_is_array(maage_auth_json *json) { return json && json_object_is_type(json, json_type_array); }
char *maage_auth_json_encode(maage_auth_json *json)
{
    const char *encoded;
    char *copy;
    if (!json) return NULL;
    encoded = json_object_to_json_string_ext(json, JSON_C_TO_STRING_PLAIN);
    if (!encoded) return NULL;
    copy = malloc(strlen(encoded) + 1);
    if (copy) strcpy(copy, encoded);
    return copy;
}
