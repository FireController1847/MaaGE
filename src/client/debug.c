#include "debug.h"
#include "maage/client.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

/* Platform-specific diagnostics stay private to this Client collector. No
 * filesystem reads, allocation, GUI mutation or world locks are needed. */
static void maage_client_debug_memory(double *resident, double *peak)
{
    *resident = *peak = -1.0;
#if defined(_WIN32)
    {
        PROCESS_MEMORY_COUNTERS memory;
        memset(&memory, 0, sizeof(memory)); memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
            *resident = (double)memory.WorkingSetSize;
            *peak = (double)memory.PeakWorkingSetSize;
        }
    }
#elif defined(__APPLE__)
    {
        mach_task_basic_info_data_t memory;
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        struct rusage usage;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&memory, &count) == KERN_SUCCESS)
            *resident = (double)memory.resident_size;
        if (!getrusage(RUSAGE_SELF, &usage)) *peak = (double)usage.ru_maxrss;
    }
#elif defined(__linux__)
    {
        struct rusage usage;
        if (!getrusage(RUSAGE_SELF, &usage)) *peak = (double)usage.ru_maxrss * 1024.0;
    }
#endif
}
static const char *maage_client_debug_biome(maage_biome_id biome)
{
    /* Display labels only: gameplay definitions remain numeric and immutable. */
    static const char *const names[MAAGE_BIOME_COUNT] = {
        "Ocean", "Plains", "Desert", "Extreme Hills", "Forest", "Taiga"
    };
    return biome < MAAGE_BIOME_COUNT ? names[biome] : "Unknown";
}
void maage_client_debug_frame(maage_client_debug *debug, double now,
    double elapsed, int presented, unsigned long ticks, double tick_seconds)
{
    double duration, retained, weight;
    if (!debug->initialized) { debug->sample_start = now - elapsed; debug->initialized = 1; }
    duration = now - debug->sample_start;
    debug->sample_start = now;
    if (!(duration > 0)) return;
    /* Update every frame. Integrating the interval's weight keeps a long stall
     * from dominating this 0.75-second history after normal frames resume. */
    retained = exp(-duration / 0.75);
    weight = retained < 1.0 ? (1.0 - retained) / duration : 1.0;
    debug->sample_seconds = debug->sample_seconds * retained + duration * weight;
    debug->frames = debug->frames * retained + (presented ? weight : 0);
    debug->ticks = debug->ticks * retained + (double)ticks * weight;
    debug->tick_seconds = debug->tick_seconds * retained + tick_seconds * weight;
    debug->fps = debug->frames / debug->sample_seconds;
    if (presented) debug->latest_frame_ms = duration * 1000.0;
    debug->frame_ms = debug->frames ? debug->sample_seconds * 1000.0 / debug->frames : 0;
    debug->tps = debug->ticks / debug->sample_seconds;
    debug->tick_sample = debug->ticks != 0;
    debug->tick_ms = debug->ticks ? debug->tick_seconds * 1000.0 / debug->ticks : 0;
    debug->timing_ready = 1;
}
const maage_client_ui_debug *maage_client_debug_collect(maage_client_debug *debug,
    double now, const maage_session_snapshot *snapshot, int radius, int paused,
    maage_renderer *renderer, const maage_client_mesh_stats *meshes)
{
    maage_client_ui_debug *text;
    const maage_client_metadata *identity = maage_client_identity();
    static const char *const facing[4] = {"North (-Z)", "East (+X)", "South (+Z)", "West (-X)"};
    const maage_world_stats *stats;
    const maage_player *player;
    double yaw, pitch;
    const char *gpu;
    double degrees, resident, peak, retained;
    int x, y, z, direction;
    unsigned left = 0, right = 0;
    if (!debug || !snapshot || !renderer || !meshes ||
        snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) return NULL;
    text = &debug->text;
    stats = &snapshot->world_stats;
    player = &snapshot->player.player;
    yaw = snapshot->player.yaw;
    pitch = snapshot->player.pitch;
    if (debug->identity_renderer != renderer) {
        debug->identity_renderer = renderer;
        gpu = maage_renderer_device_name(renderer);
        sprintf(debug->version, "MaaGE %.96s+%.32s", identity->version, identity->build);
        sprintf(debug->system, "%.48s / %.48s", identity->platform, identity->architecture);
        sprintf(debug->renderer, "Renderer: %s", maage_render_backend_name(maage_renderer_get_backend(renderer)));
        if (gpu && *gpu) sprintf(debug->gpu, "GPU: %.144s", gpu);
        else debug->gpu[0] = 0;
    }
    retained = debug->observation_time > 0 ? exp(-(now - debug->observation_time) / 0.75) : 0;
    debug->busy_workers = debug->busy_workers * retained + stats->running_jobs * (1.0 - retained);
    debug->observation_time = now;
    if (now >= debug->measurement_after) {
        if (debug->timing_ready)
            sprintf(debug->performance, "%.0f FPS | %.1f TPS | %.1f ms", debug->fps, debug->tps, debug->frame_ms);
        else strcpy(debug->performance, "Collecting timing...");
        maage_client_debug_memory(&resident, &peak);
        debug->memory[0] = 0;
        if (resident >= 0) sprintf(debug->memory, "Memory: %.0f MiB", resident / 1048576.0);
        else if (peak >= 0) sprintf(debug->memory, "Peak memory: %.0f MiB", peak / 1048576.0);
        sprintf(debug->workers, "Workers: %.0f/%u | Chunks queued: %u",
            debug->busy_workers, stats->worker_count,
            stats->generation_queued + stats->meshing_queued + stats->lighting_queued + stats->io_queued);
        debug->measurement_after = now + 0.25;
    }
    x = (int)floor(player->position[0]); y = (int)floor(player->position[1]); z = (int)floor(player->position[2]);
    degrees = fmod(yaw * 57.29577951308232, 360.0);
    if (degrees < 0) degrees += 360.0;
    direction = ((int)floor((degrees + 45.0) / 90.0)) % 4;
    strcpy(text->left[left++], debug->version);
    sprintf(text->left[left++], "%.160s%s", debug->performance, paused ? " (paused)" : "");
    sprintf(text->left[left++], "C: %u rendered / %u loaded | %u pending | D: %d",
        meshes->meshes, stats->loaded, stats->pending, radius);
    sprintf(text->left[left++], "XYZ: %.3f / %.3f / %.3f", player->position[0], player->position[1], player->position[2]);
    sprintf(text->left[left++], "Block: %d %d %d", x, y, z);
    sprintf(text->left[left++], "Chunk: %d %d | local: %d %d", maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z),
        maage_world_local_coordinate(x), maage_world_local_coordinate(z));
    sprintf(text->left[left++], "Facing: %s (%.1f / %.1f)", facing[direction], degrees, pitch * 57.29577951308232);
    if (snapshot->has_observation) {
        const maage_world_observation *observation = &snapshot->observation;
        sprintf(text->left[left++], "Biome: %s", maage_client_debug_biome(observation->biome_id));
        if (observation->light_available)
            sprintf(text->left[left++], "Light: %u sky / %u block",
                (unsigned)observation->light >> 4, (unsigned)observation->light & 15);
    }
    if (snapshot->player.has_target)
        sprintf(text->left[left++], "Target: %d %d %d | %lu:%lu",
            snapshot->player.target.x, snapshot->player.target.y, snapshot->player.target.z,
            snapshot->player.target.block_id, snapshot->player.target.state_id);
    if (debug->memory[0]) strcpy(text->right[right++], debug->memory);
    strcpy(text->right[right++], debug->system);
    strcpy(text->right[right++], debug->renderer);
    if (debug->gpu[0]) strcpy(text->right[right++], debug->gpu);
    if (stats->logical_processors) sprintf(text->right[right++], "CPU: %u logical cores", stats->logical_processors);
    strcpy(text->right[right++], debug->workers);
    text->left_count = left; text->right_count = right;
    return text;
}
