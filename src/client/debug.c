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
     * from dominating this one-second history after normal frames resume. */
    retained = exp(-duration);
    weight = retained < 1.0 ? (1.0 - retained) / duration : 1.0;
    debug->sample_seconds = debug->sample_seconds * retained + duration * weight;
    debug->frames = debug->frames * retained + (presented ? weight : 0);
    debug->ticks = debug->ticks * retained + (double)ticks * weight;
    debug->tick_seconds = debug->tick_seconds * retained + tick_seconds * weight;
    debug->fps = debug->frames / debug->sample_seconds;
    if (presented) debug->frame_ms = duration * 1000.0;
    debug->tps = debug->ticks / debug->sample_seconds;
    debug->tick_sample = debug->ticks != 0;
    debug->tick_ms = debug->ticks ? debug->tick_seconds * 1000.0 / debug->ticks : 0;
    debug->timing_ready = 1;
}
const maage_client_ui_debug *maage_client_debug_collect(maage_client_debug *debug,
    double now, const maage_world *world, const maage_world_record *record,
    const maage_world_stats *stats, const maage_player *player, double yaw,
    double pitch, int radius, int paused, maage_renderer *renderer,
    const maage_client_mesh_stats *meshes, const maage_gameplay *game)
{
    maage_client_ui_debug *text = &debug->text;
    const maage_client_metadata *identity = maage_client_identity();
    const char *gpu = maage_renderer_device_name(renderer);
    static const char *const facing[4] = {"North (-Z)", "East (+X)", "South (+Z)", "West (-X)"};
    maage_world_observation observation;
    char seed[32];
    double degrees, resident, peak;
    int x, y, z, direction;
    unsigned left = 0, right = 0;
    (void)now;
    x = (int)floor(player->position[0]); y = (int)floor(player->position[1]); z = (int)floor(player->position[2]);
    degrees = fmod(yaw * 57.29577951308232, 360.0);
    if (degrees < 0) degrees += 360.0;
    direction = ((int)floor((degrees + 45.0) / 90.0)) % 4;
    maage_world_seed_format(record->seed, seed, sizeof(seed));
    sprintf(text->left[left++], "MaaGE %.96s+%.32s", identity->version, identity->build);
    if (debug->timing_ready) sprintf(text->left[left++], "FPS: %.1f  frame: %.2f ms", debug->fps, debug->frame_ms);
    else strcpy(text->left[left++], "FPS / frame: N/A (collecting)");
    if (debug->timing_ready) sprintf(text->left[left++], "TPS: %.1f / 20%s", debug->tps, paused ? " (paused)" : "");
    else strcpy(text->left[left++], "TPS: N/A (collecting)");
    if (debug->tick_sample) sprintf(text->left[left++], "Tick time: %.3f ms", debug->tick_ms);
    else strcpy(text->left[left++], "Tick time: N/A (no ticks)");
    sprintf(text->left[left++], "World: %.64s", record->id);
    sprintf(text->left[left++], "Seed: %s", seed);
    sprintf(text->left[left++], "XYZ: %.3f / %.3f / %.3f", player->position[0], player->position[1], player->position[2]);
    sprintf(text->left[left++], "Block: %d %d %d", x, y, z);
    sprintf(text->left[left++], "Chunk: %d %d  local: %d %d", maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z),
        maage_world_local_coordinate(x), maage_world_local_coordinate(z));
    sprintf(text->left[left++], "Facing: %s", facing[direction]);
    sprintf(text->left[left++], "Yaw: %.1f  pitch: %.1f", degrees, pitch * 57.29577951308232);
    if (maage_world_observe(world, x, y, z, &observation)) {
        sprintf(text->left[left++], "Biome: %u (%s)", observation.biome_id, maage_client_debug_biome(observation.biome_id));
        if (observation.light_available)
            sprintf(text->left[left++], "Light: %u sky / %u block", (unsigned)observation.light >> 4, (unsigned)observation.light & 15);
        else strcpy(text->left[left++], "Light: N/A (pending)");
    } else {
        strcpy(text->left[left++], "Biome: N/A (loading)"); strcpy(text->left[left++], "Light: N/A (loading)");
    }
    sprintf(text->left[left++], "Chunks: %u loaded / %u ready", stats->loaded, stats->ready);
    sprintf(text->left[left++], "Rendered: %u  distance: %d", meshes->meshes, radius);
    sprintf(text->left[left++], "Sections: %u resident", stats->loaded_sections);
    sprintf(text->left[left++], "Vertices: %lu  triangles: %lu", (unsigned long)meshes->vertices, (unsigned long)(meshes->indices / 3));
    sprintf(text->left[left++], "Pending columns: %u", stats->pending);
    if (game) {
        if (game->has_target) {
            sprintf(text->left[left++], "Target: %d %d %d  face: %u", game->target.x,
                game->target.y, game->target.z, (unsigned int)game->target.face);
            sprintf(text->left[left++], "Target ID: %lu  state: %lu", game->target.block_id, game->target.state_id);
        } else strcpy(text->left[left++], "Target: N/A (none / unavailable)");
        sprintf(text->left[left++], "Selected item: %lu  slot: %u",
            game->inventory.slots[game->inventory.selected].item_id, game->inventory.selected + 1);
        sprintf(text->left[left++], "Mode: %s%s", game->mode == MAAGE_GAME_MODE_CREATIVE ?
            "Creative" : "Survival", game->flying ? " (flying)" : "");
        sprintf(text->left[left++], "Health: %u / 20  hunger: %u / 20", game->health, game->hunger);
        sprintf(text->left[left++], "Break: %.1f%%  reach: %.1f", game->break_progress * 100.0,
            game->reach > 0 ? game->reach : (game->mode == MAAGE_GAME_MODE_CREATIVE ? 5.0 : 4.5));
        sprintf(text->left[left++], "Mutations: %lu broken / %lu placed", game->blocks_broken, game->blocks_placed);
    }
    sprintf(text->right[right++], "OS: %.48s / %.48s", identity->platform, identity->architecture);
    sprintf(text->right[right++], "Renderer: %s", maage_render_backend_name(maage_renderer_get_backend(renderer)));
    sprintf(text->right[right++], "GPU: %.144s", gpu ? gpu : "N/A");
    sprintf(text->right[right++], "Client: %.96s", identity->version);
    sprintf(text->right[right++], "Build: %.48s + %.32s", identity->build_type, identity->build);
    maage_client_debug_memory(&resident, &peak);
    if (resident >= 0) sprintf(text->right[right++], "Resident: %.1f MiB", resident / 1048576.0);
    else strcpy(text->right[right++], "Resident memory: N/A");
    if (peak >= 0) sprintf(text->right[right++], "Peak RSS: %.1f MiB", peak / 1048576.0);
    else strcpy(text->right[right++], "Peak RSS: N/A");
    if (stats->logical_processors) sprintf(text->right[right++], "Logical CPUs: %u", stats->logical_processors);
    else strcpy(text->right[right++], "Logical CPUs: N/A (undetected)");
    sprintf(text->right[right++], "Pool workers: %u", stats->worker_count);
    sprintf(text->right[right++], "Active generation workers: %u", stats->generation_running);
    sprintf(text->right[right++], "Generation: %u queued / %u running", stats->generation_queued, stats->generation_running);
    sprintf(text->right[right++], "Meshing: %u queued / %u running", stats->meshing_queued, stats->meshing_running);
    sprintf(text->right[right++], "Lighting: %u queued / %u running", stats->lighting_queued, stats->lighting_running);
    sprintf(text->right[right++], "I/O: %u queued / %u running", stats->io_queued, stats->io_running);
    sprintf(text->right[right++], "All jobs: %u queued / %u running", stats->queued_jobs, stats->running_jobs);
    sprintf(text->right[right++], "Completed: %lu", stats->completed_jobs);
    sprintf(text->right[right++], "Cancelled: %lu  stale: %lu", stats->cancelled_jobs, stats->stale_jobs);
    sprintf(text->right[right++], "Queue latency: %.2f ms", stats->scheduling_latency_ms);
    sprintf(text->right[right++], "Mutations: %lu  rejected: %lu", stats->mutations, stats->rejected_mutations);
    sprintf(text->right[right++], "Dirty columns: %u", stats->dirty_columns);
    text->left_count = left; text->right_count = right;
    return text;
}
