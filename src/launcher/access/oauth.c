#include "oauth.h"
#include "verify.h"
#include "maage/time.h"
#include "maage/log.h"
#include "maage/crypto.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct maage_microsoft_session {
    char access[MAAGE_ACCESS_TOKEN_CAPACITY];
    double expires_at;
} maage_microsoft_session;
typedef struct maage_xbox_session {
    char token[MAAGE_ACCESS_TOKEN_CAPACITY], hash[65];
} maage_xbox_session;
static double maage_auth_wall(void) {
    return difftime(time(NULL), (time_t) 0);
}
static char *maage_oauth_form(const char *grant, const char *credential, const char *verifier, const char *redirect) {
    CURL *curl = curl_easy_init();
    char *escaped = NULL, *redirect_value = NULL, *body = NULL;
    size_t capacity;
    if (!curl) return NULL;
    escaped = curl_easy_escape(curl, credential, 0);
    if (verifier && redirect) redirect_value = curl_easy_escape(curl, redirect, 0);
    if (escaped && (!verifier || redirect_value)) {
        capacity = strlen(escaped) + (redirect_value ? strlen(redirect_value) : 0) + 1024;
        body = (char *) malloc(capacity);
        if (body) {
            sprintf(body, "client_id=%s&scope=XboxLive.signin%%20offline_access&grant_type=%s&%s=%s", MAAGE_MICROSOFT_CLIENT_ID, grant,
                    !strcmp(grant, "authorization_code") ? "code" : !strcmp(grant, "refresh_token") ? "refresh_token"
                                                                                                    : "device_code",
                    escaped);
            if (verifier) {
                strcat(body, "&redirect_uri=");
                strcat(body, redirect_value);
                strcat(body, "&code_verifier=");
                strcat(body, verifier);
            }
        }
    }
    if (escaped) {
        maage_crypto_clear(escaped, strlen(escaped));
        curl_free(escaped);
    }
    if (redirect_value) curl_free(redirect_value);
    curl_easy_cleanup(curl);
    return body;
}
static int maage_oauth_scope(const char *scopes) {
    const char *end;
    size_t length;
    /* RFC 6749 permits omission when granted scopes equal those requested. */
    if (!*scopes) return 1;
    while (*scopes) {
        end = strchr(scopes, ' ');
        if (!end) end = scopes + strlen(scopes);
        length = (size_t) (end - scopes);
        if (length == sizeof("XboxLive.signin") - 1 && !memcmp(scopes, "XboxLive.signin", length)) return 1;
        scopes = *end ? end + 1 : end;
    }
    return 0;
}
static maage_status maage_oauth_tokens(struct json_object *json, maage_microsoft_session *ms, maage_profile_session *s) {
    double expires = maage_auth_number(json, "expires_in");
    const char *refresh = maage_access_json_string(json, "refresh_token");
    const char *scope = maage_access_json_string(json, "scope");
    if ((strcmp(maage_access_json_string(json, "token_type"), "Bearer") && strcmp(maage_access_json_string(json, "token_type"), "bearer")) || expires < 1 || expires > 86400 || !maage_oauth_scope(scope) || maage_status_failed(maage_auth_token(ms->access, sizeof(ms->access), json, "access_token"))) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_PARSE, 0);
    if (*refresh) maage_log_verbose("maage.launcher.access.oauth", "Microsoft supplied a replacement refresh credential; value withheld.");
    if (*refresh && maage_status_failed(maage_auth_token(s->microsoft_refresh, sizeof(s->microsoft_refresh), json, "refresh_token"))) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_PARSE, 0);
    if (!*s->microsoft_refresh) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_PARSE, 0);
    ms->expires_at = maage_auth_wall() + expires;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PARSE, 0);
}
static maage_status maage_oauth_exchange(const char *grant, const char *credential, const char *verifier, const char *redirect, maage_microsoft_session *ms, maage_profile_session *s, const maage_auth_observer *o, char detail[256]) {
    char *body = maage_oauth_form(grant, credential, verifier, redirect);
    struct json_object *json;
    long status;
    int ok;
    maage_status result;
    if (!body) {
        strcpy(detail, "Cannot allocate the OAuth request.");
        return MAAGE_STATUS_TRACE(o->trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
    }
    maage_log_info("maage.launcher.access.oauth", "Exchanging %s with Microsoft.", verifier ? "authorization code with PKCE" : "refresh credential");
    if (verifier) maage_log_info("maage.launcher.access.oauth", "Authorization-code exchange configuration: endpoint=%s; client_id=%s; redirect_uri=%s.", MAAGE_MICROSOFT_AUTHORITY "token", MAAGE_MICROSOFT_CLIENT_ID, redirect);
    result = maage_auth_request(MAAGE_MICROSOFT_AUTHORITY "token", NULL, body, 1, o, &status, detail, &json);
    maage_crypto_clear(body, strlen(body));
    free(body);
    ok = status == 200 && json && !maage_status_failed(result = maage_oauth_tokens(json, ms, s));
    if (status == 200 && !ok) strcpy(detail, "Microsoft returned incomplete OAuth credentials or missing Xbox Live permission. Sign in again.");
    if (ok) maage_log_info("maage.launcher.access.oauth", "Microsoft OAuth credentials validated.");
    else if (status == 200) maage_log_verbose("maage.launcher.access.oauth", "Microsoft OAuth response rejected: invalid token type, expiry, credentials, or Xbox scope.");
    maage_auth_json_free(json);
    if (!ok && !maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
    return MAAGE_STATUS_PROPAGATE(o->trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
}
static int maage_oauth_hex(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static char *maage_oauth_decode(CURL *curl, const char *start, size_t count, int *length) {
    char *copy, *decoded;
    size_t i;
    copy = (char *) malloc(count + 1);
    if (!copy) return NULL;
    for (i = 0; i < count; ++i) {
        if (start[i] == '%' && (i + 2 >= count || !maage_oauth_hex(start[i + 1]) || !maage_oauth_hex(start[i + 2]))) {
            maage_crypto_clear(copy, count + 1);
            free(copy);
            return NULL;
        }
        copy[i] = start[i] == '+' ? ' ' : start[i];
    }
    copy[count] = 0;
    decoded = curl_easy_unescape(curl, copy, (int) count, length);
    maage_crypto_clear(copy, count + 1);
    free(copy);
    return decoded;
}
/* HTTP method/path/Host have already been checked. Duplicate security fields,
 * token responses, embedded NUL/control bytes and ambiguous query strings fail. */
static maage_status maage_oauth_callback(const char *query, const char *state, char *code, size_t capacity, const char **reason) {
    const char *end, *equals;
    CURL *curl;
    char *key, *value;
    int keysize, valuesize, found_state = 0, found_code = 0, found_error = 0, ok = 1, i;
    *reason = "malformed or ambiguous callback parameters";
    curl = curl_easy_init();
    if (!curl) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_PARSE, 0);
    while (*query && ok) {
        end = strchr(query, '&');
        if (!end) end = query + strlen(query);
        equals = (const char *) memchr(query, '=', (size_t) (end - query));
        if (!equals || equals == query) {
            ok = 0;
            break;
        }
        /* Microsoft percent-encodes query components. A raw plus is a space. */
        key = maage_oauth_decode(curl, query, (size_t) (equals - query), &keysize);
        value = maage_oauth_decode(curl, equals + 1, (size_t) (end - equals - 1), &valuesize);
        if (!key || !value) ok = 0;
        else {
            for (i = 0; i < keysize; ++i)
                if ((unsigned char) key[i] <= 32 || (unsigned char) key[i] >= 127) ok = 0;
            if (!strcmp(key, "state") || !strcmp(key, "code") || !strcmp(key, "error"))
                for (i = 0; i < valuesize; ++i)
                    if ((unsigned char) value[i] <= 32 || (unsigned char) value[i] >= 127) ok = 0;
            if (!strcmp(key, "state")) {
                if (found_state++) {
                    *reason = "duplicate OAuth state";
                    ok = 0;
                } else if (strlen(state) != (size_t) valuesize || !maage_crypto_equal(state, value, (size_t) valuesize)) {
                    *reason = "OAuth state mismatch";
                    ok = 0;
                }
            } else if (!strcmp(key, "code")) {
                if (found_code++ || !valuesize || (size_t) valuesize >= capacity) ok = 0;
                else
                    memcpy(code, value, (size_t) valuesize + 1);
            } else if (!strcmp(key, "error")) {
                if (found_error++ || !valuesize) ok = 0;
                else
                    *reason = maage_auth_error_name(value);
            } else if (!strcmp(key, "access_token") || !strcmp(key, "id_token") || !strcmp(key, "refresh_token")) ok = 0;
        }
        if (key) curl_free(key);
        if (value) {
            maage_crypto_clear(value, (size_t) valuesize);
            curl_free(value);
        }
        query = *end ? end + 1 : end;
    }
    curl_easy_cleanup(curl);
    if (ok && !found_state) *reason = "missing OAuth state";
    else if (ok && found_code + found_error != 1) *reason = "missing or conflicting OAuth result";
    return maage_status_make(ok && found_state == 1 && found_code + found_error == 1 ? (found_error ? MAAGE_STATUS_ACCESS_DENIED : MAAGE_STATUS_SUCCESS) : MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_PARSE, 0);
}
static maage_status maage_oauth_browser_login(maage_microsoft_session *ms, maage_profile_session *s, const maage_auth_observer *o, char detail[256], int *fallback) {
    unsigned char random[32], digest[32];
    char verifier[65], challenge[48], state[65], redirect[64], url[1024], *query = NULL, *code = NULL, *encoded_redirect = NULL;
    unsigned port, i, rejected = 0;
    const char *reason = "callback unavailable", *setup = "allocate browser sign-in buffers";
    int callback = 0, received, transport_failed = 0;
    double deadline;
    CURL *encoder = NULL;
    maage_auth_listener *listener = NULL;
    maage_status outcome = maage_auth_listen(&port, &listener, o->trace), received_status, callback_result;
    *fallback = 0;
    if (maage_status_failed(outcome)) {
        *fallback = 1;
        strcpy(detail, "Cannot open the local Microsoft callback listener. Try device-code sign-in.");
        return MAAGE_STATUS_PROPAGATE(o->trace, outcome, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
    }
    if (maage_time_monotonic_seconds() < 0) {
        maage_auth_close(listener);
        strcpy(detail, "Cannot start a timed sign-in operation.");
        return maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WAIT, 0);
    }
    outcome = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
    query = (char *) calloc(1, 32769);
    code = (char *) calloc(1, 16385);
    if (!query || !code) goto done;
    setup = "generate cryptographic randomness";
    if (maage_status_failed(outcome = maage_auth_random(random, sizeof(random)))) goto done;
    for (i = 0; i < sizeof(random); ++i)
        sprintf(verifier + i * 2, "%02x", random[i]);
    setup = "derive PKCE S256 challenge";
    outcome = maage_crypto_digest(MAAGE_CRYPTO_SHA256, verifier, 64, digest, sizeof(digest));
    if (maage_status_failed(outcome)) goto done;
    outcome = maage_crypto_base64url(digest, sizeof(digest), challenge, sizeof(challenge));
    if (maage_status_failed(outcome)) goto done;
    setup = "generate cryptographic randomness";
    if (maage_status_failed(outcome = maage_auth_random(random, sizeof(random)))) goto done;
    for (i = 0; i < sizeof(random); ++i)
        sprintf(state + i * 2, "%02x", random[i]);
    /* Match the registered pathless URI, adding only the native-client port.
     * Microsoft/browser normalization supplies GET / for the HTTP callback;
     * keep this original, unnormalized value for the code exchange as well. */
    sprintf(redirect, MAAGE_MICROSOFT_REDIRECT ":%u", port);
    setup = "encode loopback redirect URI";
    encoder = curl_easy_init();
    if (!encoder || !(encoded_redirect = curl_easy_escape(encoder, redirect, 0))) {
        outcome = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_FORMAT, 0);
        goto done;
    }
    sprintf(url, MAAGE_MICROSOFT_AUTHORITY "authorize?client_id=%s&response_type=code&response_mode=query&redirect_uri=%s&scope=XboxLive.signin%%20offline_access&code_challenge_method=S256&code_challenge=%s&state=%s&prompt=select_account", MAAGE_MICROSOFT_CLIENT_ID, encoded_redirect, challenge, state);
    setup = NULL;
    /* Configuration only: the full authorization URL contains state/PKCE data. */
    maage_log_info("maage.launcher.access.oauth", "Browser authorization configuration: endpoint=%s; client_id=%s; registered_redirect_uri=%s; redirect_uri=%s.", MAAGE_MICROSOFT_AUTHORITY "authorize", MAAGE_MICROSOFT_CLIENT_ID, MAAGE_MICROSOFT_REDIRECT, redirect);
    maage_log_info("maage.launcher.access.oauth", "Opening system-browser authorization (response_type=code; response_mode=query; scope=%s; PKCE=S256).", MAAGE_MICROSOFT_SCOPE);
    o->status(o->context, "Opening Microsoft sign-in in your system browser...");
    deadline = maage_time_monotonic_seconds() + 300;
    if (maage_status_failed(outcome = maage_auth_browser(url))) {
        maage_log_verbose("maage.launcher.access.oauth", "System browser launch failed; offering device-code fallback.");
        *fallback = 1;
        goto done;
    }
    o->status(o->context, "Waiting for Microsoft authorization in your browser. MaaGE will receive the response automatically.");
    maage_log_info("maage.launcher.access.oauth", "Waiting for localhost HTTP callback (300-second deadline).");
    while (!o->cancelled(o->context) && maage_time_monotonic_seconds() >= 0 && maage_time_monotonic_seconds() < deadline) {
        received_status = maage_auth_receive(listener, query, 32769, o);
        if (maage_status_failed(received_status)) {
            outcome = received_status;
            transport_failed = 1;
            break;
        }
        received = maage_status_code(received_status) != MAAGE_STATUS_NOOP;
        if (!received) continue;
        if (o->cancelled(o->context)) break;
        callback_result = maage_oauth_callback(query, state, code, 16385, &reason);
        callback = maage_status_code(callback_result) != MAAGE_STATUS_MALFORMED_INPUT;
        if (maage_status_code(callback_result) == MAAGE_STATUS_ALLOCATION_FAILED) {
            outcome = callback_result;
            transport_failed = 1;
            break;
        }
        if (!callback) {
            maage_crypto_clear(code, 16385);
            ++rejected;
            maage_log_write(rejected == 1 ? MAAGE_LOG_VERBOSE : MAAGE_LOG_VERBOSE, "maage.launcher.access.oauth", "Ignoring callback: %s. Attempt remains pending.", reason);
        }
        maage_crypto_clear(query, 32769);
        maage_auth_reply(listener, !callback ? 0 : maage_status_failed(callback_result) ? -1
                                                                                        : 1);
        if (callback) break; /* Invalid/state-mismatched requests never consume the attempt. */
    }
    maage_auth_close(listener);
    listener = NULL;
    if (o->cancelled(o->context)) {
        outcome = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
        strcpy(detail, "Microsoft sign-in cancelled. Retry when ready.");
    } else if (transport_failed) strcpy(detail, "The local Microsoft callback listener failed. Retry sign-in or use device code.");
    else if (callback && maage_status_failed(callback_result)) {
        outcome = maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
        strcpy(detail, "Microsoft authorization was declined or rejected. Retry sign-in or use device code.");
    } else if (!callback) {
        outcome = maage_status_make(MAAGE_STATUS_TIMEOUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
        strcpy(detail, "Browser sign-in timed out. Retry or choose device-code sign-in. Check that http://localhost is registered for MaaGE in Entra.");
    } else {
        maage_log_info("maage.launcher.access.oauth", "HTTP callback accepted; OAuth state validated and listener closed before code exchange.");
        o->status(o->context, "Completing Microsoft sign-in...");
        outcome = maage_oauth_exchange("authorization_code", code, verifier, redirect, ms, s, o, detail);
    }
done:
    if (setup) maage_log_verbose("maage.launcher.access.oauth", "Cannot %s.", setup);
    maage_auth_close(listener);
    if (encoded_redirect) curl_free(encoded_redirect);
    if (encoder) curl_easy_cleanup(encoder);
    if (query) {
        maage_crypto_clear(query, 32769);
        free(query);
    }
    if (code) {
        maage_crypto_clear(code, 16385);
        free(code);
    }
    maage_crypto_clear(url, sizeof(url));
    maage_crypto_clear(random, sizeof(random));
    maage_crypto_clear(verifier, sizeof(verifier));
    maage_crypto_clear(state, sizeof(state));
    maage_crypto_clear(digest, sizeof(digest));
    return MAAGE_STATUS_PROPAGATE(o->trace, outcome, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
}
static maage_status maage_oauth_device_login(maage_microsoft_session *ms, maage_profile_session *s, const maage_auth_observer *o, char detail[256]) {
    struct json_object *json;
    char *device, *body, user[33], url[256];
    const char *error;
    long status;
    double expires, interval, deadline, remaining;
    int ok = 0;
    maage_status result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
    device = (char *) calloc(1, MAAGE_ACCESS_TOKEN_CAPACITY);
    if (!device) return MAAGE_STATUS_TRACE(o->trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
    maage_log_info("maage.launcher.access.oauth", "Requesting Microsoft device authorization.");
    o->status(o->context, "Requesting Microsoft device-code sign-in...");
    result = maage_auth_request(MAAGE_MICROSOFT_AUTHORITY "devicecode", NULL, "client_id=" MAAGE_MICROSOFT_CLIENT_ID "&scope=XboxLive.signin%20offline_access", 1, o, &status, detail, &json);
    expires = maage_auth_number(json, "expires_in");
    interval = maage_auth_number(json, "interval");
    if (!json || status != 200 || expires < 1 || expires > 1800 || interval > 1800 ||
        maage_status_failed(maage_auth_token(device, MAAGE_ACCESS_TOKEN_CAPACITY, json, "device_code")) ||
        maage_status_failed(maage_auth_token(user, sizeof(user), json, "user_code")) ||
        maage_status_failed(maage_auth_token(url, sizeof(url), json, "verification_uri"))) {
        if (status == 200) maage_log_verbose("maage.launcher.access.oauth", "Device authorization response rejected: invalid/missing expiry, interval, codes, or verification URI.");
        maage_auth_json_free(json);
        goto done;
    }
    /* Only official verification pages may be opened by the device-code UI. */
    if (strcmp(url, "https://microsoft.com/devicelogin") && strcmp(url, "https://www.microsoft.com/link") && strcmp(url, "https://microsoft.com/link") && strcmp(url, "https://login.microsoftonline.com/common/oauth2/deviceauth")) {
        maage_log_verbose("maage.launcher.access.oauth", "Device authorization rejected: verification URI is not allowlisted.");
        maage_auth_json_free(json);
        strcpy(detail, "Microsoft returned an unrecognized device verification URL.");
        goto done;
    }
    if (interval < 1) interval = 5;
    if (maage_time_monotonic_seconds() < 0) {
        result = maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WAIT, 0);
        maage_log_verbose("maage.launcher.access.oauth", "Device authorization unavailable: monotonic clock failed.");
        maage_auth_json_free(json);
        strcpy(detail, "Cannot start timed device-code polling.");
        goto done;
    }
    deadline = maage_time_monotonic_seconds() + expires;
    maage_log_info("maage.launcher.access.oauth", "Device authorization ready (validity %.0f s; polling interval %.0f s).", expires, interval);
    o->device(o->context, url, user, deadline);
    o->status(o->context, "Open Microsoft's verification URL and enter the code. Waiting for authorization...");
    maage_auth_json_free(json);
    for (;;) {
        remaining = deadline - maage_time_monotonic_seconds();
        if (remaining <= 0) {
            result = maage_status_make(MAAGE_STATUS_TIMEOUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
            maage_log_verbose("maage.launcher.access.oauth", "Device authorization expired before completion.");
            strcpy(detail, "The device code expired. Start sign-in again.");
            break;
        }
        if (maage_status_failed(result = maage_auth_wait(interval < remaining ? interval : remaining, o))) {
            maage_log_info("maage.launcher.access.oauth", "Device polling stopped (cancellation or clock failure).");
            strcpy(detail, "Device-code sign-in cancelled.");
            break;
        }
        if (maage_time_monotonic_seconds() >= deadline) continue;
        body = maage_oauth_form("urn:ietf:params:oauth:grant-type:device_code", device, NULL, NULL);
        if (!body) {
            maage_log_verbose("maage.launcher.access.oauth", "Cannot allocate device polling request.");
            break;
        }
        result = maage_auth_request(MAAGE_MICROSOFT_AUTHORITY "token", NULL, body, 1, o, &status, detail, &json);
        maage_crypto_clear(body, strlen(body));
        free(body);
        if (status == 200 && json) {
            result = maage_oauth_tokens(json, ms, s);
            ok = !maage_status_failed(result);
            if (ok) maage_log_info("maage.launcher.access.oauth", "Device authorization completed; Microsoft credentials validated.");
            else {
                maage_log_verbose("maage.launcher.access.oauth", "Device token response failed credential/scope validation.");
                strcpy(detail, "Microsoft returned incomplete device-code credentials.");
            }
            maage_auth_json_free(json);
            break;
        }
        error = maage_access_json_string(json, "error");
        if (!strcmp(error, "slow_down")) {
            interval += 5;
            maage_log_info("maage.launcher.access.oauth", "Device polling slowed by service (next interval %.0f s).", interval);
        } else if (!strcmp(error, "authorization_pending")) { /* Keep server interval. */
        } else if (!status || status == 429 || status >= 500) {
            if (interval < 60) interval *= 2;
            maage_log_verbose("maage.launcher.access.oauth", "Transient device polling failure; backing off to %.0f s.", interval);
        } else {
            maage_log_verbose("maage.launcher.access.oauth", "Device authorization stopped: %s (HTTP %ld).", maage_auth_error_name(error), status);
            maage_auth_json_free(json);
            break;
        }
        maage_auth_json_free(json);
    }
done:
    o->device(o->context, "", "", 0);
    maage_crypto_clear(device, MAAGE_ACCESS_TOKEN_CAPACITY);
    free(device);
    if (o->cancelled(o->context)) result = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
    else if (ok) result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_AUTHENTICATE, 0);
    else if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
    return MAAGE_STATUS_PROPAGATE(o->trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
}
static double maage_xbox_timestamp(const char *text) {
    int year, month, day, hour, minute, second, y, m, days, limit;
    const char *tail;
    static const int months[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (strlen(text) < 20 || sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) != 6 || year < 1970 || year > 2100 || month < 1 || month > 12 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return -1;
    limit = months[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (day < 1 || day > limit) return -1;
    tail = text + 19;
    if (*tail == '.') {
        ++tail;
        if (*tail < '0' || *tail > '9') return -1;
        while (*tail >= '0' && *tail <= '9')
            ++tail;
    }
    if (strcmp(tail, "Z")) return -1;
    days = day - 1;
    for (y = 1970; y < year; ++y)
        days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    for (m = 1; m < month; ++m)
        days += months[m - 1] + (m == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    return (double) days * 86400 + hour * 3600 + minute * 60 + second;
}
static maage_status maage_xbox_parse(struct json_object *json, maage_xbox_session *session) {
    struct json_object *claims, *users, *user;
    const char *hash;
    size_t i, length;
    double issued = maage_xbox_timestamp(maage_access_json_string(json, "IssueInstant"));
    double until = maage_xbox_timestamp(maage_access_json_string(json, "NotAfter"));
    if (issued < 0 || issued > maage_auth_wall() + 300 || until <= issued || until <= maage_auth_wall() || maage_status_failed(maage_auth_token(session->token, sizeof(session->token), json, "Token")) ||
        !json_object_object_get_ex(json, "DisplayClaims", &claims) || !json_object_is_type(claims, json_type_object) ||
        !json_object_object_get_ex(claims, "xui", &users) || !json_object_is_type(users, json_type_array) || json_object_array_length(users) != 1) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_PARSE, 0);
    user = json_object_array_get_idx(users, 0);
    hash = maage_access_json_string(user, "uhs");
    length = strlen(hash);
    if (!length || length >= sizeof(session->hash)) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_PARSE, 0);
    for (i = 0; i < length; ++i)
        if (hash[i] < '0' || hash[i] > '9') return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_PARSE, 0);
    strcpy(session->hash, hash);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PARSE, 0);
}
static maage_status maage_auth_post_json(const char *endpoint, struct json_object *body, const maage_auth_observer *o, long *status, char detail[256], struct json_object **response) {
    const char *serialized = body ? json_object_to_json_string_ext(body, JSON_C_TO_STRING_PLAIN) : NULL;
    *response = NULL;
    if (!serialized) {
        *status = 0;
        return MAAGE_STATUS_TRACE(o->trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WRITE, 0), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WRITE);
    }
    return maage_auth_request(endpoint, NULL, serialized, 0, o, status, detail, response);
}
static maage_status maage_oauth_minecraft(maage_microsoft_session *ms, maage_profile_session *s, const maage_auth_observer *o, char detail[256]) {
    maage_xbox_session *live = NULL, *xsts = NULL;
    struct json_object *body = NULL, *properties, *tokens, *json = NULL;
    char *ticket = NULL;
    long status;
    double expires;
    int ok = 0;
    maage_access_state verification_state;
    maage_status result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
    const char *stage = "allocate Xbox/Minecraft exchange session";
    live = (maage_xbox_session *) calloc(1, sizeof(*live));
    xsts = (maage_xbox_session *) calloc(1, sizeof(*xsts));
    ticket = (char *) malloc(MAAGE_ACCESS_TOKEN_CAPACITY + 128);
    if (!live || !xsts || !ticket) goto done;
    stage = "Xbox Live authentication";
    maage_log_info("maage.launcher.access.oauth", "Starting %s.", stage);
    o->status(o->context, "Authenticating with Xbox Live...");
    /* Identity-platform v2 access tokens use the documented d= RPS prefix. */
    strcpy(ticket, "d=");
    strcat(ticket, ms->access);
    body = json_object_new_object();
    properties = json_object_new_object();
    if (!body || !properties) {
        if (properties) json_object_put(properties);
        goto done;
    }
    json_object_object_add(body, "Properties", properties);
    json_object_object_add(properties, "AuthMethod", json_object_new_string("RPS"));
    json_object_object_add(properties, "SiteName", json_object_new_string("user.auth.xboxlive.com"));
    json_object_object_add(properties, "RpsTicket", json_object_new_string(ticket));
    json_object_object_add(body, "RelyingParty", json_object_new_string("http://auth.xboxlive.com"));
    json_object_object_add(body, "TokenType", json_object_new_string("JWT"));
    result = maage_auth_post_json("https://user.auth.xboxlive.com/user/authenticate", body, o, &status, detail, &json);
    maage_auth_json_free(body);
    body = NULL;
    maage_crypto_clear(ticket, MAAGE_ACCESS_TOKEN_CAPACITY + 128);
    maage_crypto_clear(ms, sizeof(*ms));
    if (!json || status != 200) goto done;
    if (maage_status_failed(result = maage_xbox_parse(json, live))) {
        maage_log_verbose("maage.launcher.access.oauth", "Xbox Live returned incomplete token or user-hash information.");
        strcpy(detail, "Xbox Live returned incomplete token or user-hash information.");
        goto done;
    }
    maage_auth_json_free(json);
    json = NULL;
    maage_log_info("maage.launcher.access.oauth", "Xbox Live response validated.");
    stage = "XSTS authorization";
    maage_log_info("maage.launcher.access.oauth", "Starting %s.", stage);
    o->status(o->context, "Authorizing Xbox access with XSTS...");
    body = json_object_new_object();
    properties = json_object_new_object();
    tokens = json_object_new_array();
    if (!body || !properties || !tokens) {
        if (properties) json_object_put(properties);
        if (tokens) json_object_put(tokens);
        goto done;
    }
    json_object_object_add(body, "Properties", properties);
    json_object_object_add(properties, "UserTokens", tokens);
    json_object_array_add(tokens, json_object_new_string(live->token));
    json_object_object_add(properties, "SandboxId", json_object_new_string("RETAIL"));
    json_object_object_add(body, "RelyingParty", json_object_new_string("rp://api.minecraftservices.com/"));
    json_object_object_add(body, "TokenType", json_object_new_string("JWT"));
    result = maage_auth_post_json("https://xsts.auth.xboxlive.com/xsts/authorize", body, o, &status, detail, &json);
    maage_auth_json_free(body);
    body = NULL;
    if (!json || status != 200) goto done;
    if (maage_status_failed(result = maage_xbox_parse(json, xsts)) || strcmp(xsts->hash, live->hash)) {
        maage_log_verbose("maage.launcher.access.oauth", "XSTS token or user hash is invalid or does not match Xbox Live.");
        strcpy(detail, "XSTS token or user hash is invalid or does not match Xbox Live.");
        goto done;
    }
    maage_crypto_clear(live, sizeof(*live));
    maage_auth_json_free(json);
    json = NULL;
    maage_log_info("maage.launcher.access.oauth", "XSTS response validated; Xbox user hashes match.");
    stage = "Minecraft Services authentication";
    maage_log_info("maage.launcher.access.oauth", "Starting %s.", stage);
    o->status(o->context, "Authenticating with Minecraft Services...");
    sprintf(ticket, "XBL3.0 x=%s;%s", xsts->hash, xsts->token);
    body = json_object_new_object();
    if (!body) goto done;
    json_object_object_add(body, "identityToken", json_object_new_string(ticket));
    result = maage_auth_post_json("https://api.minecraftservices.com/authentication/login_with_xbox", body, o, &status, detail, &json);
    maage_auth_json_free(body);
    body = NULL;
    maage_crypto_clear(ticket, MAAGE_ACCESS_TOKEN_CAPACITY + 128);
    maage_crypto_clear(xsts, sizeof(*xsts));
    if (!json || status != 200) goto done;
    expires = maage_auth_number(json, "expires_in");
    if (expires < 1 || expires > 86400 || maage_status_failed(maage_auth_token(s->minecraft_token, sizeof(s->minecraft_token), json, "access_token")) || strcmp(maage_access_json_string(json, "token_type"), "Bearer")) {
        maage_log_verbose("maage.launcher.access.oauth", "Minecraft Services returned incomplete session credentials.");
        strcpy(detail, "Minecraft Services returned incomplete session credentials.");
        goto done;
    }
    s->expires_at = maage_auth_wall() + expires;
    maage_auth_json_free(json);
    json = NULL;
    maage_log_info("maage.launcher.access.oauth", "Minecraft session response validated.");
    stage = "Minecraft Java profile and entitlement verification";
    o->status(o->context, "Verifying Minecraft Java Edition profile and entitlement...");
    result = maage_access_verify(s->minecraft_token, s->uuid, s->name, detail, o->cancelled, o->context, &verification_state, &s->appearance, &s->skin_resolved, o->trace);
    ok = !maage_status_failed(result) && verification_state == MAAGE_ACCESS_VERIFIED;
    if (ok) {
        s->verified_at = maage_auth_wall();
        s->verified_until = s->verified_at + 3600;
        if (s->verified_until > s->expires_at - 60) s->verified_until = s->expires_at - 60;
        ok = s->verified_until > s->verified_at && !o->cancelled(o->context);
    }
done:
    if (!ok && !o->cancelled(o->context)) maage_log_verbose("maage.launcher.access.oauth", "Authorization pipeline stopped during %s; no new access lease granted.", stage);
    maage_auth_json_free(json);
    maage_auth_json_free(body);
    if (ticket) {
        maage_crypto_clear(ticket, MAAGE_ACCESS_TOKEN_CAPACITY + 128);
        free(ticket);
    }
    if (live) {
        maage_crypto_clear(live, sizeof(*live));
        free(live);
    }
    if (xsts) {
        maage_crypto_clear(xsts, sizeof(*xsts));
        free(xsts);
    }
    if (o->cancelled(o->context)) result = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
    else if (!ok && !maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0);
    return MAAGE_STATUS_PROPAGATE(o->trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
}
maage_status maage_oauth_login(maage_profile_session *s, int device, const maage_auth_observer *o, char detail[256]) {
    maage_microsoft_session *ms = (maage_microsoft_session *) calloc(1, sizeof(*ms));
    maage_status result;
    int fallback = 0;
    if (!ms) return MAAGE_STATUS_TRACE(o->trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
    strcpy(detail, "Microsoft sign-in could not be initialized. Retry or use device-code sign-in.");
    result = device ? maage_oauth_device_login(ms, s, o, detail) : maage_oauth_browser_login(ms, s, o, detail, &fallback);
    if (!device && fallback) {
        maage_log_info("maage.launcher.access.oauth", "Browser callback flow unavailable; switching to device authorization.");
        result = maage_oauth_device_login(ms, s, o, detail);
        maage_status_trace_recover(o->trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, __FILE__, (unsigned long) __LINE__);
    }
    if (!maage_status_failed(result)) result = maage_oauth_minecraft(ms, s, o, detail);
    maage_crypto_clear(ms, sizeof(*ms));
    free(ms);
    return MAAGE_STATUS_PROPAGATE(o->trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
}
maage_status maage_oauth_refresh(const char *root, maage_profile_session *s, const maage_auth_observer *o, char detail[256]) {
    maage_microsoft_session *ms = (maage_microsoft_session *) calloc(1, sizeof(*ms));
    maage_status result;
    if (!ms) return MAAGE_STATUS_TRACE(o->trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE, 0), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
    maage_log_info("maage.launcher.access.oauth", "Refreshing saved Microsoft authorization.");
    o->status(o->context, "Refreshing the saved Microsoft session securely...");
    s->verified_at = s->verified_until = 0;
    /* A rejected refresh must not resurrect a formerly valid disk lease after
     * restart. Persist revocation before making the refresh request. */
    result = maage_profile_store_save(root, s, o->trace);
    if (maage_status_failed(result)) {
        maage_log_verbose("maage.launcher.access.oauth", "Cannot persist lease invalidation before refresh; request not started.");
        strcpy(detail, "Cannot update the encrypted account lease. Check MaaGE data-folder permissions and retry.");
        maage_crypto_clear(ms, sizeof(*ms));
        free(ms);
        return MAAGE_STATUS_PROPAGATE(o->trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
    }
    result = maage_oauth_exchange("refresh_token", s->microsoft_refresh, NULL, NULL, ms, s, o, detail);
    /* Preserve rotated refresh credentials before any Xbox/Minecraft request.
     * An interrupted downstream exchange must not strand the saved account. */
    if (!maage_status_failed(result)) {
        result = maage_profile_store_save(root, s, o->trace);
        if (maage_status_failed(result)) strcpy(detail, "Cannot securely replace the refreshed Microsoft credential. Check MaaGE data-folder permissions and retry.");
    }
    if (!maage_status_failed(result)) {
        maage_log_info("maage.launcher.access.oauth", "Refreshed Microsoft credential persisted before downstream exchanges.");
        result = maage_oauth_minecraft(ms, s, o, detail);
    }
    maage_crypto_clear(ms, sizeof(*ms));
    free(ms);
    return MAAGE_STATUS_PROPAGATE(o->trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_AUTHENTICATE);
}
