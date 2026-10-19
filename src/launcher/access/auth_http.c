#include "oauth.h"
#include "verify.h"
#include "maage/log.h"
#include <curl/curl.h>
#include <openssl/crypto.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <json-c/json_object_iterator.h>
typedef struct maage_auth_http {
    char data[131073];
    size_t used;
    int oversized;
    const maage_auth_observer *observer;
} maage_auth_http;
static size_t maage_auth_write(char *data, size_t size, size_t count, void *context)
{
    maage_auth_http *http = (maage_auth_http *)context;
    size_t bytes;
    if (size && count > (sizeof(http->data) - 1 - http->used) / size) { http->oversized = 1; return 0; }
    bytes = size * count; memcpy(http->data + http->used, data, bytes); http->used += bytes; http->data[http->used] = 0;
    return bytes;
}
static int maage_auth_progress(void *context, curl_off_t total, curl_off_t now, curl_off_t up_total, curl_off_t up_now)
{
    maage_auth_http *http = (maage_auth_http *)context;
    (void)total; (void)now; (void)up_total; (void)up_now;
    return http->observer->cancelled(http->observer->context);
}
static void maage_auth_json_wipe(struct json_object *object)
{
    size_t i;
    struct json_object_iterator iterator, end;
    if (!object) return;
    if (json_object_is_type(object, json_type_string)) OPENSSL_cleanse((char *)json_object_get_string(object), (size_t)json_object_get_string_len(object));
    else if (json_object_is_type(object, json_type_array)) {
        for (i = 0; i < json_object_array_length(object); ++i) maage_auth_json_wipe(json_object_array_get_idx(object, i));
    } else if (json_object_is_type(object, json_type_object)) {
        iterator = json_object_iter_begin(object); end = json_object_iter_end(object);
        while (!json_object_iter_equal(&iterator, &end)) {
            maage_auth_json_wipe(json_object_iter_peek_value(&iterator)); json_object_iter_next(&iterator);
        }
    }
}
void maage_auth_json_free(struct json_object *object)
{
    const char *serialized;
    if (!object) return;
    serialized = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    if (serialized) OPENSSL_cleanse((char *)serialized, strlen(serialized));
    maage_auth_json_wipe(object); json_object_put(object);
}
double maage_auth_number(struct json_object *object, const char *key)
{
    struct json_object *value;
    double number;
    if (!object || !json_object_object_get_ex(object, key, &value) || (!json_object_is_type(value, json_type_int) && !json_object_is_type(value, json_type_double))) return -1;
    number = json_object_get_double(value); return number >= 0 && number < 1.0e12 ? number : -1;
}
int maage_auth_token(char *out, size_t capacity, struct json_object *object, const char *key)
{
    const char *value = maage_access_json_string(object, key);
    size_t i, count = strlen(value);
    if (!count || count >= capacity) return 0;
    for (i = 0; i < count; ++i) if ((unsigned char)value[i] <= 32 || (unsigned char)value[i] >= 127) return 0;
    memcpy(out, value, count + 1); return 1;
}
const char *maage_auth_error_name(const char *error)
{
    static const char *names[] = {"authorization_pending", "slow_down", "invalid_grant",
        "invalid_client", "unauthorized_client", "invalid_request", "invalid_scope",
        "access_denied", "authorization_declined", "expired_token", "bad_verification_code",
        "server_error", "temporarily_unavailable", "ForbiddenOperationException", "NOT_FOUND"};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) if (!strcmp(error, names[i])) return names[i];
    return "unrecognized_error";
}
/* Log only known routes. Never log a supplied URL, including query strings or
 * request IDs. All other diagnostic strings below are local/library constants. */
