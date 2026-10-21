#ifndef MAAGE_LAUNCHER_CLIENT_RESOURCES_H
#define MAAGE_LAUNCHER_CLIENT_RESOURCES_H
#include "maage/status.h"
/* Private archive adapter; the Launcher verifies the source JAR before opening.
 * Only assets/ is stripped from logical output; namespace and archive names remain.
 * No Java execution or ZIP dependency is exposed to resource consumers. */
typedef struct maage_client_resources maage_client_resources;
typedef enum maage_client_phase {
    MAAGE_CLIENT_INSPECT, MAAGE_CLIENT_RECONSTRUCT, MAAGE_CLIENT_FINAL_CHECK
} maage_client_phase;
typedef struct maage_client_observer {
    int (*cancelled)(void *context);
    /* Reject conflicting index paths before either source is reconstructed. */
    maage_status (*collision)(void *context, const char *name, const char *sha1, double size, char error[512]);
    void (*progress)(void *context, maage_client_phase phase, unsigned long done,
        unsigned long total, const char *name, int changed);
    maage_status (*record)(void *context, const char *name, const char *archive_name, const char *sha1, double size, const char *fingerprint);
    void *context;
    maage_status_trace *trace;
} maage_client_observer;
/* Checks every archive path/type, inventories applicable resources, derives SHA-1
 * from CRC-checked source streams, and preflights index collisions. No writes. */
maage_status maage_client_resources_open(const char *archive_path,
    const char *application_data, const maage_client_observer *observer, char error[512], maage_client_resources **output);
/* Hash-checks existing resources and atomically repairs missing/corrupt files.
 * Staging must be outside the reconstructed tree, under the installation lock. */
maage_status maage_client_resources_reconstruct(maage_client_resources *resources,
    const char *application_data, const char *staging,
    const maage_client_observer *observer, char error[512]);
maage_status maage_client_resources_check(maage_client_resources *resources,
    const char *application_data, const maage_client_observer *observer, char error[512]);
maage_status maage_client_resources_close(maage_client_resources *resources);
#endif
