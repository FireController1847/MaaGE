#ifndef MAAGE_CLIENT_DEBUG_H
#define MAAGE_CLIENT_DEBUG_H
#include "ui/ui.h"
#include "maage/player.h"
#include "maage/world_store.h"

typedef struct maage_client_mesh_stats {
    unsigned int meshes;
    size_t vertices, indices;
} maage_client_mesh_stats;
typedef struct maage_client_debug {
    maage_client_ui_debug text;
    double sample_start, sample_seconds, tick_seconds;
    double fps, frame_ms, latest_frame_ms, tps, tick_ms;
    double frames, ticks; /* exponentially weighted event counts */
    int initialized, timing_ready, tick_sample;
    double measurement_after, observation_time, busy_workers;
    const maage_renderer *identity_renderer;
    char version[MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
    char system[MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
    char renderer[MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
    char gpu[MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
    char performance[MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
    char memory[MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
    char workers[MAAGE_CLIENT_UI_DEBUG_CHARACTERS];
} maage_client_debug;
/* Player/target observations remain live each visible frame. Performance uses
 * a 0.75-second exponential history, displayed at 250 ms intervals; native memory
 * is sampled at that interval. Static identity is formatted on renderer changes.
 * latest_frame_ms remains unsmoothed for upload scheduling. Owner-thread only:
 * no heap allocation, disk I/O, World lock or worker wait. */
void maage_client_debug_frame(maage_client_debug *debug, double now,
    double elapsed, int presented, unsigned long ticks, double tick_seconds);
const maage_client_ui_debug *maage_client_debug_collect(maage_client_debug *debug,
    double now, const maage_world *world, const maage_world_record *record,
    const maage_world_stats *stats, const maage_player *player, double yaw,
    double pitch, int radius, int paused, maage_renderer *renderer,
    const maage_client_mesh_stats *meshes, const maage_gameplay *game);
#endif
