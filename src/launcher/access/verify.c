#include "verify.h"
#include "maage/log.h"
#include <curl/curl.h>
#include <openssl/crypto.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
maage_status maage_access_json(const char *data, size_t size, struct json_object **output)
{
    struct json_tokener *tokener;
    struct json_object *object;
    size_t end;
    if (output) *output = NULL;
    if (!data || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE, !data ? 0 : 2);
    if (size > INT_MAX || memchr(data, 0, size)) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE, 0);
    tokener = json_tokener_new_ex(32);
    if (!tokener) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE, 0);
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    object = json_tokener_parse_ex(tokener, data, (int)size);
    end = json_tokener_get_parse_end(tokener);
    while (end < size && (data[end] == ' ' || data[end] == '\n' || data[end] == '\r' || data[end] == '\t')) ++end;
    if (json_tokener_get_error(tokener) != json_tokener_success || end != size || !json_object_is_type(object, json_type_object)) {
        if (object) json_object_put(object);
        object = NULL;
    }
    json_tokener_free(tokener);
    *output = object;
    return maage_status_make(object ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CONFIG, MAAGE_OPERATION_PARSE, 0);
}
const char *maage_access_json_string(struct json_object *object, const char *key)
{
    struct json_object *value;
    const char *text;
    if (!json_object_is_type(object, json_type_object) || !json_object_object_get_ex(object, key, &value) || !json_object_is_type(value, json_type_string)) return "";
    text = json_object_get_string(value);
    return strlen(text) == (size_t)json_object_get_string_len(value) ? text : "";
}
/* TLS-authenticated store and license responses describe current access, not a
 * permanent-purchase guarantee. Accept only explicit Java game entitlements. */
