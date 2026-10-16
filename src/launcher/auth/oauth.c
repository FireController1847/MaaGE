#include "internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

maage_auth_error maage_auth_oauth(maage_auth *auth, maage_auth_record *record, char *access)
{
    char *credential = NULL, *form = NULL;
    const char *refresh, *kind, *failure;
    maage_auth_json *response = NULL;
    maage_auth_error result = MAAGE_AUTH_PROTOCOL;
    double expires;
    long status;
    int interactive = !record->refresh[0];
    if (interactive) {
        result = maage_auth_device(auth, &response);
        if (result != MAAGE_AUTH_OK) goto cleanup;
    } else {
        credential = maage_auth_escape(record->refresh);
        if (!credential) goto cleanup;
        form = malloc(strlen(credential) + 256);
        if (!form) goto cleanup;
        sprintf(form, "client_id=%s&grant_type=refresh_token&refresh_token=%s&scope=XboxLive.SignIn%%20XboxLive.offline_access",
            auth->config.client_id, credential);
        maage_auth_progress(auth, "Refreshing Microsoft sign-in...");
        result = maage_auth_http(auth, "Microsoft token refresh", auth->config.token, "application/x-www-form-urlencoded", form, NULL, &status, &response);
        if (result != MAAGE_AUTH_OK) goto cleanup;
        if (status != 200) {
            failure = maage_auth_json_text(response, "error");
            result = failure && (!strcmp(failure, "invalid_grant") || !strcmp(failure, "interaction_required") || !strcmp(failure, "login_required")) ?
                MAAGE_AUTH_REAUTH : failure && (!strcmp(failure, "invalid_client") || !strcmp(failure, "unauthorized_client") || !strcmp(failure, "invalid_scope")) ? MAAGE_AUTH_CONFIGURATION : MAAGE_AUTH_PROTOCOL;
            goto cleanup;
        }
    }
    expires = maage_auth_json_double(response, "expires_in");
    kind = maage_auth_json_text(response, "token_type");
    refresh = maage_auth_json_text(response, "refresh_token");
    if (!kind || (strcmp(kind, "Bearer") && strcmp(kind, "bearer")) || !(expires > 0 && expires <= 2678400) ||
        !maage_auth_copy(access, MAAGE_AUTH_TOKEN_SIZE, maage_auth_json_text(response, "access_token")) || !*access ||
        (refresh && (!*refresh || !maage_auth_copy(record->refresh, sizeof(record->refresh), refresh))) || (!refresh && interactive)) {
        strcpy(auth->response_reason, "Microsoft returned a missing or invalid access_token, refresh_token, token_type or expires_in field.");
        result = MAAGE_AUTH_PROTOCOL; goto cleanup;
    }
    record->microsoft_valid = 1;
    result = MAAGE_AUTH_OK;
cleanup:
    maage_auth_free_secret(credential); maage_auth_free_secret(form);
    maage_auth_json_put(response);
    return result;
}
