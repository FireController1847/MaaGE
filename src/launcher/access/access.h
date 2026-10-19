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
    int connected, session_saved, refreshing;
    double last_verified;
} maage_access_profile;
typedef struct maage_access_snapshot {
    maage_access_profile profiles[MAAGE_ACCESS_PROFILES];
    unsigned count, revision;
    int busy, allowed, auto_continue;
    char status[512], device_url[256], device_code[33];
    double device_deadline;
} maage_access_snapshot;
typedef struct maage_access maage_access;
/* UI-independent single-worker service; snapshots never contain credentials.
 * Registry metadata cannot authorize assets. Only authenticated, bounded leases
 * from the encrypted MaaGE-owned OAuth account store can grant access. */
maage_access *maage_access_create(void);
int maage_access_restore(maage_access *access);
int maage_access_sign_in(maage_access *access, int device_code);
int maage_access_verify_local(maage_access *access, const char *uuid);
int maage_access_sign_out(maage_access *access, const char *uuid);
int maage_access_disconnect(maage_access *access, const char *uuid);
void maage_access_snapshot_get(maage_access *access, maage_access_snapshot *snapshot);
int maage_access_allowed(maage_access *access);
/* NULL accepts any verified account for setup; selection restricts services. */
int maage_access_select(maage_access *access, const char *uuid);
/* Operation cancellation permits retry; closing revokes all in-memory leases. */
void maage_access_cancel_operation(maage_access *access);
void maage_access_cancel(maage_access *access);
void maage_access_destroy(maage_access *access);
const char *maage_access_state_name(maage_access_state state);
/* Main thread: 1 continue, 0 cancel, -1 unavailable. */
int maage_access_native_run(maage_access *access, int manage_accounts);
#endif
