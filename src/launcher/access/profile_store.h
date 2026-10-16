#ifndef MAAGE_LAUNCHER_PROFILE_STORE_H
#define MAAGE_LAUNCHER_PROFILE_STORE_H
#include "accounts.h"
/* Versioned MaaGE-owned identity + credential-provider record. A future Microsoft
 * OAuth provider replaces credentials for the same UUID; no fake refresh token,
 * client ID, or new sign-in flow is created by the session-import provider. */
typedef struct maage_profile_session {
    char uuid[33], name[17];
    char minecraft_token[MAAGE_ACCESS_TOKEN_CAPACITY];
    double expires_at, verified_at, verified_until;
} maage_profile_session;
/* AES-256-GCM account files and a random local key, with owner-only file access.
 * No OS credential store. The local user can access both key and account files.
 * Callers wipe sessions after use; no credentials are returned in UI snapshots. */
int maage_profile_store_load(const char *root, const char *uuid, maage_profile_session *session);
int maage_profile_store_save(const char *root, const maage_profile_session *session);
int maage_profile_store_remove(const char *root, const char *uuid);
#endif
