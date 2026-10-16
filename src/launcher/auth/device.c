#include "internal.h"
#include "maage/time.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int maage_auth_device_expired(maage_auth *auth)
{
    double deadline, now;
    maage_auth_lock(auth->runtime); deadline = auth->device_deadline; maage_auth_unlock(auth->runtime);
    if (!deadline) return 0;
    now = maage_time_monotonic_seconds();
    return now < 0 || now >= deadline;
}
static void maage_auth_device_browser(maage_auth *auth)
{
    char url[512];
    maage_auth_lock(auth->runtime);
    url[0] = '\0';
    if (!auth->cancelled && !auth->stop) maage_auth_copy(url, sizeof(url), auth->browser_url);
    auth->browser_url[0] = '\0';
    maage_auth_unlock(auth->runtime);
    maage_auth_browser_poll(auth->runtime);
    if (url[0] && !maage_auth_open_browser(auth->runtime, url)) {
        maage_auth_lock(auth->runtime);
        strcpy(auth->link_hint, "Browser unavailable. Open the displayed website on another device, or try again.");
        maage_auth_unlock(auth->runtime);
    }
}
static maage_auth_error maage_auth_device_wait(maage_auth *auth, double interval, double deadline)
{
    double now = maage_time_monotonic_seconds(), next = now + interval;
    if (now < 0) return MAAGE_AUTH_PROTOCOL;
    for (;;) {
        if (maage_auth_cancelled(auth)) return MAAGE_AUTH_CANCELLED;
        now = maage_time_monotonic_seconds();
        if (now < 0) return MAAGE_AUTH_PROTOCOL;
        if (now >= deadline) return MAAGE_AUTH_EXPIRED;
        maage_auth_device_browser(auth);
        if (now >= next) return MAAGE_AUTH_OK;
        maage_auth_sleep_tick();
    }
}
static int maage_auth_verification_uri(const char *uri)
{
    const char *cursor;
    if (!uri || strncmp(uri, "https://", 8) || !uri[8]) return 0;
    /* Use verification_uri, never a credential-bearing complete URI. */
    for (cursor = uri + 8; *cursor && *cursor != '/'; ++cursor)
        if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
            (*cursor >= '0' && *cursor <= '9') || *cursor == '.' || *cursor == '-')) return 0;
    if (cursor == uri + 8) return 0;
    for (; *cursor; ++cursor) if ((unsigned char)*cursor <= 32 || (unsigned char)*cursor >= 127 ||
        *cursor == '?' || *cursor == '#' || *cursor == '@' || *cursor == '\\') return 0;
    return 1;
}
static maage_auth_error maage_auth_device_error(const char *error)
{
    if (!error) return MAAGE_AUTH_PROTOCOL;
    if (!strcmp(error, "expired_token") || !strcmp(error, "code_expired")) return MAAGE_AUTH_EXPIRED;
    if (!strcmp(error, "access_denied") || !strcmp(error, "authorization_declined")) return MAAGE_AUTH_DENIED;
    if (!strcmp(error, "invalid_client") || !strcmp(error, "unauthorized_client") || !strcmp(error, "invalid_scope")) return MAAGE_AUTH_CONFIGURATION;
    if (!strcmp(error, "invalid_grant") || !strcmp(error, "bad_verification_code")) return MAAGE_AUTH_REAUTH;
    return MAAGE_AUTH_PROTOCOL;
}
maage_auth_error maage_auth_device(maage_auth *auth, maage_auth_json **tokens)
{
    maage_auth_json *response = NULL;
    char request[256], user_code[64], uri[512];
    char *credential = NULL, *form = NULL;
    const char *device, *code, *url, *failure;
    double started = maage_time_monotonic_seconds(), expires, interval, deadline = 0;
    size_t index;
    long status;
    maage_auth_error result = MAAGE_AUTH_PROTOCOL;
    *tokens = NULL;
    if (started < 0) return result;
    sprintf(request, "client_id=%s&scope=XboxLive.SignIn%%20XboxLive.offline_access", auth->config.client_id);
    maage_auth_progress(auth, "Requesting a Microsoft sign-in code...");
    result = maage_auth_http(auth, auth->config.device, "application/x-www-form-urlencoded", request, NULL, &status, &response);
    if (result != MAAGE_AUTH_OK) goto cleanup;
    if (status != 200) { result = maage_auth_device_error(maage_auth_json_text(response, "error")); goto cleanup; }
    device = maage_auth_json_text(response, "device_code"); code = maage_auth_json_text(response, "user_code");
    url = maage_auth_json_text(response, "verification_uri");
    expires = maage_auth_json_double(response, "expires_in");
    interval = maage_auth_json_get(response, "interval") ? maage_auth_json_double(response, "interval") : 5;
    result = MAAGE_AUTH_PROTOCOL;
    if (!device || !*device || strlen(device) >= MAAGE_AUTH_TOKEN_SIZE || !code || !*code || !strcmp(device, code) ||
        !maage_auth_copy(user_code, sizeof(user_code), code) || !maage_auth_verification_uri(url) ||
        !maage_auth_copy(uri, sizeof(uri), url) || strstr(uri, device) ||
        !(expires > 0 && expires <= 86400) || expires != (double)(long)expires ||
        !(interval >= 1 && interval <= 86400) || interval != (double)(long)interval) goto cleanup;
    for (index = 0; code[index]; ++index) if (!((code[index] >= 'A' && code[index] <= 'Z') || (code[index] >= 'a' && code[index] <= 'z') ||
        (code[index] >= '0' && code[index] <= '9') || code[index] == '-')) goto cleanup;
    credential = maage_auth_escape(device);
    if (!credential) goto cleanup;
    form = malloc(strlen(credential) + 256);
    if (!form) goto cleanup;
    sprintf(form, "client_id=%s&grant_type=urn:ietf:params:oauth:grant-type:device_code&device_code=%s", auth->config.client_id, credential);
    maage_auth_free_secret(credential); credential = NULL;
    maage_auth_json_put(response); response = NULL;
    deadline = started + expires;
    maage_auth_lock(auth->runtime);
    auth->device_deadline = deadline;
    strcpy(auth->user_code, user_code); strcpy(auth->verification_uri, uri); strcpy(auth->browser_url, uri);
    maage_auth_unlock(auth->runtime);
    maage_auth_progress(auth, "Waiting for you to finish signing in with Microsoft...");
    for (;;) {
        result = maage_auth_device_wait(auth, interval, deadline);
        if (result != MAAGE_AUTH_OK) break;
        result = maage_auth_http(auth, auth->config.token, "application/x-www-form-urlencoded", form, NULL, &status, &response);
        if (maage_auth_cancelled(auth)) { result = MAAGE_AUTH_CANCELLED; break; }
        if (maage_auth_device_expired(auth)) { result = MAAGE_AUTH_EXPIRED; break; }
        if (result == MAAGE_AUTH_NETWORK) {
            interval *= 2;
            maage_auth_progress(auth, "Connection interrupted. Retrying while your sign-in code is valid...");
        } else if (result != MAAGE_AUTH_OK) break;
        else if (status == 200) {
            *tokens = response; response = NULL; break;
        } else {
            failure = maage_auth_json_text(response, "error");
            if (failure && !strcmp(failure, "slow_down")) interval += 5;
            else if (!failure || strcmp(failure, "authorization_pending")) { result = maage_auth_device_error(failure); break; }
            maage_auth_progress(auth, "Waiting for you to finish signing in with Microsoft...");
        }
        maage_auth_json_put(response); response = NULL;
    }
cleanup:
    maage_auth_json_put(response); maage_auth_free_secret(credential); maage_auth_free_secret(form);
    maage_auth_lock(auth->runtime);
    auth->device_deadline = 0; auth->user_code[0] = auth->verification_uri[0] = auth->browser_url[0] = '\0';
    maage_auth_unlock(auth->runtime);
    return result;
}
