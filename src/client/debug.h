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
    double fps, frame_ms, tps, tick_ms;
    double frames, ticks; /* exponentially weighted event counts */
    int initialized, timing_ready, tick_sample;
} maage_client_debug;
/* Frame time is the latest completed interval; FPS/TPS/tick averages update
 * every frame with a one-second exponential history.
 * Formatting and native memory observations update every visible F3 frame.
 * All observations belong to the Client owner; no update-frequency throttle. */
void maage_client_debug_frame(maage_client_debug *debug, double now,
    double elapsed, int presented, unsigned long ticks, double tick_seconds);
const maage_client_ui_debug *maage_client_debug_collect(maage_client_debug *debug,
    double now, const maage_world *world, const maage_world_record *record,
    const maage_world_stats *stats, const maage_player *player, double yaw,
    double pitch, int radius, int paused, maage_renderer *renderer,
    const maage_client_mesh_stats *meshes, const maage_gameplay *game);
#endif
