#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

maage_auth_error maage_auth_oauth(maage_auth *auth, maage_auth_record *record, char *access)
{
    unsigned char random[64], digest[32];
    char verifier[87], state[44], challenge[44], code[4096];
    char *credential = NULL, *redirect = NULL, *form = NULL;
    const char *refresh, *kind, *failure;
    maage_auth_json *response = NULL;
    maage_auth_error result = MAAGE_AUTH_PROTOCOL;
    double expires;
    long status;
    int interactive = !record->refresh[0];
    memset(code, 0, sizeof(code)); memset(verifier, 0, sizeof(verifier));
    if (interactive) {
        if (!maage_auth_random(random, sizeof(random))) goto cleanup;
        maage_auth_base64url(random, sizeof(random), verifier);
        if (!maage_auth_sha256((const unsigned char *)verifier, strlen(verifier), digest)) goto cleanup;
        maage_auth_base64url(digest, sizeof(digest), challenge);
        if (!maage_auth_random(random, 32)) goto cleanup;
        maage_auth_base64url(random, 32, state);
        maage_auth_progress(auth, "Complete Microsoft sign-in in your browser. MaaGE is waiting for the response.");
        result = maage_auth_loopback(auth, state, challenge, code, sizeof(code));
        if (result != MAAGE_AUTH_OK) goto cleanup;
    }
    credential = maage_auth_escape(interactive ? code : record->refresh);
    redirect = maage_auth_escape(auth->config.redirect);
    if (!credential || !redirect) { result = MAAGE_AUTH_PROTOCOL; goto cleanup; }
    form = malloc(strlen(credential) + strlen(redirect) + strlen(verifier) + 512);
    if (!form) { result = MAAGE_AUTH_PROTOCOL; goto cleanup; }
    sprintf(form, "client_id=%s&grant_type=%s&%s=%s&redirect_uri=%s&scope=XboxLive.SignIn%%20XboxLive.offline_access%s%s",
        auth->config.client_id, interactive ? "authorization_code" : "refresh_token", interactive ? "code" : "refresh_token", credential, redirect,
        interactive ? "&code_verifier=" : "", interactive ? verifier : "");
    maage_auth_progress(auth, interactive ? "Exchanging Microsoft authorization code..." : "Refreshing Microsoft sign-in...");
    result = maage_auth_http(auth, auth->config.token, "application/x-www-form-urlencoded", form, NULL, &status, &response);
    if (result != MAAGE_AUTH_OK) goto cleanup;
    if (status != 200) {
        failure = maage_auth_json_text(response, "error");
        result = failure && (!strcmp(failure, "invalid_grant") || !strcmp(failure, "interaction_required") || !strcmp(failure, "login_required")) ?
            MAAGE_AUTH_REAUTH : failure && (!strcmp(failure, "invalid_client") || !strcmp(failure, "unauthorized_client") || !strcmp(failure, "invalid_scope")) ? MAAGE_AUTH_CONFIGURATION : MAAGE_AUTH_PROTOCOL;
        goto cleanup;
    }
    expires = maage_auth_json_double(response, "expires_in");
    kind = maage_auth_json_text(response, "token_type");
    refresh = maage_auth_json_text(response, "refresh_token");
    if (!kind || (strcmp(kind, "Bearer") && strcmp(kind, "bearer")) || !(expires > 0 && expires <= 2678400) ||
        !maage_auth_copy(access, MAAGE_AUTH_TOKEN_SIZE, maage_auth_json_text(response, "access_token")) || !*access ||
        (refresh && (!*refresh || !maage_auth_copy(record->refresh, sizeof(record->refresh), refresh))) || (!refresh && interactive)) {
        result = MAAGE_AUTH_PROTOCOL; goto cleanup;
    }
    record->microsoft_valid = 1;
    result = MAAGE_AUTH_OK;
cleanup:
    maage_auth_wipe(random, sizeof(random)); maage_auth_wipe(digest, sizeof(digest));
    maage_auth_wipe(verifier, sizeof(verifier)); maage_auth_wipe(code, sizeof(code));
    maage_auth_free_secret(credential); maage_auth_free_secret(redirect); maage_auth_free_secret(form);
    maage_auth_json_put(response);
    return result;
}