#include "oauth.h"
static int maage_access_java_items(struct json_object *response, int license)
{
    struct json_object *items, *item, *errors;
    size_t i, count;
    int game = 0;
    const char *name;
    if (!*maage_access_json_string(response, "signature") || !*maage_access_json_string(response, "keyId") ||
        !json_object_object_get_ex(response, "items", &items) || !json_object_is_type(items, json_type_array)) return 0;
    if (json_object_object_get_ex(response, "errors", &errors) &&
        (!json_object_is_type(errors, json_type_array) || json_object_array_length(errors))) return 0;
    count = json_object_array_length(items); if (count > 256) return 0;
    for (i = 0; i < count; ++i) {
        item = json_object_array_get_idx(items, i);
        if (!json_object_is_type(item, json_type_object)) return 0;
        name = maage_access_json_string(item, "name");
        if (!*name || !*maage_access_json_string(item, license ? "source" : "signature")) return 0;
        if (!strcmp(name, "game_minecraft")) game = 1;
    }
    return game;
}
maage_status maage_access_verify(const char *token, char uuid[33],
    char name[17], char detail[256], int (*cancelled)(void *), void *context,
    maage_access_state *output_state, maage_status_trace *trace)
{
    maage_auth_observer observer;
    struct json_object *profile = NULL, *entitlements = NULL, *license = NULL;
    maage_access_state state = MAAGE_ACCESS_FAILED;
    char normalized[33], endpoint[160], request[37], store_error[256];
    unsigned char random[16];
    const char *text;
    long status;
    int game = 0, licensed = 0, profile_valid = 0;
    maage_status outcome;
    if (output_state) *output_state = MAAGE_ACCESS_FAILED;
    if (!token || !uuid || !name || !detail || !cancelled || !output_state)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_VERIFY, 0);
    memset(&observer, 0, sizeof(observer)); observer.cancelled = cancelled; observer.context = context;
    observer.trace = trace;
    maage_log_info("maage.launcher.access.verify", "Checking authenticated Minecraft Java profile.");
    outcome = maage_auth_request("https://api.minecraftservices.com/minecraft/profile", token, NULL, 0, &observer, &status, detail, &profile);
    if (status == 401) state = MAAGE_ACCESS_EXPIRED;
    if (!profile || status != 200) goto done;
    text = maage_access_json_string(profile, "name");
    if (!maage_access_username(text) || !maage_access_uuid(maage_access_json_string(profile, "id"), normalized) || (*uuid && strcmp(normalized, uuid))) {
        maage_log_verbose("maage.launcher.access.verify", "Profile response rejected: invalid Java identity or mismatch with selected account.");
        strcpy(detail, "Minecraft Java profile missing or different from the selected account. Sign in with the correct Microsoft account."); goto done;
    }
    profile_valid = 1;
    maage_log_info("maage.launcher.access.verify", "Java profile validated; checking explicit Java entitlements.");
    strcpy(name, text);
    outcome = maage_auth_request("https://api.minecraftservices.com/entitlements/mcstore", token, NULL, 0, &observer, &status, detail, &entitlements);
    if (status == 401) state = MAAGE_ACCESS_EXPIRED;
    if (status == 401 || status == 403 || cancelled(context)) goto done;
    if (status == 200 && entitlements) game = maage_access_java_items(entitlements, 0);
    maage_log_debug("maage.launcher.access.verify", "Store entitlement check: explicit Java entitlement %s.", game ? "present" : "not confirmed");
    strcpy(store_error, detail);
    /* The license endpoint includes non-store access (e.g. subscriptions).
     * Its signed aggregate has item name/source, not per-item signatures.
     * Treat source as opaque; it is not a permanent-ownership warranty. */
    if (maage_status_failed(maage_auth_random(random, sizeof(random)))) { maage_log_verbose("maage.launcher.access.verify", "Cannot generate entitlement request identifier; verification stopped."); strcpy(detail, "Cannot generate a secure entitlement request identifier."); goto done; }
    random[6] = (unsigned char)((random[6] & 15) | 64); random[8] = (unsigned char)((random[8] & 63) | 128);
    sprintf(request, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        random[0], random[1], random[2], random[3], random[4], random[5], random[6], random[7], random[8], random[9], random[10], random[11], random[12], random[13], random[14], random[15]);
    sprintf(endpoint, "https://api.minecraftservices.com/entitlements/license?requestId=%s", request);
    outcome = maage_auth_request(endpoint, token, NULL, 0, &observer, &status, detail, &license);
    if (status == 401) state = MAAGE_ACCESS_EXPIRED;
    if (status == 401 || status == 403 || cancelled(context)) goto done;
    if (status == 200 && license) licensed = maage_access_java_items(license, 1);
    maage_log_debug("maage.launcher.access.verify", "License entitlement check: explicit Java entitlement %s.", licensed ? "present" : "not confirmed");
    if (game || licensed) {
        strcpy(uuid, normalized); state = MAAGE_ACCESS_VERIFIED;
        strcpy(detail, "Minecraft Java Edition access verified. These services do not conclusively establish permanent purchase versus subscription access.");
    } else if (status == 200 && license) strcpy(detail, "Minecraft Java Edition access could not be confirmed. A valid Java profile and explicit Java entitlement are required; empty lists and Bedrock-only access do not qualify.");
    else {
        char failure[256];
        strcpy(failure, !entitlements ? store_error : detail);
        sprintf(detail, "Minecraft Java Edition access could not be confirmed. %.195s", failure);
    }
done:
    if (state == MAAGE_ACCESS_VERIFIED) maage_log_info("maage.launcher.access.verify", "Java access verified (store entitlement=%s; license entitlement=%s).", game ? "confirmed" : "not confirmed", licensed ? "confirmed" : "not confirmed");
    else if (!cancelled(context)) maage_log_verbose("maage.launcher.access.verify", "Java verification failed: %s. No access lease granted.", !profile_valid ? "profile could not be validated" : "explicit Java entitlement could not be confirmed");
    maage_auth_json_free(profile); maage_auth_json_free(entitlements); maage_auth_json_free(license);
    *output_state = state;
    if (state == MAAGE_ACCESS_VERIFIED) outcome = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_VERIFY, 0);
    else if (cancelled(context)) outcome = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_VERIFY, 0);
    else if (!maage_status_failed(outcome)) outcome = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_VERIFY, 0);
    return MAAGE_STATUS_PROPAGATE(trace, outcome, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_VERIFY);
}
