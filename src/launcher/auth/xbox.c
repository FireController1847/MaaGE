#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static maage_auth_json *maage_auth_xbox_request(maage_auth_json *properties, const char *party)
{
    maage_auth_json *request = maage_auth_json_object();
    maage_auth_json_add(request, "Properties", properties);
    maage_auth_json_string(request, "RelyingParty", party);
    maage_auth_json_string(request, "TokenType", "JWT");
    return request;
}
static int maage_auth_xbox_claims(maage_auth_json *response, char *token, char *hash)
{
    const char *value = maage_auth_json_text(maage_auth_json_at(maage_auth_json_get(maage_auth_json_get(response, "DisplayClaims"), "xui"), 0), "uhs");
    size_t index;
    if (!value || !*value || strlen(value) > 32) return 0;
    for (index = 0; value[index]; ++index) if (value[index] < '0' || value[index] > '9') return 0;
    return maage_auth_copy(hash, 33, value) && maage_auth_copy(token, MAAGE_AUTH_TOKEN_SIZE, maage_auth_json_text(response, "Token")) && *token;
}
maage_auth_error maage_auth_xbox(maage_auth *auth, const char *access, char *token, char *hash, char *xuid)
{
    maage_auth_json *properties, *request = NULL, *response = NULL, *tokens;
    char *body = NULL, *ticket = NULL, *user_token = NULL;
    char user_hash[33], xsts_hash[33];
    maage_auth_error result = MAAGE_AUTH_PROTOCOL;
    long status;
    int party;
    const char *identity;
    size_t index;
    xuid[0] = hash[0] = '\0';
    ticket = malloc(strlen(access) + 3); user_token = calloc(1, MAAGE_AUTH_TOKEN_SIZE);
    if (!ticket || !user_token) goto cleanup;
    strcpy(ticket, "d="); strcat(ticket, access);
    properties = maage_auth_json_object();
    maage_auth_json_string(properties, "AuthMethod", "RPS");
    maage_auth_json_string(properties, "SiteName", "user.auth.xboxlive.com");
    maage_auth_json_string(properties, "RpsTicket", ticket);
    request = maage_auth_xbox_request(properties, "http://auth.xboxlive.com");
    body = maage_auth_json_encode(request);
    if (!body) goto cleanup;
    maage_auth_progress(auth, "Signing in to Xbox Live...");
    result = maage_auth_http(auth, "Xbox Live authentication", auth->config.xbox, "application/json", body, NULL, &status, &response);
    if (result != MAAGE_AUTH_OK) goto cleanup;
    if (status != 200) { result = status == 401 ? MAAGE_AUTH_RESTRICTED : MAAGE_AUTH_CONFIGURATION; goto cleanup; }
    if (!maage_auth_xbox_claims(response, user_token, user_hash)) {
        strcpy(auth->response_reason, "Xbox Live did not return a valid token and user-hash claim.");
        result = MAAGE_AUTH_PROTOCOL; goto cleanup; }
    maage_auth_json_put(response); response = NULL;
    maage_auth_json_put(request); request = NULL;
    maage_auth_free_secret(body); body = NULL;
    /* Xbox services supplies the permanent XUID; Minecraft uses a separate token. */
    for (party = 0; party < 2; ++party) {
        properties = maage_auth_json_object(); tokens = maage_auth_json_array();
        maage_auth_json_array_string(tokens, user_token);
        maage_auth_json_add(properties, "UserTokens", tokens);
        maage_auth_json_string(properties, "SandboxId", "RETAIL");
        request = maage_auth_xbox_request(properties, party ? "rp://api.minecraftservices.com/" : "http://xboxlive.com");
        body = maage_auth_json_encode(request);
        if (!body) { result = MAAGE_AUTH_PROTOCOL; goto cleanup; }
        maage_auth_progress(auth, party ? "Authorizing access to Minecraft Services..." : "Verifying Xbox account identity...");
        result = maage_auth_http(auth, party ? "Minecraft XSTS authorization" : "Xbox identity authorization", auth->config.xsts, "application/json", body, NULL, &status, &response);
        if (result != MAAGE_AUTH_OK) goto cleanup;
        if (status != 200) {
            double code = maage_auth_json_double(response, "XErr");
            if (code == 2148916233.0) maage_auth_progress(auth, "Create an Xbox profile for this Microsoft account, then sign in again.");
            else if (code == 2148916238.0 || code == 2148916229.0) maage_auth_progress(auth, "Your Xbox family permissions need attention at account.microsoft.com/family.");
            else if (code == 2148916236.0 || code == 2148916237.0) maage_auth_progress(auth, "Complete Xbox age verification or check permitted play time.");
            else maage_auth_progress(auth, "Xbox authorization was refused. Check region restrictions, account status and Xbox terms.");
            result = MAAGE_AUTH_RESTRICTED; goto cleanup;
        }
        if (!maage_auth_xbox_claims(response, token, xsts_hash) || strcmp(xsts_hash, user_hash)) {
            strcpy(auth->response_reason, "XSTS token/user-hash claims are invalid or differ from the Xbox Live account.");
            result = MAAGE_AUTH_PROTOCOL; goto cleanup;
        }
        if (!party) {
            identity = maage_auth_json_text(maage_auth_json_at(maage_auth_json_get(maage_auth_json_get(response, "DisplayClaims"), "xui"), 0), "xid");
            if (!identity || !*identity || strlen(identity) > 20) {
                strcpy(auth->response_reason, "Xbox did not return a valid permanent XUID.");
                result = MAAGE_AUTH_PROTOCOL; goto cleanup;
            }
            for (index = 0; identity[index]; ++index) if (identity[index] < '0' || identity[index] > '9') {
                strcpy(auth->response_reason, "Xbox returned an invalid permanent XUID.");
                result = MAAGE_AUTH_PROTOCOL; goto cleanup;
            }
            strcpy(xuid, identity);
            maage_auth_json_put(response); response = NULL;
            maage_auth_json_put(request); request = NULL;
            maage_auth_free_secret(body); body = NULL;
        } else strcpy(hash, xsts_hash);
    }
cleanup:
    maage_auth_free_secret(body); maage_auth_free_secret(ticket); maage_auth_free_secret(user_token);
    maage_auth_json_put(request); maage_auth_json_put(response);
    return result;
}
