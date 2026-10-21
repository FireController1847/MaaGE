#ifndef MAAGE_LAUNCHER_ACCESS_H
#define MAAGE_LAUNCHER_ACCESS_H
#include <stddef.h>
#include "maage/status.h"
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
    maage_access_profile *profiles;
    unsigned count, capacity, revision;
    int busy, allowed, auto_continue;
    char status[512], device_url[256], device_code[33];
    double device_deadline;
    maage_status result;
    maage_status_trace trace; /* Owned snapshot copy; no account/token strings. */
} maage_access_snapshot;
typedef struct maage_access maage_access;
/* UI-independent single-worker service; snapshots never contain credentials.
 * Registry metadata cannot authorize assets. Only authenticated, bounded leases
 * from the encrypted MaaGE-owned OAuth account store can grant access. */
maage_status maage_access_create(maage_access **output, maage_status_trace *trace);
maage_status maage_access_restore(maage_access *access);
maage_status maage_access_sign_in(maage_access *access, int device_code);
maage_status maage_access_verify_local(maage_access *access, const char *uuid);
maage_status maage_access_sign_out(maage_access *access, const char *uuid);
maage_status maage_access_disconnect(maage_access *access, const char *uuid);
/* Each caller owns its deep-copied snapshot; zero-initialize, then release. */
maage_status maage_access_snapshot_get(maage_access *access, maage_access_snapshot *snapshot);
maage_status maage_access_snapshot_release(maage_access_snapshot *snapshot);
int maage_access_allowed(maage_access *access);
/* NULL accepts any verified account for setup; selection restricts services. */
maage_status maage_access_select(maage_access *access, const char *uuid);
/* Operation cancellation permits retry; closing revokes all in-memory leases. */
maage_status maage_access_cancel_operation(maage_access *access);
maage_status maage_access_cancel(maage_access *access);
maage_status maage_access_destroy(maage_access *access);
const char *maage_access_state_name(maage_access_state state);
/* Main thread: 1 continue, 0 cancel, -1 unavailable. */
maage_status maage_access_native_run(maage_access *access, int manage_accounts, int *action);
#endif