static const char *maage_auth_log_endpoint(const char *endpoint)
{
    static const char *routes[] = {MAAGE_MICROSOFT_AUTHORITY "token", MAAGE_MICROSOFT_AUTHORITY "devicecode",
        "https://user.auth.xboxlive.com/user/authenticate", "https://xsts.auth.xboxlive.com/xsts/authorize",
        "https://api.minecraftservices.com/authentication/login_with_xbox", "https://api.minecraftservices.com/minecraft/profile",
        "https://api.minecraftservices.com/entitlements/mcstore", "https://api.minecraftservices.com/entitlements/license"};
    size_t i, length;
    for (i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        length = strlen(routes[i]);
        if (!strncmp(endpoint, routes[i], length) && (endpoint[length] == 0 || endpoint[length] == '?')) return routes[i];
    }
    return "unrecognized authentication endpoint";
}
static const char *maage_auth_error(struct json_object *object)
{
    const char *error = maage_access_json_string(object, "error");
    double xerr = maage_auth_number(object, "XErr");
    if (xerr == 2148916233.0) return "Xbox profile required. Create an Xbox profile for this account and retry.";
    if (xerr == 2148916235.0) return "Xbox Live is unavailable in this account's country or region.";
    if (xerr == 2148916236.0 || xerr == 2148916237.0) return "Xbox account requires age verification. Review your Xbox account settings.";
    if (xerr == 2148916238.0) return "Xbox family/child account restrictions prevent authorization. Review Microsoft Family settings.";
    if (!strcmp(error, "invalid_grant")) return "invalid_grant: saved authorization expired or was revoked. Sign in again.";
    if (!strcmp(error, "invalid_client") || !strcmp(error, "unauthorized_client")) return "Application/public-client redirect configuration rejected. Use device-code sign-in; review Entra registration.";
    if (!strcmp(error, "invalid_request")) return "invalid_request: authorization request rejected. Check the registered redirect; try device-code sign-in.";
    if (!strcmp(error, "invalid_scope")) return "invalid_scope: XboxLive.signin permission was rejected for MaaGE.";
    if (!strcmp(error, "access_denied") || !strcmp(error, "authorization_declined")) return "Authorization was declined. Sign in again when ready.";
    if (!strcmp(error, "expired_token")) return "The sign-in code expired. Start sign-in again.";
    if (!strcmp(maage_access_json_string(object, "errorMessage"), "Invalid app registration")) return "Invalid app registration";
    if (!strcmp(error, "ForbiddenOperationException")) return "ForbiddenOperationException: service denied this operation.";
    if (!strcmp(error, "NOT_FOUND")) return "Minecraft Java profile not found. Confirm Java Edition access and finish profile setup.";
    return "Service rejected the request (unrecognized details withheld). Retry or sign in again.";
}
struct json_object *maage_auth_request(const char *endpoint, const char *bearer,
    const char *body, int form, const maage_auth_observer *observer,
    long *status, char detail[256])
{
    maage_auth_http *http = (maage_auth_http *)calloc(1, sizeof(*http));
    CURL *curl = NULL;
    CURLcode result = CURLE_FAILED_INIT;
    struct curl_slist *headers = NULL, *entry;
    struct json_object *json = NULL;
    char *authorization = NULL;
    const char *route = maage_auth_log_endpoint(endpoint), *setup = "initialize HTTP client", *error;
    double elapsed = 0, xerr;
    int performed = 0;
    *status = 0; strcpy(detail, "Cannot initialize a secure authentication request.");
    if (!http) { maage_log_error("maage.launcher.access.http", "Cannot allocate HTTP response buffer for %s.", route); return NULL; }
    http->observer = observer; curl = curl_easy_init(); if (!curl) goto done;
    setup = "allocate request headers";
    headers = curl_slist_append(NULL, body ? (form ? "Content-Type: application/x-www-form-urlencoded" : "Content-Type: application/json") : "Accept: application/json");
    if (!headers) goto done;
    if (bearer) {
        authorization = (char *)malloc(strlen(bearer) + 24); if (!authorization) goto done;
        strcpy(authorization, "Authorization: Bearer "); strcat(authorization, bearer);
        entry = curl_slist_append(headers, authorization); if (!entry) goto done; headers = entry;
    }
    entry = curl_slist_append(headers, "x-xbl-contract-version: 1"); if (!entry) goto done; headers = entry;
#define MAAGE_AUTH_CURL(option, value) if ((result = curl_easy_setopt(curl, option, value)) != CURLE_OK) { setup = #option; goto done; }
    MAAGE_AUTH_CURL(CURLOPT_URL, endpoint);
    MAAGE_AUTH_CURL(CURLOPT_HTTPHEADER, headers);
    MAAGE_AUTH_CURL(CURLOPT_PROTOCOLS_STR, "https");
    MAAGE_AUTH_CURL(CURLOPT_FOLLOWLOCATION, 0L);
    MAAGE_AUTH_CURL(CURLOPT_SSL_VERIFYPEER, 1L);
    MAAGE_AUTH_CURL(CURLOPT_SSL_VERIFYHOST, 2L);
    MAAGE_AUTH_CURL(CURLOPT_CONNECTTIMEOUT, 10L);
    MAAGE_AUTH_CURL(CURLOPT_TIMEOUT, 30L);
    MAAGE_AUTH_CURL(CURLOPT_NOSIGNAL, 1L);
    MAAGE_AUTH_CURL(CURLOPT_USERAGENT, "MaaGE-Launcher");
    MAAGE_AUTH_CURL(CURLOPT_WRITEFUNCTION, maage_auth_write);
    MAAGE_AUTH_CURL(CURLOPT_WRITEDATA, http);
    MAAGE_AUTH_CURL(CURLOPT_XFERINFOFUNCTION, maage_auth_progress);
    MAAGE_AUTH_CURL(CURLOPT_XFERINFODATA, http);
    MAAGE_AUTH_CURL(CURLOPT_NOPROGRESS, 0L);
    if (body) { MAAGE_AUTH_CURL(CURLOPT_POSTFIELDS, body); }
#undef MAAGE_AUTH_CURL
    if (observer->cancelled(observer->context)) goto done;
    maage_log_debug("maage.launcher.access.http", "%s %s started.", body ? "POST" : "GET", route);
    performed = 1; result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status); curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &elapsed);
    if (result == CURLE_OK) json = maage_access_json(http->data, http->used);
    if (observer->cancelled(observer->context)) strcpy(detail, "Authentication cancelled. No access was granted.");
    else if (result != CURLE_OK) strcpy(detail, "Cannot reach authentication services securely. Check your connection and retry.");
    else if (*status != 200) {
        /* Endpoint is a compile-time constant. No server-supplied URL/error text
         * is echoed. Known errors are mapped to bounded, actionable messages. */
        sprintf(detail, "%.105s: HTTP %ld. %.130s", endpoint, *status, maage_auth_error(json));
    } else if (!json) strcpy(detail, "Authentication service returned a malformed or oversized response.");
    else strcpy(detail, "Authentication response received.");
