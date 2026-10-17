#ifndef MAAGE_LAUNCHER_ACCESS_H
#define MAAGE_LAUNCHER_ACCESS_H
#include <stddef.h>
#define MAAGE_ACCESS_PROFILES 64
typedef enum maage_access_state {
    MAAGE_ACCESS_DETECTED, MAAGE_ACCESS_CONNECTED, MAAGE_ACCESS_VERIFYING,
    MAAGE_ACCESS_VERIFIED, MAAGE_ACCESS_EXPIRED, MAAGE_ACCESS_UNSUPPORTED,
    MAAGE_ACCESS_FAILED
} maage_access_state;
typedef struct maage_access_profile {
    char name[17], uuid[33], sources[160], detail[256];
    maage_access_state state;
    int connected, available, imported;
    double last_verified;
} maage_access_profile;
typedef struct maage_access_snapshot {
    maage_access_profile profiles[MAAGE_ACCESS_PROFILES];
    unsigned count, revision, launchers_found, unsupported_files;
    int busy, allowed, auto_continue;
    char detected_launchers[160], status[512];
} maage_access_snapshot;
typedef struct maage_access maage_access;
/* All entry points are UI-independent. Operations serialize on one worker.
 * Creation performs no account-file I/O. Snapshots contain no credentials.
 * Authenticated local verification leases expire within one hour; plain registry
 * flags never grant access. Stored sessions revalidate without source-launcher I/O. */
maage_access *maage_access_create(void);
int maage_access_scan(maage_access *access, const char *manual_directory);
/* Call only after explicit consent to read the selected launcher's MC session. */
int maage_access_connect(maage_access *access, const char *uuid);
int maage_access_verify_local(maage_access *access, const char *uuid);
int maage_access_disconnect(maage_access *access, const char *uuid);
void maage_access_snapshot_get(maage_access *access, maage_access_snapshot *snapshot);
int maage_access_allowed(maage_access *access);
/* Restrict subsequent authorization to a connected account; NULL restores the
 * initial-setup policy accepting any current account. Does not grant a lease. */
int maage_access_select(maage_access *access, const char *uuid);
void maage_access_cancel(maage_access *access);
void maage_access_destroy(maage_access *access);
const char *maage_access_state_name(maage_access_state state);
/* Native UI, main thread: 1 continue, 0 cancel, -1 unavailable. */
/* manage_accounts keeps the account controls visible even with a valid lease. */
int maage_access_native_run(maage_access *access, int manage_accounts);
#define MAAGE_ACCESS_CONSENT "Import this account into MaaGE? MaaGE will copy only its Minecraft Services session token from the selected compatible launcher, verify Java access, and save it encrypted in MaaGE's local accounts folder with a local key. No system credential storage is used. The original launcher is unchanged. This session expires; re-import will be needed until Microsoft sign-in is implemented."
#endif
