#ifndef MAAGE_LAUNCHER_CLIENT_RESOURCES_H
#define MAAGE_LAUNCHER_CLIENT_RESOURCES_H
/* Private archive adapter; the Launcher verifies the source JAR before opening.
 * assets/minecraft/ is stripped from logical output; archive names are retained.
 * No Java execution or ZIP dependency is exposed to resource consumers. */
typedef struct maage_client_resources maage_client_resources;
typedef enum maage_client_phase {
    MAAGE_CLIENT_INSPECT, MAAGE_CLIENT_RECONSTRUCT, MAAGE_CLIENT_FINAL_CHECK
} maage_client_phase;
typedef struct maage_client_observer {
    int (*cancelled)(void *context);
    /* Reject conflicting index paths before either source is reconstructed. */
    int (*collision)(void *context, const char *name, const char *sha1, double size, char error[512]);
    void (*progress)(void *context, maage_client_phase phase, unsigned long done,
        unsigned long total, const char *name, int changed);
    int (*record)(void *context, const char *name, const char *archive_name, const char *sha1, double size, const char *fingerprint);
    void *context;
} maage_client_observer;
/* Checks every archive path/type, inventories applicable resources, derives SHA-1
 * from CRC-checked source streams, and preflights index collisions. No writes. */
maage_client_resources *maage_client_resources_open(const char *archive_path,
    const char *application_data, const maage_client_observer *observer, char error[512]);
/* Hash-checks existing resources and atomically repairs missing/corrupt files.
 * Staging must be outside the reconstructed tree, under the installation lock. */
int maage_client_resources_reconstruct(maage_client_resources *resources,
    const char *application_data, const char *staging,
    const maage_client_observer *observer, char error[512]);
int maage_client_resources_check(maage_client_resources *resources,
    const char *application_data, const maage_client_observer *observer, char error[512]);
void maage_client_resources_close(maage_client_resources *resources);
#endif