done:
    if (observer->cancelled(observer->context)) maage_log_info("maage.launcher.access.http", "Request cancelled: %s (HTTP %ld; %.3f s).", route, *status, elapsed);
    else if (!performed) maage_log_error("maage.launcher.access.http", "Cannot %s for %s (curl=%d).", setup, route, (int)result);
    else if (http->oversized) maage_log_warn("maage.launcher.access.http", "Response size limit exceeded: %s (HTTP %ld; %.3f s).", route, *status, elapsed);
    else if (result != CURLE_OK) maage_log_warn("maage.launcher.access.http", "Transport/TLS request failed: %s (curl=%d: %s; HTTP %ld; %.3f s).", route, (int)result, curl_easy_strerror(result), *status, elapsed);
    else if (*status != 200) {
        error = maage_auth_error_name(maage_access_json_string(json, "error"));
        if (!strcmp(error, "authorization_pending") || !strcmp(error, "slow_down"))
            maage_log_debug("maage.launcher.access.http", "%s: HTTP %ld; %s (%.3f s).", route, *status, error, elapsed);
        else {
            maage_log_warn("maage.launcher.access.http", "%s: HTTP %ld; %s; %s (%.3f s).", route, *status, error, maage_auth_error(json), elapsed);
            xerr = maage_auth_number(json, "XErr");
            if (xerr >= 0) maage_log_warn("maage.launcher.access.http", "Xbox service error code XErr=%.0f.", xerr);
        }
    } else if (!json) maage_log_warn("maage.launcher.access.http", "Malformed JSON response: %s (HTTP %ld; %.3f s).", route, *status, elapsed);
    else maage_log_debug("maage.launcher.access.http", "%s completed: HTTP %ld (%.3f s).", route, *status, elapsed);
    if (curl) curl_easy_cleanup(curl);
    if (authorization) { OPENSSL_cleanse(authorization, strlen(authorization)); free(authorization); }
    for (entry = headers; entry; entry = entry->next) OPENSSL_cleanse(entry->data, strlen(entry->data));
    curl_slist_free_all(headers); OPENSSL_cleanse(http, sizeof(*http)); free(http); return json;
}
