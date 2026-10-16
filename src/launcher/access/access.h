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
    int connected, available;
    double last_verified;
} maage_access_profile;
typedef struct maage_access_snapshot {
    maage_access_profile profiles[MAAGE_ACCESS_PROFILES];
    unsigned count, revision, launchers_found, unsupported_files;
    int busy, allowed;
    char detected_launchers[160], status[512];
} maage_access_snapshot;
typedef struct maage_access maage_access;
/* All entry points are UI-independent. Operations serialize on one worker.
 * Creation performs no account-file I/O. Snapshots contain no credentials.
 * Grants exist only in memory, expire within one hour, and never restore from disk. */
maage_access *maage_access_create(void);
int maage_access_scan(maage_access *access, const char *manual_directory);
/* Call only after explicit consent to read the selected launcher's MC session. */
int maage_access_connect(maage_access *access, const char *uuid);
int maage_access_disconnect(maage_access *access, const char *uuid);
void maage_access_snapshot_get(maage_access *access, maage_access_snapshot *snapshot);
int maage_access_allowed(maage_access *access);
void maage_access_cancel(maage_access *access);
void maage_access_destroy(maage_access *access);
const char *maage_access_state_name(maage_access_state state);
/* Native UI, main thread: 1 continue, 0 cancel, -1 unavailable. */
int maage_access_native_run(maage_access *access);
#define MAAGE_ACCESS_CONSENT "Connect this profile? MaaGE will read only its temporary Minecraft Services session token from a compatible PrismLauncher/MultiMC account file and send it to Minecraft Services to check the profile and Java access. The token will not be saved. Other launchers remain unchanged."
#endif
