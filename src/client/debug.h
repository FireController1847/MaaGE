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
    double sample_start, refresh_after, frame_seconds, tick_seconds;
    double fps, frame_ms, tps, tick_ms;
    unsigned long frames, ticks;
    int initialized, timing_ready, tick_sample;
} maage_client_debug;
/* Cheap rolling counters; formatting and native memory queries run at 4 Hz
 * only while F3 is visible. All observations belong to the Client owner. */
void maage_client_debug_frame(maage_client_debug *debug, double now,
    double elapsed, int presented, unsigned long ticks, double tick_seconds);
const maage_client_ui_debug *maage_client_debug_collect(maage_client_debug *debug,
    double now, const maage_world *world, const maage_world_record *record,
    const maage_world_stats *stats, const maage_player *player, double yaw,
    double pitch, int radius, int paused, maage_renderer *renderer,
    const maage_client_mesh_stats *meshes);
#endif
