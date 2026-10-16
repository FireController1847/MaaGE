#ifndef MAAGE_LAUNCHER_ASSETS_H
#define MAAGE_LAUNCHER_ASSETS_H
/* Launcher-owned, UI-independent operation. All snapshots are copied under a
 * lock; worker owns installation files. Destroy cancels and joins the worker. */
typedef enum maage_setup_stage {
    MAAGE_SETUP_NOT_STARTED, MAAGE_SETUP_PREPARING, MAAGE_SETUP_METADATA,
    MAAGE_SETUP_DOWNLOADING, MAAGE_SETUP_VERIFYING, MAAGE_SETUP_RECONSTRUCTING,
    MAAGE_SETUP_COMPLETE,
    MAAGE_SETUP_CANCELLED, MAAGE_SETUP_FAILED
} maage_setup_stage;
typedef struct maage_setup_progress {
    maage_setup_stage stage;
    unsigned long total_objects, verified_objects, total_logical_assets;
    unsigned long valid_cached_objects, downloaded_objects, reconstructed_files;
    /* Known outstanding defects, discovered as verification advances. */
    unsigned long missing_objects, missing_reconstructed_files;
    int objects_ready, reconstruction_ready;
    double downloaded_bytes, total_bytes;
    int percent; /* -1 until index totals are known; 100 only when complete. */
    int cancellation_requested, needs_setup, directories_ready, metadata_ready;
    char activity[256], error[512];
} maage_setup_progress;
typedef struct maage_assets maage_assets;
maage_assets *maage_assets_create(void);
/* Returns zero if a worker is active. Repair forces every object hash check. */
int maage_assets_start(maage_assets *assets, int repair);
/* Retry preserves whether the failed operation requested a full repair. */
int maage_assets_retry(maage_assets *assets);
void maage_assets_snapshot(maage_assets *assets, maage_setup_progress *progress);
void maage_assets_cancel(maage_assets *assets);
void maage_assets_destroy(maage_assets *assets);
#endif
