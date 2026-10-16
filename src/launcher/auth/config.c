#include "internal.h"
#include "maage/auth_config.h"
#include <stdlib.h>
#include <string.h>

void maage_auth_wipe(void *memory, size_t size)
{
    volatile unsigned char *bytes = (volatile unsigned char *)memory;
    while (size--) *bytes++ = 0;
}
int maage_auth_copy(char *output, size_t capacity, const char *input)
{
    size_t length;
    if (!input) { if (capacity) output[0] = '\0'; return 0; }
    length = strlen(input);
    if (length >= capacity) { if (capacity) output[0] = '\0'; return 0; }
    memcpy(output, input, length + 1);
    return 1;
}
void maage_auth_free_secret(char *text)
{
    if (text) { maage_auth_wipe(text, strlen(text)); free(text); }
}
int maage_auth_config_init(maage_auth_config *config)
{
    const char *id = getenv("MAAGE_MICROSOFT_CLIENT_ID");
    size_t index;
    memset(config, 0, sizeof(*config));
    config->device = MAAGE_AUTH_DEVICE_URL;
    config->token = MAAGE_AUTH_TOKEN_URL;
    config->xbox = MAAGE_AUTH_XBOX_URL;
    config->xsts = MAAGE_AUTH_XSTS_URL;
    config->minecraft = MAAGE_AUTH_MINECRAFT_URL;
    if (!id || !*id) id = MAAGE_MICROSOFT_CLIENT_ID;
    if (!maage_auth_copy(config->client_id, sizeof(config->client_id), id) || strlen(config->client_id) != 36) return 0;
    for (index = 0; index < 36; ++index) {
        char c = id[index];
        if (index == 8 || index == 13 || index == 18 || index == 23) { if (c != '-') return 0; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
        if (c >= 'A' && c <= 'F') config->client_id[index] = (char)(c + ('a' - 'A'));
    }
    return 1;
}
const char *maage_auth_message(maage_auth_error error)
{
    switch (error) {
    case MAAGE_AUTH_OK: return "Account validation complete.";
    case MAAGE_AUTH_CANCELLED: return "Sign-in cancelled.";
    case MAAGE_AUTH_NETWORK: return "Could not reach authentication services. Check your connection, then try again.";
    case MAAGE_AUTH_REAUTH: return "Your Microsoft sign-in has expired or was revoked. Sign in again.";
    case MAAGE_AUTH_RESTRICTED: return "Xbox authorization was refused. Check your Xbox profile, region, age and family permissions.";
    case MAAGE_AUTH_CONFIGURATION: return "Configure MaaGE's own Microsoft public-client ID with device-code sign-in enabled. Minecraft Services access may require approval.";
    case MAAGE_AUTH_STORAGE: return "Account storage could not be accessed. Unlock your credential store and check MaaGE data directory permissions, then retry.";
    case MAAGE_AUTH_BROWSER: return "The system browser could not be opened. Check your default browser and try again.";
    case MAAGE_AUTH_EXPIRED: return "This sign-in code expired. Start sign-in again for a new code.";
    case MAAGE_AUTH_DENIED: return "Microsoft sign-in was declined. You can start sign-in again when ready.";
    case MAAGE_AUTH_LIMIT: return "The account limit has been reached. Remove an account before adding another.";
    case MAAGE_AUTH_SESSION: return "Minecraft rejected the session. Refresh it; if the problem persists, sign in again.";
    case MAAGE_AUTH_PROFILE: return "Minecraft Java profile unavailable. Complete your Java profile at minecraft.net, then refresh.";
    default: return "Authentication returned an invalid or unexpected response. Try again or check application registration and service availability.";
    }
}
