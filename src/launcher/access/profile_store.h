#ifndef MAAGE_LAUNCHER_PROFILE_STORE_H
#define MAAGE_LAUNCHER_PROFILE_STORE_H
#include "accounts.h"
#include "maage/skin.h"
/* Only OAuth refresh + current Minecraft credentials persist. Microsoft access,
 * Xbox Live and XSTS tokens are distinct, transient exchange objects. */
typedef struct maage_profile_session {
    char uuid[33], name[17];
    char microsoft_refresh[MAAGE_ACCESS_TOKEN_CAPACITY];
    char minecraft_token[MAAGE_ACCESS_TOKEN_CAPACITY];
    double expires_at, verified_at, verified_until;
    maage_skin_reference appearance; /* Transient public response, never credentials. */
    int skin_resolved;
} maage_profile_session;
/* AES-256-GCM records with a random local key and owner-only permissions.
 * Explicit project policy: no native credential stores. The local user can read
 * both key and ciphertext. Callers wipe sessions after use. */
maage_status maage_profile_store_load(const char *root, const char *uuid, maage_profile_session *session, maage_status_trace *trace);
maage_status maage_profile_store_save(const char *root, const maage_profile_session *session, maage_status_trace *trace);
maage_status maage_profile_store_remove(const char *root, const char *uuid, maage_status_trace *trace);
#endif
