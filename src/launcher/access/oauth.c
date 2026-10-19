#include "oauth.h"
#include "verify.h"
#include "maage/time.h"
#include <openssl/evp.h>
#include <openssl/crypto.h>
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
static double maage_auth_wall(void) { return difftime(time(NULL), (time_t)0); }
static char *maage_oauth_form(const char *grant, const char *credential, const char *verifier)
{
    CURL *curl = curl_easy_init();
    char *escaped = NULL, *body = NULL;
    size_t capacity;
    if (!curl) return NULL;
    escaped = curl_easy_escape(curl, credential, 0);
    if (escaped) {
        capacity = strlen(escaped) + 1024; body = (char *)malloc(capacity);
        if (body) {
            sprintf(body, "client_id=%s&scope=XboxLive.signin%%20offline_access&grant_type=%s&%s=%s", MAAGE_MICROSOFT_CLIENT_ID, grant,
                !strcmp(grant, "authorization_code") ? "code" : !strcmp(grant, "refresh_token") ? "refresh_token" : "device_code", escaped);
            if (verifier) { strcat(body, "&redirect_uri=maage%3A%2F%2Fauth&code_verifier="); strcat(body, verifier); }
        }
        OPENSSL_cleanse(escaped, strlen(escaped)); curl_free(escaped);
    }
    curl_easy_cleanup(curl); return body;
}
static int maage_oauth_scope(const char *scopes)
{
    const char *end;
    size_t length;
    /* RFC 6749 permits omission when granted scopes equal those requested. */
    if (!*scopes) return 1;
    while (*scopes) {
        end = strchr(scopes, ' '); if (!end) end = scopes + strlen(scopes);
        length = (size_t)(end - scopes);
        if (length == sizeof("XboxLive.signin") - 1 && !memcmp(scopes, "XboxLive.signin", length)) return 1;
        scopes = *end ? end + 1 : end;
    }
    return 0;
}
static int maage_oauth_tokens(struct json_object *json, maage_microsoft_session *ms, maage_profile_session *s)
{
    double expires = maage_auth_number(json, "expires_in");
    const char *refresh = maage_access_json_string(json, "refresh_token");
    const char *scope = maage_access_json_string(json, "scope");
    if ((strcmp(maage_access_json_string(json, "token_type"), "Bearer") && strcmp(maage_access_json_string(json, "token_type"), "bearer")) || expires < 1 || expires > 86400 ||
        !maage_oauth_scope(scope) || !maage_auth_token(ms->access, sizeof(ms->access), json, "access_token")) return 0;
    if (*refresh && !maage_auth_token(s->microsoft_refresh, sizeof(s->microsoft_refresh), json, "refresh_token")) return 0;
    if (!*s->microsoft_refresh) return 0;
    ms->expires_at = maage_auth_wall() + expires; return 1;
}
static int maage_oauth_exchange(const char *grant, const char *credential, const char *verifier,
    maage_microsoft_session *ms, maage_profile_session *s, const maage_auth_observer *o, char detail[256])
{
    char *body = maage_oauth_form(grant, credential, verifier);
    struct json_object *json;
    long status;
    int ok;
    if (!body) { strcpy(detail, "Cannot allocate the OAuth request."); return 0; }
    json = maage_auth_request(MAAGE_MICROSOFT_AUTHORITY "token", NULL, body, 1, o, &status, detail);
    OPENSSL_cleanse(body, strlen(body)); free(body);
    ok = status == 200 && json && maage_oauth_tokens(json, ms, s);
    if (status == 200 && !ok) strcpy(detail, "Microsoft returned incomplete OAuth credentials or missing Xbox Live permission. Sign in again.");
    maage_auth_json_free(json); return ok;
}
static int maage_oauth_hex(int c)
{ return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static char *maage_oauth_decode(CURL *curl, const char *start, size_t count, int *length)
{
    char *copy, *decoded;
    size_t i;
    copy = (char *)malloc(count + 1); if (!copy) return NULL;
    for (i = 0; i < count; ++i) {
        if (start[i] == '%' && (i + 2 >= count || !maage_oauth_hex(start[i + 1]) || !maage_oauth_hex(start[i + 2]))) {
            OPENSSL_cleanse(copy, count + 1); free(copy); return NULL;
        }
        copy[i] = start[i] == '+' ? ' ' : start[i];
    }
    copy[count] = 0; decoded = curl_easy_unescape(curl, copy, (int)count, length);
    OPENSSL_cleanse(copy, count + 1); free(copy); return decoded;
}
/* Parses only an exact registered callback. Duplicate security fields, fragments,
 * token responses, embedded NUL/control bytes and ambiguous query strings fail. */
static int maage_oauth_callback(const char *uri, const char *state, char *code, size_t capacity)
{
    const char *query, *end, *equals;
    CURL *curl;
    char *key, *value;
    int keysize, valuesize, found_state = 0, found_code = 0, found_error = 0, ok = 1, i;
    if (strncmp(uri, MAAGE_MICROSOFT_REDIRECT "?", sizeof(MAAGE_MICROSOFT_REDIRECT)) || strchr(uri, '#')) return 0;
    query = uri + sizeof(MAAGE_MICROSOFT_REDIRECT); curl = curl_easy_init(); if (!curl) return 0;
    while (*query && ok) {
        end = strchr(query, '&'); if (!end) end = query + strlen(query);
        equals = (const char *)memchr(query, '=', (size_t)(end - query));
        if (!equals || equals == query) { ok = 0; break; }
        /* Microsoft percent-encodes query components. A raw plus is a space. */
        key = maage_oauth_decode(curl, query, (size_t)(equals - query), &keysize);
        value = maage_oauth_decode(curl, equals + 1, (size_t)(end - equals - 1), &valuesize);
        if (!key || !value) ok = 0;
        else {
            for (i = 0; i < keysize; ++i) if ((unsigned char)key[i] <= 32 || (unsigned char)key[i] >= 127) ok = 0;
            if (!strcmp(key, "state") || !strcmp(key, "code") || !strcmp(key, "error"))
                for (i = 0; i < valuesize; ++i) if ((unsigned char)value[i] <= 32 || (unsigned char)value[i] >= 127) ok = 0;
            if (!strcmp(key, "state")) {
                if (found_state++ || strlen(state) != (size_t)valuesize || CRYPTO_memcmp(state, value, (size_t)valuesize)) ok = 0;
            } else if (!strcmp(key, "code")) {
                if (found_code++ || !valuesize || (size_t)valuesize >= capacity) ok = 0;
                else memcpy(code, value, (size_t)valuesize + 1);
            } else if (!strcmp(key, "error")) { if (found_error++ || !valuesize) ok = 0; }
            else if (!strcmp(key, "access_token") || !strcmp(key, "id_token") || !strcmp(key, "refresh_token")) ok = 0;
        }
        if (key) curl_free(key);
        if (value) { OPENSSL_cleanse(value, (size_t)valuesize); curl_free(value); }
        query = *end ? end + 1 : end;
    }
    curl_easy_cleanup(curl);
    return ok && found_state == 1 && found_code + found_error == 1 ? (found_error ? -1 : 1) : 0;
}
static int maage_oauth_browser_login(maage_microsoft_session *ms, maage_profile_session *s,
    const maage_auth_observer *o, char detail[256])
{
    unsigned char random[32], digest[32];
    unsigned int digest_size;
    char verifier[65], challenge[48], state[80], url[1024], *uri, *code;
    unsigned port, i;
    int callback = 0, result = 0;
    double deadline;
    maage_auth_listener *listener = maage_auth_listen(&port);
    if (!listener) return -1;
    if (maage_time_monotonic_seconds() < 0) { maage_auth_close(listener); strcpy(detail, "Cannot start a timed sign-in operation."); return 0; }
    uri = (char *)calloc(1, 32769); code = (char *)calloc(1, 16385);
    if (!uri || !code) goto done;
    if (!maage_auth_random(random, sizeof(random))) goto done;
    for (i = 0; i < sizeof(random); ++i) sprintf(verifier + i * 2, "%02x", random[i]);
    if (EVP_Digest(verifier, 64, digest, &digest_size, EVP_sha256(), NULL) != 1 || digest_size != 32) goto done;
    EVP_EncodeBlock((unsigned char *)challenge, digest, 32);
    for (i = 0; challenge[i]; ++i) { if (challenge[i] == '+') challenge[i] = '-'; else if (challenge[i] == '/') challenge[i] = '_'; else if (challenge[i] == '=') { challenge[i] = 0; break; } }
    if (!maage_auth_random(random, sizeof(random))) goto done;
    sprintf(state, "%u.", port);
    for (i = 0; i < sizeof(random); ++i) sprintf(state + strlen(state), "%02x", random[i]);
    sprintf(url, MAAGE_MICROSOFT_AUTHORITY "authorize?client_id=%s&response_type=code&response_mode=query&redirect_uri=maage%%3A%%2F%%2Fauth&scope=XboxLive.signin%%20offline_access&code_challenge_method=S256&code_challenge=%s&state=%s&prompt=select_account", MAAGE_MICROSOFT_CLIENT_ID, challenge, state);
    o->status(o->context, "Opening Microsoft sign-in in your system browser...");
    if (!maage_auth_browser(url)) { result = -1; goto done; }
    o->status(o->context, "Waiting for browser authorization. Cancel sign-in to retry, or use device-code sign-in if the redirect is rejected.");
    deadline = maage_time_monotonic_seconds() + 300;
    while (!o->cancelled(o->context) && maage_time_monotonic_seconds() >= 0 && maage_time_monotonic_seconds() < deadline) {
        if (!maage_auth_receive(listener, uri, 32769)) continue;
        callback = maage_oauth_callback(uri, state, code, 16385);
        OPENSSL_cleanse(uri, 32769);
        if (callback) break; /* Invalid/stale callbacks never consume the attempt. */
    }
    maage_auth_close(listener); listener = NULL; /* Duplicate callbacks now fail. */
    if (o->cancelled(o->context)) strcpy(detail, "Microsoft sign-in cancelled. Retry when ready.");
    else if (callback < 0) strcpy(detail, "Microsoft authorization was declined or rejected. Retry, or use device-code sign-in and check Entra redirect registration.");
    else if (!callback) strcpy(detail, "Browser sign-in timed out. Retry or choose device-code sign-in. MaaGE requires the registered maage://auth redirect.");
    else result = maage_oauth_exchange("authorization_code", code, verifier, ms, s, o, detail);
done:
    maage_auth_close(listener);
    if (uri) { OPENSSL_cleanse(uri, 32769); free(uri); }
    if (code) { OPENSSL_cleanse(code, 16385); free(code); }
    OPENSSL_cleanse(random, sizeof(random)); OPENSSL_cleanse(verifier, sizeof(verifier)); OPENSSL_cleanse(state, sizeof(state)); OPENSSL_cleanse(digest, sizeof(digest));
    return result;
}
static int maage_oauth_device_login(maage_microsoft_session *ms, maage_profile_session *s,
    const maage_auth_observer *o, char detail[256])
{
    struct json_object *json;
    char *device, *body, user[33], url[256];
    const char *error;
    long status;
    double expires, interval, deadline, remaining;
    int ok = 0;
    device = (char *)calloc(1, MAAGE_ACCESS_TOKEN_CAPACITY); if (!device) return 0;
    o->status(o->context, "Requesting Microsoft device-code sign-in...");
    json = maage_auth_request(MAAGE_MICROSOFT_AUTHORITY "devicecode", NULL,
        "client_id=" MAAGE_MICROSOFT_CLIENT_ID "&scope=XboxLive.signin%20offline_access", 1, o, &status, detail);
    expires = maage_auth_number(json, "expires_in"); interval = maage_auth_number(json, "interval");
    if (!json || status != 200 || expires < 1 || expires > 1800 || interval > 1800 ||
        !maage_auth_token(device, MAAGE_ACCESS_TOKEN_CAPACITY, json, "device_code") ||
        !maage_auth_token(user, sizeof(user), json, "user_code") ||
        !maage_auth_token(url, sizeof(url), json, "verification_uri")) { maage_auth_json_free(json); goto done; }
    /* Only official verification pages may be opened by the device-code UI. */
    if (strcmp(url, "https://microsoft.com/devicelogin") && strcmp(url, "https://www.microsoft.com/link") &&
        strcmp(url, "https://microsoft.com/link") && strcmp(url, "https://login.microsoftonline.com/common/oauth2/deviceauth")) {
        maage_auth_json_free(json); strcpy(detail, "Microsoft returned an unrecognized device verification URL."); goto done;
    }
    if (interval < 1) interval = 5;
    if (maage_time_monotonic_seconds() < 0) { maage_auth_json_free(json); strcpy(detail, "Cannot start timed device-code polling."); goto done; }
    deadline = maage_time_monotonic_seconds() + expires;
    o->device(o->context, url, user, deadline);
    o->status(o->context, "Open Microsoft's verification URL and enter the code. Waiting for authorization...");
    maage_auth_json_free(json);
    for (;;) {
        remaining = deadline - maage_time_monotonic_seconds();
        if (remaining <= 0) { strcpy(detail, "The device code expired. Start sign-in again."); break; }
        if (!maage_auth_wait(interval < remaining ? interval : remaining, o)) { strcpy(detail, "Device-code sign-in cancelled."); break; }
        if (maage_time_monotonic_seconds() >= deadline) continue;
        body = maage_oauth_form("urn:ietf:params:oauth:grant-type:device_code", device, NULL); if (!body) break;
        json = maage_auth_request(MAAGE_MICROSOFT_AUTHORITY "token", NULL, body, 1, o, &status, detail);
        OPENSSL_cleanse(body, strlen(body)); free(body);
        if (status == 200 && json) {
            ok = maage_oauth_tokens(json, ms, s);
            if (!ok) strcpy(detail, "Microsoft returned incomplete device-code credentials.");
            maage_auth_json_free(json); break;
        }
        error = maage_access_json_string(json, "error");
        if (!strcmp(error, "slow_down")) interval += 5;
        else if (!strcmp(error, "authorization_pending")) { /* Keep server interval. */ }
        else if (!status || status == 429 || status >= 500) { if (interval < 60) interval *= 2; }
        else { maage_auth_json_free(json); break; }
        maage_auth_json_free(json);
    }
done:
    o->device(o->context, "", "", 0); OPENSSL_cleanse(device, MAAGE_ACCESS_TOKEN_CAPACITY); free(device); return ok;
}
static double maage_xbox_timestamp(const char *text)
{
    int year, month, day, hour, minute, second, y, m, days, limit;
    const char *tail;
    static const int months[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (strlen(text) < 20 || sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) != 6 ||
        year < 1970 || year > 2100 || month < 1 || month > 12 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return -1;
    limit = months[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (day < 1 || day > limit) return -1;
    tail = text + 19;
    if (*tail == '.') { ++tail; if (*tail < '0' || *tail > '9') return -1; while (*tail >= '0' && *tail <= '9') ++tail; }
    if (strcmp(tail, "Z")) return -1;
    days = day - 1;
    for (y = 1970; y < year; ++y) days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    for (m = 1; m < month; ++m) days += months[m - 1] + (m == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    return (double)days * 86400 + hour * 3600 + minute * 60 + second;
}
static int maage_xbox_parse(struct json_object *json, maage_xbox_session *session)
{
    struct json_object *claims, *users, *user;
    const char *hash;
    size_t i, length;
    double issued = maage_xbox_timestamp(maage_access_json_string(json, "IssueInstant"));
    double until = maage_xbox_timestamp(maage_access_json_string(json, "NotAfter"));
    if (issued < 0 || issued > maage_auth_wall() + 300 || until <= issued || until <= maage_auth_wall() || !maage_auth_token(session->token, sizeof(session->token), json, "Token") ||
        !json_object_object_get_ex(json, "DisplayClaims", &claims) || !json_object_is_type(claims, json_type_object) ||
        !json_object_object_get_ex(claims, "xui", &users) || !json_object_is_type(users, json_type_array) || json_object_array_length(users) != 1) return 0;
    user = json_object_array_get_idx(users, 0); hash = maage_access_json_string(user, "uhs"); length = strlen(hash);
    if (!length || length >= sizeof(session->hash)) return 0;
    for (i = 0; i < length; ++i) if (hash[i] < '0' || hash[i] > '9') return 0;
    strcpy(session->hash, hash); return 1;
}
static struct json_object *maage_auth_post_json(const char *endpoint, struct json_object *body,
    const maage_auth_observer *o, long *status, char detail[256])
{
    const char *serialized = body ? json_object_to_json_string_ext(body, JSON_C_TO_STRING_PLAIN) : NULL;
    if (!serialized) { *status = 0; return NULL; }
    return maage_auth_request(endpoint, NULL, serialized, 0, o, status, detail);
}
static int maage_oauth_minecraft(maage_microsoft_session *ms, maage_profile_session *s,
    const maage_auth_observer *o, char detail[256])
{
    maage_xbox_session *live = NULL, *xsts = NULL;
    struct json_object *body = NULL, *properties, *tokens, *json = NULL;
    char *ticket = NULL;
    long status;
    double expires;
    int ok = 0;
    live = (maage_xbox_session *)calloc(1, sizeof(*live)); xsts = (maage_xbox_session *)calloc(1, sizeof(*xsts));
    ticket = (char *)malloc(MAAGE_ACCESS_TOKEN_CAPACITY + 128);
    if (!live || !xsts || !ticket) goto done;
    o->status(o->context, "Authenticating with Xbox Live...");
    /* Identity-platform v2 access tokens use the documented d= RPS prefix. */
    strcpy(ticket, "d="); strcat(ticket, ms->access);
    body = json_object_new_object(); properties = json_object_new_object();
    if (!body || !properties) { if (properties) json_object_put(properties); goto done; }
    json_object_object_add(body, "Properties", properties);
    json_object_object_add(properties, "AuthMethod", json_object_new_string("RPS"));
    json_object_object_add(properties, "SiteName", json_object_new_string("user.auth.xboxlive.com"));
    json_object_object_add(properties, "RpsTicket", json_object_new_string(ticket));
    json_object_object_add(body, "RelyingParty", json_object_new_string("http://auth.xboxlive.com"));
    json_object_object_add(body, "TokenType", json_object_new_string("JWT"));
    json = maage_auth_post_json("https://user.auth.xboxlive.com/user/authenticate", body, o, &status, detail);
    maage_auth_json_free(body); body = NULL; OPENSSL_cleanse(ticket, MAAGE_ACCESS_TOKEN_CAPACITY + 128); OPENSSL_cleanse(ms, sizeof(*ms));
    if (!json || status != 200) goto done;
    if (!maage_xbox_parse(json, live)) { strcpy(detail, "Xbox Live returned incomplete token or user-hash information."); goto done; }
    maage_auth_json_free(json); json = NULL;
    o->status(o->context, "Authorizing Xbox access with XSTS...");
    body = json_object_new_object(); properties = json_object_new_object(); tokens = json_object_new_array();
    if (!body || !properties || !tokens) { if (properties) json_object_put(properties); if (tokens) json_object_put(tokens); goto done; }
    json_object_object_add(body, "Properties", properties); json_object_object_add(properties, "UserTokens", tokens);
    json_object_array_add(tokens, json_object_new_string(live->token));
    json_object_object_add(properties, "SandboxId", json_object_new_string("RETAIL"));
    json_object_object_add(body, "RelyingParty", json_object_new_string("rp://api.minecraftservices.com/"));
    json_object_object_add(body, "TokenType", json_object_new_string("JWT"));
    json = maage_auth_post_json("https://xsts.auth.xboxlive.com/xsts/authorize", body, o, &status, detail);
    maage_auth_json_free(body); body = NULL;
    if (!json || status != 200) goto done;
    if (!maage_xbox_parse(json, xsts) || strcmp(xsts->hash, live->hash)) { strcpy(detail, "XSTS token or user hash is invalid or does not match Xbox Live."); goto done; }
    OPENSSL_cleanse(live, sizeof(*live)); maage_auth_json_free(json); json = NULL;
    o->status(o->context, "Authenticating with Minecraft Services...");
    sprintf(ticket, "XBL3.0 x=%s;%s", xsts->hash, xsts->token);
    body = json_object_new_object(); if (!body) goto done;
    json_object_object_add(body, "identityToken", json_object_new_string(ticket));
    json = maage_auth_post_json("https://api.minecraftservices.com/authentication/login_with_xbox", body, o, &status, detail);
    maage_auth_json_free(body); body = NULL; OPENSSL_cleanse(ticket, MAAGE_ACCESS_TOKEN_CAPACITY + 128); OPENSSL_cleanse(xsts, sizeof(*xsts));
    if (!json || status != 200) goto done;
    expires = maage_auth_number(json, "expires_in");
    if (expires < 1 || expires > 86400 || !maage_auth_token(s->minecraft_token, sizeof(s->minecraft_token), json, "access_token") ||
        strcmp(maage_access_json_string(json, "token_type"), "Bearer")) { strcpy(detail, "Minecraft Services returned incomplete session credentials."); goto done; }
    s->expires_at = maage_auth_wall() + expires;
    maage_auth_json_free(json); json = NULL;
    o->status(o->context, "Verifying Minecraft Java Edition profile and entitlement...");
    ok = maage_access_verify(s->minecraft_token, s->uuid, s->name, detail, o->cancelled, o->context) == MAAGE_ACCESS_VERIFIED;
    if (ok) {
        s->verified_at = maage_auth_wall(); s->verified_until = s->verified_at + 3600;
        if (s->verified_until > s->expires_at - 60) s->verified_until = s->expires_at - 60;
        ok = s->verified_until > s->verified_at && !o->cancelled(o->context);
    }
done:
    maage_auth_json_free(json); maage_auth_json_free(body);
    if (ticket) { OPENSSL_cleanse(ticket, MAAGE_ACCESS_TOKEN_CAPACITY + 128); free(ticket); }
    if (live) { OPENSSL_cleanse(live, sizeof(*live)); free(live); }
    if (xsts) { OPENSSL_cleanse(xsts, sizeof(*xsts)); free(xsts); }
    return ok;
}
int maage_oauth_login(maage_profile_session *s, int device, const maage_auth_observer *o, char detail[256])
{
    maage_microsoft_session *ms = (maage_microsoft_session *)calloc(1, sizeof(*ms));
    int ok;
    if (!ms) return 0;
    strcpy(detail, "Microsoft sign-in could not be initialized. Retry or use device-code sign-in.");
    ok = device ? maage_oauth_device_login(ms, s, o, detail) : maage_oauth_browser_login(ms, s, o, detail);
    if (ok < 0) ok = maage_oauth_device_login(ms, s, o, detail);
    if (ok) ok = maage_oauth_minecraft(ms, s, o, detail);
    OPENSSL_cleanse(ms, sizeof(*ms)); free(ms); return ok;
}
int maage_oauth_refresh(const char *root, maage_profile_session *s, const maage_auth_observer *o, char detail[256])
{
    maage_microsoft_session *ms = (maage_microsoft_session *)calloc(1, sizeof(*ms));
    int ok;
    if (!ms) return 0;
    o->status(o->context, "Refreshing the saved Microsoft session securely...");
    s->verified_at = s->verified_until = 0;
    /* A rejected refresh must not resurrect a formerly valid disk lease after
     * restart. Persist revocation before making the refresh request. */
    if (!maage_profile_store_save(root, s)) {
        strcpy(detail, "Cannot update the encrypted account lease. Check MaaGE data-folder permissions and retry.");
        OPENSSL_cleanse(ms, sizeof(*ms)); free(ms); return 0;
    }
    ok = maage_oauth_exchange("refresh_token", s->microsoft_refresh, NULL, ms, s, o, detail);
    /* Preserve rotated refresh credentials before any Xbox/Minecraft request.
     * An interrupted downstream exchange must not strand the saved account. */
    if (ok && !maage_profile_store_save(root, s)) { strcpy(detail, "Cannot securely replace the refreshed Microsoft credential. Check MaaGE data-folder permissions and retry."); ok = 0; }
    if (ok) ok = maage_oauth_minecraft(ms, s, o, detail);
    OPENSSL_cleanse(ms, sizeof(*ms)); free(ms); return ok;
}
