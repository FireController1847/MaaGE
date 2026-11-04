#include "../application/content_modules.h"
#ifdef MAAGE_CLIENT_MACOS_DOCK_NAME
#include "macos/dock_name.h"
#endif
#include "maage/window.h"
#include "maage/client.h"
#include "maage/input.h"
#include "maage/render.h"
#include "maage/storage.h"
#include "maage/minecraft_assets.h"
#include "maage/time.h"
#include "maage/player.h"
#include "maage/gameplay.h"
#include "maage/session_protocol.h"
#include "maage/local_session.h"
#include "maage/server_runtime.h"
#include "maage/content.h"
#include "audio_service.h"
#include "audio_native.h"
#include "maage/log.h"
#include "resources.h"
#include "ui/ui.h"
#include "ui/splash.h"
#include "debug.h"
#include "interaction.h"
#include "prediction.h"
#include "item_entities.h"
#include "player_view.h"
#include "camera.h"
#include "environment.h"
#include "storage_context.h"
#include "options.h"
#include "frontend.h"
#include "pacing.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <float.h>
#include <stdio.h>
#include "mesh_diagnostics.h"

/* Camera tuning remains provisional pending programmer verification. */
#define MAAGE_CLIENT_PITCH_LIMIT 1.553343034
#define MAAGE_CLIENT_MAX_FRAME_SECONDS 0.25
/* Presentation policy: discard effects older than the Server's four-tick
 * catch-up window instead of replaying a stale burst after a Client stall. */
#define MAAGE_CLIENT_SOUND_MAX_AGE_TICKS 4UL
#define MAAGE_CLIENT_SOUND_MAX_AGE_SECONDS 0.2
/* Bounded, recurring audit. Each visit checks one loaded column and
 * searches the Client GPU cache; rebuilding happens on normal World jobs. */
#define MAAGE_CLIENT_MESH_VERIFY_INTERVAL 0.025
#define MAAGE_CLIENT_MESH_VERIFY_BUDGET 96U

typedef struct maage_client_world_options {
    const char *id, *name;
    maage_world_seed seed;
    unsigned int workers;
    int create, seeded, radius, radius_specified, mode;
    double reach, mesh_budget_ms, frame_target_ms;
} maage_client_world_options;
typedef struct maage_client_mesh_cache {
    maage_client_chunk_mesh *slots;
    const maage_render_mesh **draws;
    size_t capacity, retire_cursor;
    unsigned long generation;
    maage_world_mesh_update uploading;
    maage_render_mesh *upload_mesh;
} maage_client_mesh_cache;
typedef struct maage_client_mesh_activity {
    maage_client_mesh_diagnostics status;
    maage_status last_result;
    double sample_start, previous_upload_ms;
    unsigned long sample_uploads, sample_vertices;
} maage_client_mesh_activity;
typedef struct maage_client_mesh_verifier {
    size_t cursor;
    int center_x, center_z, radius;
    double next_at;
} maage_client_mesh_verifier;

/* A smaller Server target does not retire already acknowledged Client bases.
 * Leave packet headroom above every live footprint until FIFO unloads arrive. */
static maage_status maage_client_replica_capacity(const maage_world_stats *server, const maage_world *replica, size_t packet_capacity, unsigned int *capacity) {
    unsigned int retained = server->cache_capacity, resident = maage_world_resident_columns(replica);
    if (server->resident_columns > retained) retained = server->resident_columns;
    if (resident > retained) retained = resident;
    if (!server->cache_capacity || packet_capacity > UINT_MAX - retained) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET, 0);
    *capacity = retained + (unsigned int) packet_capacity;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}

/* Textual options are resolved before any world/runtime work begins. */
static maage_status maage_client_world_options_parse(int argc, char *const argv[], maage_client_world_options *options) {
    int i;
    const char *argument;
    char *end;
    long value;
    unsigned long workers;
    memset(options, 0, sizeof(*options));
    options->radius = MAAGE_CLIENT_RENDER_DISTANCE_DEFAULT;
    options->mode = -1;
    options->frame_target_ms = 1000.0 / 60.0;
    for (i = 1; i < argc; ++i) {
        argument = argv[i];
        if (!strncmp(argument, "--world=", 8)) {
            if (options->id || options->create) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->id = argument + 8;
        } else if (!strncmp(argument, "--create-world=", 15)) {
            if (options->id || options->create) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->id = argument + 15;
            options->create = 1;
        } else if (!strcmp(argument, "--new-world")) {
            if (options->id || options->create) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->create = 1;
        } else if (!strncmp(argument, "--seed=", 7)) {
            if (maage_status_failed(maage_world_seed_parse(argument + 7, &options->seed))) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->seeded = 1;
        } else if (!strncmp(argument, "--world-name=", 13)) options->name = argument + 13;
        else if (!strncmp(argument, "--render-distance=", 18)) {
            errno = 0;
            value = strtol(argument + 18, &end, 10);
            if (errno || !argument[18] || *end || value < 1 || value > MAAGE_WORLD_MAX_RADIUS) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->radius = (int) value;
            options->radius_specified = 1;
        } else if (!strncmp(argument, "--mesh-upload-budget-ms=", 24)) {
            errno = 0;
            options->mesh_budget_ms = strtod(argument + 24, &end);
            if (errno || !argument[24] || *end || !(options->mesh_budget_ms >= 0.0 && options->mesh_budget_ms <= DBL_MAX)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
        } else if (!strncmp(argument, "--mesh-frame-target-ms=", 23)) {
            errno = 0;
            options->frame_target_ms = strtod(argument + 23, &end);
            if (errno || !argument[23] || *end || !(options->frame_target_ms > 0.0 && options->frame_target_ms <= DBL_MAX)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
        } else if (!strncmp(argument, "--world-workers=", 16)) {
            if (argument[16] < '0' || argument[16] > '9') return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            errno = 0;
            workers = strtoul(argument + 16, &end, 10);
            if (errno || *end || workers > UINT_MAX) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->workers = (unsigned int) workers;
        } else if (!strncmp(argument, "--gamemode=", 11)) {
            if (!strcmp(argument + 11, "creative")) options->mode = MAAGE_GAME_MODE_CREATIVE;
            else if (!strcmp(argument + 11, "survival")) options->mode = MAAGE_GAME_MODE_SURVIVAL;
            else
                return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
        } else if (!strncmp(argument, "--interaction-range=", 20)) {
            errno = 0;
            options->reach = strtod(argument + 20, &end);
            if (errno || !argument[20] || *end || !(options->reach > 0 && options->reach <= 64)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
        } else if (!strcmp(argument, "--world") || !strcmp(argument, "--create-world") ||
                   !strcmp(argument, "--seed") || !strcmp(argument, "--world-name") || !strcmp(argument, "--render-distance") ||
                   !strcmp(argument, "--world-workers") || !strcmp(argument, "--mesh-upload-budget-ms") ||
                   !strcmp(argument, "--mesh-frame-target-ms") || !strcmp(argument, "--gamemode") ||
                   !strcmp(argument, "--interaction-range"))
            return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    }
    if (options->seeded && options->id && !options->create) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    if (options->name && !options->create && !options->seeded) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PARSE, 0);
}

/* Mesh resources remain Client-owned. Capacity grows with actual resident
 * meshes, independently of the World's scheduling/cache implementation. */
static maage_status maage_client_mesh_cache_grow(maage_client_mesh_cache *cache) {
    maage_client_chunk_mesh *slots;
    const maage_render_mesh **draws;
    size_t previous = cache->capacity, grown;
    if (previous > ((size_t) - 1 - 1) / 2) grown = previous + 1;
    else
        grown = previous ? previous * 2 : 32;
    if (grown <= previous || grown > (size_t) - 1 / sizeof(*slots) || grown >= (size_t) - 1 / sizeof(*draws) - 1) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    slots = (maage_client_chunk_mesh *) realloc(cache->slots, grown * sizeof(*slots));
    if (!slots) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    cache->slots = slots;
    draws = (const maage_render_mesh **) realloc(cache->draws, (grown + 2) * sizeof(*draws));
    if (!draws) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    cache->draws = draws;
    memset(cache->slots + previous, 0, (grown - previous) * sizeof(*slots));
    cache->capacity = grown;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
static void maage_client_mesh_cache_drop(maage_client_mesh_cache *cache, size_t index, maage_world *visual) {
    if (visual && cache->slots[index].used && !cache->slots[index].presented) (void) maage_world_mesh_prepared(visual, cache->slots[index].x, cache->slots[index].z, 0);
    maage_render_mesh_destroy(cache->slots[index].prepared);
    maage_render_mesh_destroy(cache->slots[index].prediction);
    maage_world_mesh_destroy(cache->slots[index].source);
    maage_render_mesh_destroy(cache->slots[index].mesh);
    memset(&cache->slots[index], 0, sizeof(cache->slots[index]));
    ++cache->generation;
}
static maage_status maage_client_mesh_cache_clear(maage_client_mesh_cache *cache, maage_world *visual) {
    size_t i;
    for (i = 0; i < cache->capacity; ++i)
        maage_client_mesh_cache_drop(cache, i, visual);
    maage_render_mesh_destroy(cache->upload_mesh);
    maage_world_mesh_destroy(cache->uploading.mesh);
    free(cache->slots);
    free(cache->draws);
    memset(cache, 0, sizeof(*cache));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
static maage_status maage_client_reload_checkpoint(void *context, maage_status_trace *trace) {
    return MAAGE_STATUS_PROPAGATE(trace, maage_window_poll_events((maage_window_system *) context, 0), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOAD);
}
/* Render-owner reload transaction. Logical content, session, prediction actions
 * and authoritative World are unchanged. A retired generation supplies old jobs
 * until their normal completion handoff; a second reload waits for that release. */
static maage_status maage_client_reload_resources(maage_window_system *system, const char *directory, maage_content *content, maage_renderer *renderer, maage_client_ui *ui, maage_world *visual, maage_client_resources *resources, maage_client_resources *retired, maage_render_texture **atlas, maage_client_environment **environment, maage_client_item_entities **entities, maage_client_mesh_cache *cache, maage_client_interaction_visual *interaction, maage_client_first_person *first_person, maage_client_player_view *player, maage_client_prediction *prediction, maage_status_trace *trace) {
    maage_client_resources candidate;
    maage_render_texture *texture = NULL;
    maage_client_environment *sky = NULL;
    maage_client_item_entities *models = NULL;
    maage_client_ui_reload *gui = NULL;
    const maage_block_registry *blocks;
    maage_status result;
    if (retired->view) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_LOAD, 0);
    memset(&candidate, 0, sizeof(candidate));
    result = maage_client_resources_load(directory, content, &candidate, maage_client_storage_player(), maage_client_reload_checkpoint, system, trace);
    if (!maage_status_is_success(result)) goto rollback;
    result = maage_renderer_create_texture(renderer, &candidate.atlas, &texture, trace);
    if (!maage_status_is_success(result)) goto rollback;
    blocks = maage_content_view_blocks(candidate.view);
    result = maage_client_environment_create(renderer, directory, &candidate, &sky, trace);
    if (!maage_status_is_success(result)) goto rollback;
    result = maage_client_item_entities_create(renderer, texture, &candidate, maage_content_items(content), blocks, &models, trace);
    if (!maage_status_is_success(result)) goto rollback;
    result = maage_client_ui_reload_prepare(ui, directory, &candidate, &gui, trace);
    if (!maage_status_is_success(result)) goto rollback;
    result = maage_renderer_set_instances(renderer, NULL, 0);
    if (!maage_status_failed(result)) result = maage_renderer_set_scene(renderer, 1, NULL, NULL);
    if (!maage_status_failed(result)) result = maage_renderer_set_foreground(renderer, NULL, NULL, 0, 0);
    if (maage_status_failed(result)) goto rollback;
    result = maage_world_refresh_presentation(visual, blocks, maage_content_view_biomes(candidate.view));
    if (maage_status_failed(result)) goto rollback;
    /* All fallible preparation is complete. Clear borrowed render arrays before
     * retiring their meshes; backends defer native destruction until GPU users retire. */
    maage_client_first_person_destroy(first_person, renderer);
    maage_client_player_view_destroy(player);
    maage_client_mesh_cache_clear(cache, NULL);
    maage_render_mesh_destroy(prediction->reveal);
    prediction->reveal = NULL;
    prediction->dirty = 1;
    ++prediction->generation;
    prediction->visual_revision = 0;
    maage_render_mesh_destroy(interaction->mesh);
    maage_client_item_entities_destroy(*entities);
    maage_client_environment_destroy(*environment);
    maage_client_ui_reload_commit(ui, gui, &candidate);
    maage_render_texture_destroy(*atlas);
    *retired = *resources;
    *resources = candidate;
    *atlas = texture;
    *environment = sky;
    *entities = models;
    maage_client_interaction_init(interaction, resources);
    maage_client_first_person_init(first_person, resources);
    maage_image_destroy(&resources->image);
    resources->atlas.pixels = NULL;
    return maage_content_load_complete(content, trace);
rollback:
    maage_client_ui_reload_destroy(gui);
    maage_client_item_entities_destroy(models);
    maage_client_environment_destroy(sky);
    maage_render_texture_destroy(texture);
    maage_client_resources_destroy(&candidate);
    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.resources", result, trace, "LOAD refresh failed; retaining the previous complete presentation.");
    return result;
}
/* Reclaim obsolete presentation even when uploads are deferred by a modal,
 * a prediction update or an unavailable drawable. Work is budgeted per frame;
 * a complete pass also contracts high-water-only slot/draw arrays. */
static int maage_client_mesh_cache_retire(maage_client_mesh_cache *cache, maage_world *visual, int center_x, int center_z, int radius, double budget_ms) {
    size_t scanned = 0, index, highest = 0, target = 32;
    double began = maage_time_monotonic_seconds();
    maage_client_chunk_mesh *slots;
    const maage_render_mesh **draws;
    if (cache->uploading.mesh &&
        (!maage_world_chunk_loaded(visual, cache->uploading.x, cache->uploading.z) ||
         abs(cache->uploading.x - center_x) > radius + 1 || abs(cache->uploading.z - center_z) > radius + 1)) {
        (void) maage_world_mesh_prepared(visual, cache->uploading.x, cache->uploading.z, 0);
        maage_render_mesh_destroy(cache->upload_mesh);
        maage_world_mesh_destroy(cache->uploading.mesh);
        cache->upload_mesh = NULL;
        memset(&cache->uploading, 0, sizeof(cache->uploading));
    }
    while (scanned < cache->capacity) {
        index = cache->retire_cursor++;
        if (cache->retire_cursor == cache->capacity) cache->retire_cursor = 0;
        ++scanned;
        if (cache->slots[index].used &&
            (!maage_world_chunk_loaded(visual, cache->slots[index].x, cache->slots[index].z) ||
             abs(cache->slots[index].x - center_x) > radius + 1 ||
             abs(cache->slots[index].z - center_z) > radius + 1))
            maage_client_mesh_cache_drop(cache, index, visual);
        if (began >= 0.0 && (maage_time_monotonic_seconds() - began) * 1000.0 >= budget_ms) return 0;
    }
    for (index = 0; index < cache->capacity; ++index)
        if (cache->slots[index].used) highest = index + 1;
    while (target < highest && target <= (size_t) - 1 / 2)
        target *= 2;
    if (target >= highest && target <= cache->capacity / 2) {
        slots = (maage_client_chunk_mesh *) realloc(cache->slots, target * sizeof(*slots));
        if (slots) {
            cache->slots = slots;
            cache->capacity = target;
            cache->retire_cursor = 0;
            ++cache->generation;
            draws = (const maage_render_mesh **) realloc(cache->draws, (target + 2) * sizeof(*draws));
            if (draws) cache->draws = draws;
        }
    }
    return 1;
}
/* Periodically compare each loaded, requested column with the Client's
 * owned GPU mesh. A valid empty mesh still occupies a used cache slot, so
 * there is no false repair loop for legitimately invisible geometry.
 * The replica World owns retry/backoff and decides which job, if any,
 * needs resubmission. No disk or GPU calls happen in this audit. */
static maage_status maage_client_verify_meshes(maage_world *visual, const maage_client_mesh_cache *cache, maage_client_mesh_verifier *verifier, int cx, int cz, int radius, double now) {
    size_t span, total, index, i;
    unsigned examined;
    int x, z;
    unsigned long revision;
    maage_status result;
    if (!visual || !cache || !verifier || radius < 1) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_VERIFY, 0);
    if (verifier->center_x != cx || verifier->center_z != cz || verifier->radius != radius) {
        verifier->center_x = cx;
        verifier->center_z = cz;
        verifier->radius = radius;
        verifier->cursor = 0;
        verifier->next_at = 0.0;
    }
    if (now < verifier->next_at) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_VERIFY, 0);
    verifier->next_at = now + MAAGE_CLIENT_MESH_VERIFY_INTERVAL;
    span = (size_t) radius * 2U + 1U;
    total = span * span; /* bounded by the validated World render distance */
    for (examined = 0; examined < MAAGE_CLIENT_MESH_VERIFY_BUDGET; ++examined) {
        index = verifier->cursor++;
        if (verifier->cursor >= total) verifier->cursor = 0;
        x = cx + (int) (index % span) - radius;
        z = cz + (int) (index / span) - radius;
        if (!maage_world_chunk_loaded(visual, x, z)) continue;
        if (cache->uploading.mesh && cache->uploading.x == x && cache->uploading.z == z) continue;
        revision = 0;
        for (i = 0; i < cache->capacity; ++i)
            if (cache->slots[i].used && cache->slots[i].x == x && cache->slots[i].z == z) {
                revision = cache->slots[i].visual_revision;
                break;
            }
        result = maage_world_reconcile_mesh(visual, x, z, revision);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS)
            MAAGE_LOG_VERBOSE_EVENT("maage.client.mesh", maage_log_verbose("maage.client.mesh", "Reconciled incomplete Client presentation for column %d,%d; cached GPU revision=%lu; operation=%u.", x, z, revision, (unsigned) maage_status_operation(result)));
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_VERIFY, 0);
}
/* Cleanup continues after a reported failure while retaining the first failed
 * outcome. Each independent cleanup occurrence gets its own bounded trace. */
/* Save only at explicit option boundaries, never from the rendering path.
 * A snapshot uses the last confirmed window mode during AppKit transitions. */
static maage_status maage_client_options_store(maage_client_frontend *frontend, const maage_client_ui *ui, maage_client_options *saved, int force) {
    maage_client_options current;
    maage_status result;
    maage_client_ui_get_options(ui, &current);
    if (!force && maage_client_options_equal(&current, saved)) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SAVE, 0);
    result = maage_client_frontend_save_options(frontend, &current);
    if (!maage_status_failed(result)) *saved = current;
    return result;
}

static void maage_client_cleanup_result(maage_status result, maage_status *outcome, maage_status_trace *trace, maage_status_trace *failure_trace) {
    if (maage_status_failed(result)) {
        MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_DESTROY);
        maage_log_trace_status(MAAGE_LOG_ERROR, "maage.client", result, trace, "Client cleanup reported a failure.");
        if (!maage_status_failed(*outcome)) {
            *outcome = result;
            if (failure_trace) *failure_trace = *trace;
        }
    }
    maage_status_trace_init(trace);
}
static double maage_client_mesh_budget(const maage_client_world_options *options, const maage_client_debug *debug, const maage_client_mesh_activity *activity) {
    double other_work_ms, headroom;
    if (options->mesh_budget_ms > 0.0) return options->mesh_budget_ms;
    other_work_ms = debug->timing_ready ? debug->latest_frame_ms - activity->previous_upload_ms : 0.0;
    if (other_work_ms < 0.0) other_work_ms = 0.0;
    headroom = options->frame_target_ms - other_work_ms;
    /* Reserve some throughput when already over target, and no more than
     * one quarter of the frame for streaming when otherwise idle. */
    if (headroom < options->frame_target_ms * 0.05) headroom = options->frame_target_ms * 0.05;
    if (headroom > options->frame_target_ms * 0.25) headroom = options->frame_target_ms * 0.25;
    return headroom * 0.75;
}
static void maage_client_mesh_rates(maage_client_mesh_activity *activity, double now) {
    double interval = now - activity->sample_start;
    if (!(interval >= 1.0)) return;
    activity->status.uploads_per_second = (double) activity->sample_uploads / interval;
    activity->status.vertices_per_second = (double) activity->sample_vertices / interval;
    activity->sample_uploads = activity->sample_vertices = 0;
    activity->sample_start = now;
}
/* Worker-prepared geometry is copied in bounded byte slices on the renderer
 * thread. One pending upload borrows its CPU source until completion/cancel;
 * previous presentation stays intact throughout the handoff. Driver allocation
 * latency is still platform-dependent, independent of the copy budget. */
static maage_status maage_client_stream_meshes(maage_world *visual, maage_renderer *renderer, maage_render_texture *atlas, maage_client_mesh_cache *cache, maage_client_mesh_activity *activity, const maage_client_prediction *prediction, const maage_client_world_options *options, const maage_client_debug *debug, int center_x, int center_z) {
    maage_world_mesh_update *update = &cache->uploading;
    const maage_mesh_data *geometry;
    maage_mesh_preparation *prepared;
    maage_render_mesh *filtered_mesh;
    maage_status result;
    int filtered;
    double begin, current, budget_ms = maage_client_mesh_budget(options, debug, activity);
    size_t i, selected, vacant;
    unsigned processed = 0;
    activity->status.uploaded_this_frame = 0;
    activity->status.frame_upload_ms = 0;
    activity->status.budget_ms = budget_ms;
    begin = maage_time_monotonic_seconds();
    if (begin < 0) return maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPDATE, 0);
    for (;;) {
        current = maage_time_monotonic_seconds();
        if (current < 0) return maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPDATE, 0);
        if (processed && (current - begin) * 1000 >= budget_ms) break;
        if (!update->mesh) {
            result = maage_world_take_mesh(visual, update);
            if (maage_status_code(result) == MAAGE_STATUS_NOOP || maage_status_code(result) == MAAGE_STATUS_INVALID_STATE) break;
            if (maage_status_failed(result)) return result;
        }
        ++processed;
        if (!update->mesh || !maage_world_mesh_current(visual, update->x, update->z, update->revision) || abs(update->x - center_x) > options->radius || abs(update->z - center_z) > options->radius) {
            ++activity->status.rejected;
            maage_render_mesh_destroy(cache->upload_mesh);
            cache->upload_mesh = NULL;
            maage_world_mesh_destroy(update->mesh);
            memset(update, 0, sizeof(*update));
            continue;
        }
        if (!cache->upload_mesh) {
            for (i = 0; i < cache->capacity; ++i)
                if (cache->slots[i].used && cache->slots[i].x == update->x && cache->slots[i].z == update->z && cache->slots[i].visual_revision >= update->revision) break;
            if (i < cache->capacity) {
                ++activity->status.rejected;
                maage_world_mesh_destroy(update->mesh);
                memset(update, 0, sizeof(*update));
                continue;
            }
        }
        geometry = maage_world_mesh_data(update->mesh);
        if (geometry->index_count) {
            if (!cache->upload_mesh) {
                prepared = maage_world_mesh_take_preparation(update->mesh);
                result = maage_renderer_begin_geometry(renderer, &prepared, atlas, &cache->upload_mesh, NULL);
                maage_mesh_preparation_destroy(prepared);
                if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto upload_failed;
            }
            result = maage_render_mesh_upload_step(cache->upload_mesh, 262144U, NULL);
            if (maage_status_code(result) == MAAGE_STATUS_NOOP) continue;
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto upload_failed;
        }
        /* Finished uploads no longer require the complete CPU vertex/index
         * arrays. Future local predictions reconstruct only filtered indices. */
        maage_world_mesh_release_upload_data(update->mesh);
        selected = vacant = cache->capacity;
        for (i = 0; i < cache->capacity; ++i) {
            if (cache->slots[i].used && cache->slots[i].x == update->x && cache->slots[i].z == update->z) {
                selected = i;
                break;
            }
            if (!cache->slots[i].used && vacant == cache->capacity) vacant = i;
        }
        if (selected == cache->capacity) {
            selected = vacant;
            if (selected == cache->capacity) {
                result = maage_client_mesh_cache_grow(cache);
                if (maage_status_failed(result)) goto upload_failed;
            }
        }
        filtered_mesh = NULL;
        result = maage_client_prediction_mesh(prediction, cache->upload_mesh, update->mesh, update->x, update->z, &filtered_mesh, &filtered, NULL);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
            maage_render_mesh_destroy(filtered_mesh);
            goto upload_failed;
        }
        maage_render_mesh_destroy(cache->slots[selected].prediction);
        maage_render_mesh_destroy(cache->slots[selected].mesh);
        maage_world_mesh_destroy(cache->slots[selected].source);
        cache->slots[selected].prediction = filtered_mesh;
        cache->slots[selected].filtered = filtered;
        cache->slots[selected].filtered_revision = update->revision;
        cache->slots[selected].filtered_generation = prediction->applied_generation;
        cache->slots[selected].mesh = cache->upload_mesh;
        cache->upload_mesh = NULL;
        cache->slots[selected].source = update->mesh;
        cache->slots[selected].used = 1;
        cache->slots[selected].x = update->x;
        cache->slots[selected].z = update->z;
        cache->slots[selected].visual_revision = update->revision;
        cache->slots[selected].revision = update->state_revision;
        cache->slots[selected].presentation_generation = update->presentation_generation;
        cache->slots[selected].vertices = geometry->vertex_count;
        cache->slots[selected].indices = geometry->index_count;
        cache->slots[selected].initialized_at = update->initialized_at;
        cache->slots[selected].uploaded_at = maage_time_monotonic_seconds();
        result = maage_world_mesh_prepared(visual, update->x, update->z, update->revision);
        memset(update, 0, sizeof(*update));
        if (maage_status_failed(result)) return result;
        ++cache->generation;
        ++activity->status.gpu_uploads;
        ++activity->sample_uploads;
        ++activity->status.uploaded_this_frame;
        activity->status.vertices_uploaded += (unsigned long) geometry->vertex_count;
        activity->sample_vertices += (unsigned long) geometry->vertex_count;
        continue;
    upload_failed:
        maage_render_mesh_destroy(cache->upload_mesh);
        cache->upload_mesh = NULL;
        maage_world_mesh_destroy(update->mesh);
        update->mesh = NULL;
        if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED && maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW) return result;
        if (activity->last_result != result) {
            activity->last_result = result;
            maage_log_status(MAAGE_LOG_WARNING, "maage.client.render", result, "Chunk upload unavailable; retaining previous presentation and retrying.");
        }
        result = maage_world_retry_mesh(visual, update->x, update->z, update->revision);
        memset(update, 0, sizeof(*update));
        if (maage_status_failed(result)) return result;
        break;
    }
    current = maage_time_monotonic_seconds();
    activity->status.stream_ms = (current - begin) * 1000;
    activity->status.frame_upload_ms = activity->status.stream_ms;
    activity->previous_upload_ms = activity->status.frame_upload_ms;
    maage_client_mesh_rates(activity, current);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}

typedef struct maage_client_fov {
    double multiplier;
    unsigned long session_epoch, world_epoch, life;
    int initialized;
} maage_client_fov;

static double maage_client_fov_update(maage_client_fov *view, const maage_session_snapshot *snapshot, double elapsed) {
    double target = snapshot->player.player.sprinting ? (MAAGE_PLAYER_SPRINT_SPEED + 1.0) * 0.5 : 1.0;
    if (snapshot->player.flying) target *= 1.1;
    if (!view->initialized || view->session_epoch != snapshot->session_epoch || view->world_epoch != snapshot->world_epoch || view->life != snapshot->player.motion.life) {
        view->multiplier = target;
        view->initialized = 1;
        view->session_epoch = snapshot->session_epoch;
        view->world_epoch = snapshot->world_epoch;
        view->life = snapshot->player.motion.life;
    } else if (!snapshot->paused && elapsed > 0) {
        /* Half the remaining difference per 20 Hz tick, expressed
         * continuously so rendering FPS and duplicate snapshots cannot reset it.
         * A newly observed target never fast-forwards across a long frame stall. */
        if (elapsed > 0.1) elapsed = 0.1;
        view->multiplier += (target - view->multiplier) * (1.0 - pow(0.5, elapsed / MAAGE_PLAYER_TICK_SECONDS));
    }
    return view->multiplier;
}

static void maage_client_camera(float output[16], const double eye[3], double yaw, double pitch, double aspect, int radius, double fov, const float bob[16], int first_person) {
    double sy = sin(yaw), cy = cos(yaw), sp = sin(pitch), cp = cos(pitch);
    double forward[3], right[3], up[3], focal, far_clip;
    float view[16], projection[16], bobbed_view[16];
    int row, column, k;
    forward[0] = sy * cp;
    forward[1] = -sp;
    forward[2] = -cy * cp;
    right[0] = cy;
    right[1] = 0;
    right[2] = sy;
    up[0] = sy * sp;
    up[1] = cp;
    up[2] = -cy * sp;
    memset(view, 0, sizeof(view));
    memset(projection, 0, sizeof(projection));
    for (k = 0; k < 3; ++k) {
        view[k * 4] = (float) right[k];
        view[k * 4 + 1] = (float) up[k];
        view[k * 4 + 2] = (float) -forward[k];
        view[12] -= (float) (right[k] * eye[k]);
        view[13] -= (float) (up[k] * eye[k]);
        view[14] += (float) (forward[k] * eye[k]);
    }
    view[15] = 1.0f;
    if (first_person) view[14] -= 0.1f;
    focal = 1.0 / tan(fov * 0.5);
    projection[0] = (float) (focal / aspect);
    projection[5] = (float) focal;
    /* Include the furthest chunk corner and full world height, even while
     * moving within the center chunk; keep the established minimum range. */
    far_clip = ((double) radius + 2.0) * MAAGE_CHUNK_EDGE * 1.4142135623730951 + MAAGE_WORLD_HEIGHT;
    if (far_clip < 512.0) far_clip = 512.0;
    /* Right-handed perspective, near=.05, depth [0,1]. */
    projection[10] = (float) (far_clip / (0.05 - far_clip));
    projection[11] = -1.0f;
    projection[14] = (float) (0.05 * far_clip / (0.05 - far_clip));
    for (column = 0; column < 4; ++column)
        for (row = 0; row < 4; ++row) {
            bobbed_view[column * 4 + row] = 0;
            for (k = 0; k < 4; ++k)
                bobbed_view[column * 4 + row] += bob[k * 4 + row] * view[column * 4 + k];
        }
    for (column = 0; column < 4; ++column)
        for (row = 0; row < 4; ++row) {
            output[column * 4 + row] = 0;
            for (k = 0; k < 4; ++k)
                output[column * 4 + row] += projection[k * 4 + row] * bobbed_view[column * 4 + k];
        }
}

/* Session outbound commands are ordered and never silently discarded when
 * the transport backpressures. Unobserved held-only INPUT samples may be
 * replaced by a newer held-only sample; discrete actions and the ordering of
 * PAUSE, inventory, respawn and disconnect are preserved. This queue belongs
 * exclusively to the presentation thread. */
#define MAAGE_CLIENT_OUTBOX_CAPACITY 128U
typedef struct maage_client_outbox {
    maage_session_command commands[MAAGE_CLIENT_OUTBOX_CAPACITY];
    unsigned int head, count;
    unsigned long next_sequence, input_sequence;
    int diagnostics;
} maage_client_outbox;

static int maage_client_input_coalescible(const maage_session_command *command) {
    /* Held actions are levels, but a freshly pressed jump is a one-shot.
     * Coalesce only when held action levels have not changed, so rapid
     * click/release transitions cannot be swallowed by frame-rate sampling. */
    return command->kind == MAAGE_CMD_INPUT && !command->intent.jump_pressed && !command->intent.attack_pressed && !command->intent.use_pressed && !command->intent.forward_pressed;
}

static int maage_client_input_unchanged(const maage_session_command *a, const maage_session_command *b) {
    if (a->intent.attack_pressed || a->intent.use_pressed || a->intent.jump_pressed || a->intent.forward_pressed) return 0;
    return a->intent.movement.yaw == b->intent.movement.yaw && a->intent.pitch == b->intent.pitch &&
           a->intent.movement.forward == b->intent.movement.forward && a->intent.movement.strafe == b->intent.movement.strafe &&
           a->intent.movement.jump == b->intent.movement.jump && a->intent.movement.sprint == b->intent.movement.sprint &&
           a->intent.descend == b->intent.descend && a->intent.attack == b->intent.attack && a->intent.use == b->intent.use && a->use_snapshot == b->use_snapshot;
}

static maage_status maage_client_outbox_queue(maage_client_outbox *outbox, const maage_session_command *input) {
    maage_session_command command;
    unsigned int tail, previous;
    if (!outbox || !input) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 0);
    command = *input;
    command.queued_at = outbox->diagnostics ? maage_time_monotonic_seconds() : -1;
    command.sent_at = -1;
    command.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    if (outbox->count && maage_client_input_coalescible(&command)) {
        previous = (outbox->head + outbox->count - 1U) % MAAGE_CLIENT_OUTBOX_CAPACITY;
        if (maage_client_input_coalescible(&outbox->commands[previous]) &&
            outbox->commands[previous].intent.attack == command.intent.attack &&
            outbox->commands[previous].intent.use == command.intent.use &&
            outbox->commands[previous].intent.movement.jump == command.intent.movement.jump &&
            outbox->commands[previous].intent.movement.forward == command.intent.movement.forward &&
            outbox->commands[previous].intent.movement.strafe == command.intent.movement.strafe &&
            outbox->commands[previous].intent.movement.sprint == command.intent.movement.sprint &&
            outbox->commands[previous].intent.descend == command.intent.descend) {
            /* Keep the original FIFO sequence; the last held-only input wins.
             * No edge-triggered or authoritative command is overwritten. */
            command.sequence = outbox->commands[previous].sequence;
            outbox->commands[previous] = command;
            outbox->input_sequence = command.sequence;
            return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
        }
    }
    if (outbox->count == MAAGE_CLIENT_OUTBOX_CAPACITY) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 0);
    if (outbox->next_sequence == ULONG_MAX) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 1);
    command.sequence = ++outbox->next_sequence;
    if (command.kind == MAAGE_CMD_INPUT) outbox->input_sequence = command.sequence;
    tail = (outbox->head + outbox->count) % MAAGE_CLIENT_OUTBOX_CAPACITY;
    outbox->commands[tail] = command;
    ++outbox->count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}

/* A full local mailbox is not a lost command. Leave the FIFO head intact,
 * pump native events/presentation, and retry before sampling another input. */
static maage_status maage_client_outbox_flush(maage_client_outbox *outbox, maage_local_session *session) {
    maage_status result;
    if (!outbox || !session) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 0);
    while (outbox->count) {
        maage_session_command *command = &outbox->commands[outbox->head];
        command->sent_at = outbox->diagnostics ? maage_time_monotonic_seconds() : -1;
        result = maage_local_session_send(session, command);
        if (maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW) return result;
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 0);
        if (outbox->diagnostics && (command->prediction || command->intent.attack_pressed || command->intent.use_pressed)) MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "action=%lu command_sequence=%lu kind=%u sampled_at=%.6f queued_at=%.6f sent_at=%.6f outbox_ms=%.3f", command->prediction, command->sequence, command->kind, command->sampled_at, command->queued_at, command->sent_at, (command->sent_at - command->queued_at) * 1000));
        outbox->head = (outbox->head + 1U) % MAAGE_CLIENT_OUTBOX_CAPACITY;
        --outbox->count;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}

static void maage_client_audio_click(void *context) {
    maage_client_audio_service *audio = (maage_client_audio_service *) context;
    if (audio) (void) maage_client_audio_service_ui_click(audio, NULL);
}

/* The Client owns a presentation-only World replica and immutable CPU/GPU
 * visuals. Only the reusable Server runtime owns simulation and persistence. */
maage_status maage_client_run(int argc, char *const argv[], maage_status_trace *trace) {
    maage_window_system *system = NULL;
    maage_window *window = NULL;
    maage_renderer *renderer = NULL;
    maage_render_texture *atlas = NULL;
    maage_client_ui *ui = NULL;
    maage_client_splash splash;
    maage_client_frontend *frontend = NULL;
    maage_client_world_choice choice;
    int direct_world = 0;
    maage_client_environment *environment_view = NULL;
    maage_client_audio_service *audio = NULL;
    int retain_content = 0, retired_in_shutdown = 0;
    maage_local_session *session = NULL;
    maage_server_runtime *server = NULL;
    maage_world *visual_world = NULL;
    maage_world_config visual_config;
    maage_world_stats visual_stats;
    double visual_stats_time = -1;
    maage_local_column_update column_update;
    maage_content *content = NULL;
    maage_content_config content_registration;
    const maage_block_registry *blocks = NULL;
    const maage_item_registry *items = NULL;
    maage_instance *instance = maage_client_storage_instance();
    maage_client_resources resources, retired_resources;
    maage_session_command last_input;
    int has_last_input = 0;
    maage_client_mesh_cache cache;
    maage_client_prediction prediction;
    unsigned long prediction_mesh_generation = 0;
    maage_client_mesh_activity mesh_activity;
    maage_client_mesh_verifier mesh_verifier;
    maage_client_world_options options;
    maage_controls *controls = NULL;
    maage_client_options video_options, saved_video_options, current_options;
    maage_client_interaction_visual interaction;
    maage_client_first_person first_person;
    maage_client_camera_effects camera_effects;
    maage_client_swing local_swing;
    maage_client_food_animation food_animation;
    maage_session_snapshot visual_snapshot;
    double swing_progress = 0;
    maage_client_player_view player_view;
    maage_world_camera_sample collision;
    unsigned int camera_mode = MAAGE_CAMERA_FIRST;
    maage_client_item_entities *item_entities = NULL;
    maage_client_debug debug;
    maage_client_mesh_stats mesh_stats;
    maage_client_ui_input ui_input;
    const maage_render_ui_data *ui_draw;
    const maage_client_ui_debug *debug_text;
    maage_render_desc render_config;
    maage_window_config window_config;
    maage_window_state state;
    maage_input_snapshot input;
    maage_server_config server_config;
    maage_session_snapshot latest, previous, incoming, action_snapshot;
    maage_session_event event;
    maage_session_command command;
    maage_client_outbox outbox;
    maage_status result = 0, operation = 0, rendered = 0, outcome = 0;
    maage_status resource_notice = 0, mesh_notice = 0, audio_notice = 0;
    maage_status_trace cleanup_trace;
    const char *failure_message = "Client presentation cannot continue.";
    char failure_detail[256];
    char directory[MAAGE_PATH_CAPACITY];
    char window_title[128];
    double last = 0.0, now = 0.0, raw_elapsed = 0.0, frame_end = 0.0;
    double diagnostic_time = 0.0, received_time = 0.0, alpha = 1.0, startup_began = 0.0;
    double memory_reclaim_after = -1.0;
    maage_client_pacing pacing = {0, 0.0};
    maage_client_fov fov = {0.0, 0, 0, 0, 0};
    double effective_fov;
    const maage_fluid_definition *eye_fluid, *camera_fluid;
    double eye[3], forward[3], yaw = 0.0, pitch = 0.0, wheel = 0.0;
    double render_yaw = 0.0, render_pitch = 0.0;
    unsigned long world_epoch = 0, last_sound_sequence = 0, last_seen_tick = 0;
    unsigned long sound_epoch = 0, sound_world_epoch = 0, frames = 0, tick_count = 0;
    float matrix[16];
    double camera_eye[3], camera_yaw, camera_pitch, camera_distance;
    size_t draw_count, resident_count, slot_index, first_count;
    int drop_latched = 0, drop_stack = 0;
    unsigned int drop_slot = 0;
    int valid_snapshot = 0, connected = 0, ok = 0, options_ready = 0, server_started = 0, i, axis;
    int initial_capture = 1, active = 0, was_active = 0;
    int was_modal = 0, was_focused = 0, changing_mode = 0, was_changing_mode = 0;
    int was_options_visible = 0, options_save_needed = 0, resize_pending = 0;
    int logged_max_fps, logged_vsync, paused_command = -1;
    int selected_hotbar = 0, queued_hotbar = -1, view_sent = 0, disconnected = 0, cx = 0, cz = 0;
    int jump_latched = 0, attack_latched = 0, use_latched = 0, respawn_latched = 0, forward_latched = 0;
    unsigned int ui_actions = 0, event_budget;
    memset(&resources, 0, sizeof(resources));
    memset(&retired_resources, 0, sizeof(retired_resources));
    memset(&splash, 0, sizeof(splash));
    memset(&cache, 0, sizeof(cache));
    memset(&prediction, 0, sizeof(prediction));
    memset(&local_swing, 0, sizeof(local_swing));
    memset(&food_animation, 0, sizeof(food_animation));
    memset(&mesh_activity, 0, sizeof(mesh_activity));
    memset(&mesh_verifier, 0, sizeof(mesh_verifier));
    memset(&interaction, 0, sizeof(interaction));
    memset(&first_person, 0, sizeof(first_person));
    memset(&camera_effects, 0, sizeof(camera_effects));
    memset(&player_view, 0, sizeof(player_view));
    memset(&collision, 0, sizeof(collision));
    memset(&debug, 0, sizeof(debug));
    memset(&outbox, 0, sizeof(outbox));
    outbox.diagnostics = maage_log_enabled("maage.client.latency", MAAGE_LOG_VERBOSE) && getenv("MAAGE_WORLD_DIAGNOSTICS") && !strcmp(getenv("MAAGE_WORLD_DIAGNOSTICS"), "1");
    memset(&latest, 0, sizeof(latest));
    memset(&previous, 0, sizeof(previous));
    memset(&server_config, 0, sizeof(server_config));
    maage_render_desc_init(&render_config);
    rendered = maage_render_desc_parse_args(argc, argv, &render_config);
    if (maage_status_code(rendered) != MAAGE_STATUS_SUCCESS) goto done;
    operation = maage_client_world_options_parse(argc, argv, &options);
    if (maage_status_failed(operation)) {
        failure_message = "Invalid world, seed, worker, radius, or gameplay options.";
        goto done;
    }
    if (!instance) {
        operation = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SELECT, 0);
        failure_message = "No validated instance was selected at startup.";
        goto done;
    }
    operation = maage_controls_create(&controls);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_options_load(instance, &video_options, controls);
    if (maage_status_failed(operation)) maage_log_status(MAAGE_LOG_WARNING, "maage.client.options", operation, "Unable to read config/options.ini; using defaults.");
    saved_video_options = video_options;
    if (options.radius_specified) video_options.render_distance = options.radius;
    else options.radius = video_options.render_distance;
    strcpy(directory, maage_client_storage_data_root());
    maage_log_info("maage.client.instance", "Instance: %s (%s).", maage_instance_get_info(instance)->name, maage_instance_get_info(instance)->id);
    result = maage_window_system_create(MAAGE_WINDOW_AUTO, &system, trace);
    if (maage_status_failed(result)) goto window_failed;
    maage_window_config_init(&window_config);
    /* Platform title APIs receive the same immutable release/build identity. */
    sprintf(window_title, "MaaGE %.112s", maage_client_identity()->version);
    window_config.title = window_title;
    window_config.visible = 0;
    /* Enables the notch-capable drawable only for borderless/fullscreen;
     * ordinary macOS Windowed mode still keeps its opaque native title bar. */
    window_config.full_display_content = 1;
    result = maage_window_create(system, &window_config, &window, trace);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
    maage_status_trace_init(trace);
#ifdef MAAGE_CLIENT_MACOS_DOCK_NAME
    operation = maage_client_dock_name(trace);
    if (!maage_status_is_success(operation)) MAAGE_LOG_VERBOSE_EVENT("maage.client.desktop", maage_log_trace_status(MAAGE_LOG_VERBOSE, "maage.client.desktop", operation, trace, "Optional Dock display name unavailable; retaining the system label."));
    maage_status_trace_init(trace);
#endif
    operation = maage_client_resources_set_window_icon(directory, window, trace);
    if (!maage_status_is_success(operation)) maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.resources", operation, trace, "Client icon unavailable; retaining the platform default. Reconstructed icons: %s/minecraft/assets/reconst/icons", directory);
    maage_status_trace_init(trace);
    render_config.window = window;
    render_config.required_features |= MAAGE_RENDER_CAP_TEXTURED_MESH | MAAGE_RENDER_CAP_UI | MAAGE_RENDER_CAP_SCENE;
    rendered = maage_renderer_create(&render_config, &renderer, trace);
    if (maage_status_failed(rendered)) goto done;
    operation = maage_client_splash_begin(&splash, system, window, renderer, directory, video_options.gui_scale, trace);
    if (maage_status_failed(operation)) goto done;
    resource_notice = splash.resource_status;
    maage_application_content_config(&content_registration);
    content_registration.execution.side = MAAGE_EXECUTION_CLIENT;
    content_registration.execution.capabilities = MAAGE_EXECUTION_PRESENTATION;
    operation = maage_content_create(&content_registration, &content, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_resources_load(directory, content, &resources, maage_client_storage_player(), maage_client_splash_update, &splash, trace);
    if (maage_status_failed(operation)) goto done;
    if (maage_status_is_success(resource_notice)) resource_notice = resources.load_status;
    maage_status_trace_init(trace);
    blocks = maage_content_view_blocks(resources.view);
    items = maage_content_items(content);
    rendered = maage_renderer_create_texture(renderer, &resources.atlas, &atlas, trace);
    if (maage_status_code(rendered) != MAAGE_STATUS_SUCCESS) goto done;
    operation = maage_client_interaction_init(&interaction, &resources);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_item_entities_create(renderer, atlas, &resources, items, blocks, &item_entities, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_first_person_init(&first_person, &resources);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_splash_update(&splash, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_ui_create(renderer, window, directory, &resources, &ui, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_ui_set_content(ui, items, blocks, maage_content_creative(content), resources.inventory_grass_color, resources.uv, resources.texture_count);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_frontend_create(ui, instance, content, &frontend, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_ui_finalize(ui);
    if (maage_status_failed(operation)) goto done;
    if (maage_status_is_success(resource_notice)) resource_notice = maage_client_ui_resource_status(ui);
    operation = maage_client_splash_update(&splash, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_environment_create(renderer, directory, &resources, &environment_view, trace);
    /* Both GUI and environment copy from the initialization-only CPU atlas. */
    (void) maage_image_destroy(&resources.image);
    resources.atlas.pixels = NULL;
    if (maage_status_failed(operation)) {
        failure_message = "Environment presentation initialization failed.";
        goto done;
    }
    if (maage_status_is_success(resource_notice)) resource_notice = maage_client_environment_resource_status(environment_view, NULL);
    /* The service remains valid in silent-device recovery. Rendering and the
     * authoritative Server must not depend on sound playback succeeding. */
    operation = maage_client_splash_update(&splash, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_audio_native_install();
    if (maage_status_failed(operation)) {
        maage_log_status(MAAGE_LOG_WARNING, "maage.client.audio", operation, "Audio component registration failed; falling back to silent service.");
    }
    operation = maage_client_audio_service_create(directory, &video_options, maage_content_sounds(content), &audio, trace);
    if (maage_status_failed(operation) && !audio) {
        maage_log_status(MAAGE_LOG_WARNING, "maage.client.audio", operation, "Audio setup failed; gameplay presentation continues.");
        maage_status_trace_init(trace);
    } else if (maage_status_is_warning(operation)) {
        audio_notice = operation;
        maage_status_trace_init(trace);
    }
    if (audio) maage_client_ui_set_click_callback(ui, maage_client_audio_click, audio);
    direct_world = options.id || options.create || options.seeded;
    result = maage_window_set_visible(window, 1);
    if (maage_status_failed(result)) goto window_failed;
    operation = maage_client_ui_apply_options(ui, &video_options);
    if (maage_status_failed(operation)) goto done;
    options_ready = 1;
    logged_max_fps = maage_client_ui_max_fps(ui);
    logged_vsync = maage_client_ui_vsync(ui);
    operation = maage_client_splash_update(&splash, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_splash_destroy(&splash);
    if (maage_status_failed(operation)) goto done;
    rendered = maage_renderer_set_clear_color(renderer, 0, 0, 0, 1);
    if (maage_status_failed(rendered)) goto done;
    operation = maage_content_load_complete(content, trace);
    if (!maage_status_failed(operation)) operation = maage_content_initialize(content, trace);
    if (maage_status_failed(operation)) goto done;
choose_world:
    player_view.initialized = 0;
    memset(&collision, 0, sizeof(collision));
    fov.initialized = 0;
    if (!direct_world) {
        operation = maage_client_frontend_run(frontend, system, audio, &choice, trace);
        if (maage_status_code(operation) == MAAGE_STATUS_NOOP) {
            ok = 1;
            goto done;
        }
        if (maage_status_failed(operation)) goto done;
        if (retired_in_shutdown) {
            maage_client_resources_destroy(&retired_resources);
            retired_in_shutdown = 0;
        }
        maage_client_ui_get_options(ui, &video_options);
        operation = maage_client_options_store(frontend, ui, &saved_video_options, 1);
        if (maage_status_failed(operation)) goto done;
        options.id = choice.create ? NULL : choice.id;
        options.name = choice.create ? choice.name : NULL;
        options.create = choice.create;
        options.mode = choice.create ? choice.mode : -1;
        options.seeded = choice.create && choice.seed[0];
        if (options.seeded) {
            operation = maage_world_seed_parse(choice.seed, &options.seed);
            if (maage_status_failed(operation)) goto done;
        }
    }
    direct_world = 0;
    options.radius = video_options.render_distance;
    has_last_input = 0;
    memset(&outbox, 0, sizeof(outbox));
    outbox.diagnostics = maage_log_enabled("maage.client.latency", MAAGE_LOG_VERBOSE) && getenv("MAAGE_WORLD_DIAGNOSTICS") && !strcmp(getenv("MAAGE_WORLD_DIAGNOSTICS"), "1");
    memset(&mesh_activity, 0, sizeof(mesh_activity));
    world_epoch = last_sound_sequence = last_seen_tick = sound_epoch = sound_world_epoch = 0;
    active = was_active = was_modal = was_focused = was_changing_mode = 0;
    paused_command = -1;
    view_sent = disconnected = 0;
    initial_capture = 1;
    jump_latched = attack_latched = use_latched = respawn_latched = forward_latched = drop_latched = 0;
    pacing.deadline = 0;
    pacing.fps = 0;
    operation = maage_client_frontend_loading(frontend, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_local_session_create(256U, 256U, 512U, &session);
    if (maage_status_failed(operation)) goto done;
    server_config.instance_root = maage_instance_directory(instance);
    server_config.world_id = options.id;
    server_config.world_name = options.name;
    server_config.create_world = options.create;
    server_config.has_seed = options.seeded;
    server_config.seed = options.seed;
    server_config.world_workers = options.workers;
    server_config.render_distance = options.radius;
    server_config.initial_game_mode = options.mode;
    server_config.interaction_range = options.reach;
    server_config.execution_mode = MAAGE_SERVER_INTEGRATED;
    server_config.content_peer = content;
    server_config.session = session;
    operation = maage_server_runtime_create(&server_config, &server, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_server_runtime_start(server, trace);
    if (maage_status_failed(operation)) goto done;
    server_started = 1;
    /* Poll AppKit in bounded slices while the authoritative thread begins.
     * An explicit overall timeout detects a stalled bootstrap instead of
     * trapping a hidden window indefinitely. */
    startup_began = maage_time_monotonic_seconds();
    if (startup_began < 0.0) goto clock_failed;
    for (;;) {
        result = maage_window_poll_events(system, 0);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        result = maage_window_get_state(window, &state);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        if (state.close_requested) {
            ok = 1;
            goto done;
        }
        operation = maage_local_session_read_snapshot(session, &incoming);
        if (maage_status_code(operation) == MAAGE_STATUS_SUCCESS && incoming.protocol_version == MAAGE_SESSION_PROTOCOL_VERSION && incoming.sequence) {
            latest = previous = incoming;
            world_epoch = latest.world_epoch;
            last_seen_tick = latest.tick;
            selected_hotbar = queued_hotbar = (int) latest.player.inventory.selected;
            valid_snapshot = 1;
        }
        if (maage_status_failed(operation) && maage_status_code(operation) != MAAGE_STATUS_NOOP) goto done;
        if (maage_server_runtime_finished(server)) {
            operation = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_START, 0);
            failure_message = "The integrated server exited before becoming ready.";
            goto done;
        }
        if (valid_snapshot && maage_server_runtime_ready(server)) {
            connected = 1;
            break;
        }
        now = maage_time_monotonic_seconds();
        if (now < 0.0) goto clock_failed;
        if (now - startup_began >= 120.0) {
            operation = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_START, 0);
            failure_message = "Integrated server startup timed out before readiness.";
            goto done;
        }
        operation = maage_client_frontend_loading(frontend, trace);
        if (maage_status_failed(operation)) goto done;
        result = maage_window_wait_events(system, 1);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
    }
    memset(&local_swing, 0, sizeof(local_swing));
    memset(&food_animation, 0, sizeof(food_animation));
    memset(&visual_stats, 0, sizeof(visual_stats));
    visual_stats_time = -1;
    maage_world_config_init(&visual_config);
    visual_config.registry = blocks;
    visual_config.items = items;
    visual_config.execution = *maage_content_execution(content);
    visual_config.biomes = maage_content_view_biomes(resources.view);
    visual_config.presentation_only = 1;
    visual_config.simulation_distance = 0;
    visual_config.random_tick_speed = 0;
    visual_config.worker_count = options.workers;
    visual_config.render_distance = options.radius;
    {
        maage_local_session_backlog backlog;
        operation = maage_local_session_get_backlog(session, &backlog);
        if (maage_status_failed(operation)) goto done;
        operation = maage_client_replica_capacity(&latest.world_stats, NULL, backlog.column_capacity, &visual_config.cache_capacity);
        if (maage_status_failed(operation)) goto done;
    }
    memcpy(visual_config.position, latest.player.player.position, sizeof(visual_config.position));
    operation = maage_world_create(&visual_config, &visual_world, trace);
    if (maage_status_failed(operation)) goto done;
    while (maage_client_ui_modal(ui))
        (void) maage_client_ui_back(ui);
    operation = maage_client_ui_set_snapshot(ui, &latest);
    if (maage_status_failed(operation)) goto done;
    rendered = maage_renderer_set_clear_color(renderer, 0.53f, 0.76f, 1.0f, 1.0f);
    if (maage_status_code(rendered) != MAAGE_STATUS_SUCCESS) goto done;
    /* The Client already presented its title/loading screens in this same
     * visible window. prepare_frame is strictly for priming a hidden window;
     * calling it after world startup rejects the visible window and aborts
     * the Client with RENDER/PRESENT INVALID_ARGUMENT. The existing renderer
     * and drawable are reused; the next normal world frame picks up the new
     * sky clear color without a second visibility transition. */
    operation = maage_client_ui_apply_options(ui, &video_options);
    if (maage_status_failed(operation)) maage_log_status(MAAGE_LOG_WARNING, "maage.client.options", operation, "Some saved video choices could not be applied.");
    options_ready = 1;
    yaw = latest.player.yaw;
    pitch = latest.player.pitch;
    last = maage_time_monotonic_seconds();
    if (last < 0.0) goto clock_failed;
    received_time = diagnostic_time = mesh_activity.sample_start = last;
    maage_client_ui_notify(ui, resource_notice, last);
    if (audio_notice) maage_client_ui_notify(ui, audio_notice, last);
    maage_log_info("maage.client", "Connected to integrated server.");
    for (;;) {
        maage_status_trace_init(trace);
        MAAGE_STATUS_TRACE(trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
        result = maage_window_poll_events(system, 0);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        result = maage_window_get_state(window, &state);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        state.resized = state.resized || resize_pending;
        resize_pending = 0;
        if (state.close_requested) {
            ok = 1;
            break;
        }
        result = maage_window_get_input(window, &input);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        now = maage_time_monotonic_seconds();
        if (now < 0.0) goto clock_failed;
        raw_elapsed = now - last;
        last = now;
        if (raw_elapsed < 0.0) raw_elapsed = 0.0;
        if (!retired_in_shutdown && retired_resources.view && !maage_world_presentation_borrowed(visual_world, maage_content_view_blocks(retired_resources.view))) maage_client_resources_destroy(&retired_resources);
        if (input.focused && input.keys[MAAGE_INPUT_KEY_F3] && input.pressed[MAAGE_INPUT_KEY_T]) {
            operation = maage_client_reload_resources(system, directory, content, renderer, ui, visual_world, &resources, &retired_resources, &atlas, &environment_view, &item_entities, &cache, &interaction, &first_person, &player_view, &prediction, trace);
            blocks = maage_content_view_blocks(resources.view);
            maage_client_ui_notify(ui, operation, now);
            maage_status_trace_init(trace);
            input.pressed[MAAGE_INPUT_KEY_T] = 0;
        }
        maage_controls_observe(controls, &input);
        was_modal = maage_client_ui_modal(ui);
        was_options_visible = maage_client_ui_options_visible(ui);
        memset(&ui_input, 0, sizeof(ui_input));
        ui_input.raw = &input;
        ui_input.scroll = input.scroll_y;
        if (input.focused) {
            ui_input.escape = input.pressed[MAAGE_INPUT_KEY_ESCAPE];
            ui_input.f3 = input.pressed[MAAGE_INPUT_KEY_F3];
            ui_input.f11 = input.pressed[MAAGE_INPUT_KEY_F11];
            ui_input.accept = input.pressed[MAAGE_INPUT_KEY_ENTER];
            ui_input.previous = input.pressed[MAAGE_INPUT_KEY_UP];
            ui_input.next = input.pressed[MAAGE_INPUT_KEY_TAB] || input.pressed[MAAGE_INPUT_KEY_DOWN];
            ui_input.left = input.pressed[MAAGE_INPUT_KEY_LEFT];
            ui_input.right = input.pressed[MAAGE_INPUT_KEY_RIGHT];
            ui_input.primary_down = input.primary_down;
            ui_input.primary_pressed = input.primary_pressed;
            ui_input.secondary_pressed = input.secondary_pressed;
            ui_input.inventory = maage_control_gui_pressed(controls, controls->inventory, &input);
            ui_input.shift = input.keys[MAAGE_INPUT_KEY_SHIFT];
            ui_input.text = input.text;
        }
        ui_input.mouse_x = (float) input.mouse_x;
        ui_input.mouse_y = (float) input.mouse_y;
        operation = maage_client_ui_set_safe_area(ui, &state.safe_area);
        if (maage_status_failed(operation)) goto done;
        operation = maage_client_ui_set_window_state(ui, &state);
        if (maage_status_failed(operation)) goto done;
        ui_actions = 0;
        operation = maage_client_ui_update(ui, &ui_input, state.framebuffer_width, state.framebuffer_height, state.width, state.height, &ui_actions);
        if (maage_status_failed(operation)) {
            maage_client_ui_notify(ui, operation, now);
            maage_log_status(MAAGE_LOG_WARNING, "maage.client.ui", operation, "Presentation UI rejected an action.");
        }
        changing_mode = maage_client_ui_window_mode_changing(ui);
        if (was_options_visible && !maage_client_ui_options_visible(ui)) options_save_needed = 1;
        if (was_changing_mode && !changing_mode) {
            initial_capture = 1;
            options_save_needed = 1;
        }
        if (options_save_needed && !changing_mode) {
            operation = maage_client_options_store(frontend, ui, &saved_video_options, 0);
            options_save_needed = 0;
            if (maage_status_failed(operation)) {
                maage_client_ui_notify(ui, operation, now);
                maage_log_status(MAAGE_LOG_WARNING, "maage.client.options", operation, "Unable to save config/options.ini.");
            }
        }
        video_options.render_distance = maage_client_ui_render_distance(ui);
        if (options.radius != video_options.render_distance) {
            memory_reclaim_after = video_options.render_distance < options.radius ? now + 3.0 : -1.0;
            options.radius = video_options.render_distance;
            view_sent = 0;
        }
        if (!changing_mode && !was_changing_mode && ((was_focused && (!input.focused || input.focus_lost)) || (was_active && !input.captured))) {
            if (!maage_client_ui_modal(ui)) (void) maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
        }
        was_changing_mode = changing_mode;
        was_focused = input.focused;
        if (ui_actions & MAAGE_CLIENT_UI_EXIT) goto return_to_title;
        if (maage_client_ui_modal(ui)) {
            initial_capture = 0;
            if (input.captured) result = maage_window_capture_mouse(window, 0);
            input.captured = 0;
        } else if (!changing_mode && input.focused && !input.captured && (initial_capture || was_modal || input.primary_pressed)) {
            result = maage_window_capture_mouse(window, 1);
            initial_capture = 0;
            if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
                input.captured = 1;
                input.mouse_delta_x = input.mouse_delta_y = 0;
            }
        }
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        active = !maage_client_ui_modal(ui) && !changing_mode &&
                 input.focused && input.captured && state.visible &&
                 state.framebuffer_width > 0 && state.framebuffer_height > 0;
        if (!active || was_modal || !was_active) maage_controls_block(controls, &input);
        if (active && maage_control_pressed(controls, controls->perspective, &input)) camera_mode = (camera_mode + 1U) % 3U;
        was_active = active;
        if (logged_max_fps != maage_client_ui_max_fps(ui) || logged_vsync != maage_client_ui_vsync(ui)) {
            logged_max_fps = maage_client_ui_max_fps(ui);
            logged_vsync = maage_client_ui_vsync(ui);
            maage_log_info("maage.client.options", "FPS=%d (zero unlimited); VSync=%s.", logged_max_fps, logged_vsync ? "on" : "off");
        }
        /* First advance queued reliable commands; transport queue-full is
         * retained as backpressure and never treated as successful delivery. */
        operation = maage_client_outbox_flush(&outbox, session);
        if (maage_status_failed(operation) && maage_status_code(operation) != MAAGE_STATUS_BUFFER_OVERFLOW) goto done;
        if (ui_actions & MAAGE_CLIENT_UI_RESPAWN) respawn_latched = 1;
        if (respawn_latched) {
            memset(&command, 0, sizeof(command));
            command.kind = MAAGE_CMD_RESPAWN;
            operation = maage_client_outbox_queue(&outbox, &command);
            if (!maage_status_failed(operation)) {
                respawn_latched = 0;
                (void) maage_client_ui_back(ui);
            } else
                maage_client_ui_notify(ui, operation, now);
        }
        for (i = 0; i < 64 && outbox.count < MAAGE_CLIENT_OUTBOX_CAPACITY; ++i) {
            operation = maage_client_ui_take_command(ui, &command);
            if (maage_status_code(operation) == MAAGE_STATUS_NOOP) break;
            if (maage_status_failed(operation)) {
                maage_client_ui_notify(ui, operation, now);
                break;
            }
            operation = maage_client_outbox_queue(&outbox, &command);
            if (maage_status_failed(operation)) {
                /* The GUI request was taken but never submitted. Return it to
                 * the UI FIFO head so retries retain causal ordering. */
                (void) maage_client_ui_retry_command(ui, &command);
                maage_client_ui_notify(ui, operation, now);
                break;
            }
            maage_client_ui_command_submitted(ui, &command, outbox.next_sequence);
        }
        if (paused_command != (maage_client_ui_pauses_game(ui) ? 1 : 0) && outbox.count < MAAGE_CLIENT_OUTBOX_CAPACITY) {
            memset(&command, 0, sizeof(command));
            command.kind = MAAGE_CMD_PAUSE;
            command.paused = maage_client_ui_pauses_game(ui) ? 1 : 0;
            operation = maage_client_outbox_queue(&outbox, &command);
            if (!maage_status_failed(operation)) paused_command = command.paused;
        }
        if (!view_sent) {
            if (outbox.count < MAAGE_CLIENT_OUTBOX_CAPACITY) {
                memset(&command, 0, sizeof(command));
                command.kind = MAAGE_CMD_VIEW;
                command.render_radius = options.radius;
                command.view_yaw = yaw;
                operation = maage_client_outbox_queue(&outbox, &command);
                if (!maage_status_failed(operation)) {
                    view_sent = 1;
                }
            }
        }
        if (active) {
            double factor, radians;
            maage_client_ui_get_options(ui, &current_options);
            factor = (double) current_options.mouse_sensitivity * 0.006 + 0.2;
            /* Cubic sensitivity, then 0.15 degrees per scaled native delta.
             * No elapsed-time or GUI-scale factor belongs in relative motion. */
            radians = factor * factor * factor * 8.0 * 0.15 * 0.017453292519943295;
            yaw = fmod(yaw + input.mouse_delta_x * radians, 6.283185307179586);
            pitch += input.mouse_delta_y * radians * (current_options.invert_mouse ? -1 : 1);
            if (pitch > MAAGE_CLIENT_PITCH_LIMIT) pitch = MAAGE_CLIENT_PITCH_LIMIT;
            if (pitch < -MAAGE_CLIENT_PITCH_LIMIT) pitch = -MAAGE_CLIENT_PITCH_LIMIT;
        }
        if (active) {
            for (i = 0; i < (int) MAAGE_HOTBAR_SLOTS; ++i) {
                if (maage_control_pressed(controls, controls->hotbar[i], &input)) selected_hotbar = i;
            }
            wheel += input.scroll_y;
            if (wheel >= 1.0 || wheel <= -1.0) {
                i = (int) wheel;
                selected_hotbar = (selected_hotbar - i + 900) % (int) MAAGE_HOTBAR_SLOTS;
                wheel -= (double) i;
            }
            if (selected_hotbar != queued_hotbar) {
                memset(&command, 0, sizeof(command));
                command.kind = MAAGE_CMD_HOTBAR;
                command.hotbar_slot = (unsigned int) selected_hotbar;
                operation = maage_client_outbox_queue(&outbox, &command);
                if (maage_status_failed(operation)) maage_client_ui_notify(ui, operation, now);
                else
                    queued_hotbar = selected_hotbar;
            }
            if (maage_control_pressed(controls, controls->drop, &input) && !drop_latched) {
                drop_latched = 1;
                drop_slot = (unsigned int) selected_hotbar;
                drop_stack = input.keys[MAAGE_INPUT_KEY_CONTROL] != 0;
            }
            if (drop_latched) {
                memset(&command, 0, sizeof(command));
                command.kind = MAAGE_CMD_DROP;
                command.inventory_slot = drop_slot;
                command.secondary_click = drop_stack;
                operation = maage_client_outbox_queue(&outbox, &command);
                if (maage_status_failed(operation)) maage_client_ui_notify(ui, operation, now);
                else drop_latched = 0;
            }
        } else {
            drop_latched = 0;
            wheel = 0.0;
        }
        if (active) {
            if (maage_control_pressed(controls, controls->jump, &input)) jump_latched = 1;
            if (maage_control_pressed(controls, controls->forward, &input) && !maage_control_down(controls, controls->backward, &input)) forward_latched = 1;
            if (maage_control_pressed(controls, controls->attack, &input)) attack_latched = 1;
            if (maage_control_pressed(controls, controls->use, &input)) use_latched = 1;
        } else {
            jump_latched = attack_latched = use_latched = forward_latched = 0;
        }
        action_snapshot = latest;
        if (outbox.count < MAAGE_CLIENT_OUTBOX_CAPACITY) {
            memset(&command, 0, sizeof(command));
            command.kind = MAAGE_CMD_INPUT;
            command.sampled_at = outbox.diagnostics ? now : -1;
            command.intent.movement.yaw = yaw;
            command.intent.pitch = pitch;
            if (active) {
                command.intent.movement.forward = (double) (maage_control_down(controls, controls->forward, &input) - maage_control_down(controls, controls->backward, &input));
                command.intent.movement.strafe = (double) (maage_control_down(controls, controls->right, &input) - maage_control_down(controls, controls->left, &input));
                command.intent.movement.jump = maage_control_down(controls, controls->jump, &input);
                command.intent.movement.sprint = maage_control_down(controls, controls->sprint, &input);
                command.intent.forward_pressed = forward_latched;
                command.intent.jump_pressed = jump_latched;
                command.intent.descend = maage_control_down(controls, controls->sneak, &input);
                command.intent.attack = maage_control_down(controls, controls->attack, &input) || attack_latched;
                command.intent.use = maage_control_down(controls, controls->use, &input) || use_latched;
                command.intent.attack_pressed = attack_latched;
                command.intent.use_pressed = use_latched;
                if (command.intent.use) command.use_snapshot = action_snapshot.sequence;
            }
            if (!has_last_input || !maage_client_input_unchanged(&command, &last_input)) {
                operation = maage_client_outbox_queue(&outbox, &command);
                if (maage_status_failed(operation)) maage_client_ui_notify(ui, operation, now);
                else {
                    last_input = command;
                    has_last_input = 1;
                    jump_latched = attack_latched = use_latched = forward_latched = 0;
                }
            }
        } else {
            /* The input edge latches above are retained until an ordered
             * command can be queued; never acknowledge an unsent click. */
            maage_client_ui_notify(ui, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 0), now);
        }
        /* Resolve this frame's action from copied state before submitting
         * its input. Its own instant Server break must not retarget the press
         * onto the next block before local prediction observes that press. */
        operation = maage_local_session_read_snapshot(session, &incoming);
        if (maage_status_code(operation) == MAAGE_STATUS_SUCCESS &&
            incoming.protocol_version == MAAGE_SESSION_PROTOCOL_VERSION &&
            (incoming.session_epoch != latest.session_epoch ||
             incoming.world_epoch != latest.world_epoch ||
             incoming.sequence > latest.sequence)) {
            if (incoming.session_epoch != latest.session_epoch || incoming.world_epoch != latest.world_epoch) {
                (void) maage_client_mesh_cache_clear(&cache, visual_world);
                previous = incoming;
                received_time = now;
                memset(&collision, 0, sizeof(collision));
                memset(&camera_effects, 0, sizeof(camera_effects));
                player_view.initialized = 0;
                world_epoch = incoming.world_epoch;
                view_sent = 0;
                sound_epoch = sound_world_epoch = last_sound_sequence = 0;
                operation = maage_client_audio_service_reset_world(audio, trace);
                if (maage_status_failed(operation)) goto done;
                yaw = incoming.player.yaw;
                pitch = incoming.player.pitch;
                selected_hotbar = queued_hotbar = (int) incoming.player.inventory.selected;
                jump_latched = attack_latched = use_latched = respawn_latched = forward_latched = drop_latched = 0;
            } else if (incoming.player.motion.life != latest.player.motion.life) {
                previous = incoming;
                received_time = now;
                memset(&camera_effects, 0, sizeof(camera_effects));
                player_view.initialized = 0;
            } else if (incoming.tick != latest.tick) {
                previous = latest;
                received_time = now;
            }
            latest = incoming;
            was_modal = maage_client_ui_modal(ui);
            operation = maage_client_ui_set_snapshot(ui, &latest);
            if (maage_status_failed(operation)) goto done;
            if (was_modal && !maage_client_ui_modal(ui)) initial_capture = 1;
            if (!latest.player.health && latest.player.game_mode == MAAGE_GAME_MODE_SURVIVAL && !maage_client_ui_modal(ui)) (void) maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
        } else if (maage_status_failed(operation) && maage_status_code(operation) != MAAGE_STATUS_NOOP && maage_status_code(operation) != MAAGE_STATUS_INVALID_STATE) goto done;
        /* Anchor a new life at its first copied snapshot before FIFO events
         * can run ahead of that snapshot during an owner catch-up phase. */
        if (!camera_effects.initialized) {
            operation = maage_client_camera_observe(&camera_effects, latest.session_epoch, latest.world_epoch, latest.tick, &latest.player.motion);
            if (maage_status_failed(operation)) goto done;
        }
        for (event_budget = 0; event_budget < 256U; ++event_budget) {
            operation = maage_local_session_take_event(session, &event);
            if (maage_status_code(operation) == MAAGE_STATUS_NOOP || maage_status_code(operation) == MAAGE_STATUS_INVALID_STATE) break;
            if (maage_status_failed(operation)) goto done;
            if (event.protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) continue;
            if (event.kind == MAAGE_EVENT_DISCONNECTED) {
                disconnected = 1;
                if (maage_status_failed(event.status)) {
                    outcome = event.status;
                    if (event.has_trace && trace) *trace = event.trace;
                } else
                    ok = 1;
                break;
            }
            if (event.session_epoch != latest.session_epoch || event.world_epoch != latest.world_epoch) continue;
            if (event.kind == MAAGE_EVENT_PLAYER_MOTION) {
                if (event.motion.life == latest.player.motion.life) {
                    operation = maage_client_camera_observe(&camera_effects, event.session_epoch, event.world_epoch, event.tick, &event.motion);
                    if (maage_status_failed(operation)) goto done;
                    if (maage_status_is_warning(operation)) maage_client_ui_notify(ui, operation, now);
                }
            } else if (event.kind == MAAGE_EVENT_BLOCK_ACTION) {
                maage_client_prediction_confirm(&prediction, &event);
            } else if (event.kind == MAAGE_EVENT_SOUND || event.kind == MAAGE_EVENT_PICKUP) {
                if (sound_epoch != event.session_epoch || sound_world_epoch != event.world_epoch) {
                    last_sound_sequence = 0;
                    sound_epoch = event.session_epoch;
                    sound_world_epoch = event.world_epoch;
                }
                if (event.sequence > last_sound_sequence) {
                    last_sound_sequence = event.sequence;
                    if ((latest.tick <= event.tick || latest.tick - event.tick <= MAAGE_CLIENT_SOUND_MAX_AGE_TICKS) && (event.emitted_at < 0 || now - event.emitted_at <= MAAGE_CLIENT_SOUND_MAX_AGE_SECONDS)) {
                        if (event.kind == MAAGE_EVENT_PICKUP) (void) maage_client_item_entities_pickup(item_entities, &event, now);
                        if (maage_status_code(maage_client_prediction_observe_sound(&prediction, blocks, &event)) == MAAGE_STATUS_SUCCESS) continue;
                        if (audio) (void) maage_client_audio_service_event(audio, &event.sound, trace);
                    }
                }
            } else if (event.kind == MAAGE_EVENT_STATUS) {
                maage_client_ui_notify(ui, event.status, now);
                if (maage_status_failed(event.status)) maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.server", event.status, event.has_trace ? &event.trace : NULL, "Authoritative Server reported an operation failure.");
            }
        }
        if (disconnected) break;
        if (maage_server_runtime_finished(server)) {
            /* Drain any terminal STATUS/DISCONNECT next frame; a closed
             * Server without a terminal event is not successful gameplay. */
            operation = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_STOP, 0);
            failure_message = "The authoritative Server stopped unexpectedly.";
            goto done;
        }
        cx = maage_world_chunk_coordinate((int) floor(latest.player.player.position[0]));
        cz = maage_world_chunk_coordinate((int) floor(latest.player.player.position[2]));
        {
            unsigned int received = 0, replica_capacity;
            maage_local_session_backlog backlog;
            double receive_begin = maage_time_monotonic_seconds(), apply_begin;
            /* Retain all acknowledged bases until ordered unloads arrive,
             * including Server residency growth after a distance change. */
            operation = maage_local_session_get_backlog(session, &backlog);
            if (maage_status_failed(operation)) goto done;
            operation = maage_client_replica_capacity(&latest.world_stats, visual_world, backlog.column_capacity, &replica_capacity);
            if (maage_status_failed(operation)) goto done;
            operation = maage_world_set_render_distance(visual_world, options.radius, replica_capacity);
            if (maage_status_failed(operation)) goto done;
            while (received++ < 16U) {
                memset(&column_update, 0, sizeof(column_update));
                operation = maage_local_session_take_column(session, cx, cz, &column_update);
                if (maage_status_code(operation) == MAAGE_STATUS_NOOP || maage_status_code(operation) == MAAGE_STATUS_INVALID_STATE) break;
                if (maage_status_failed(operation)) goto done;
                apply_begin = prediction.diagnostics ? maage_time_monotonic_seconds() : -1;
                if (column_update.world_epoch == world_epoch) {
                    operation = maage_world_apply_column(visual_world, column_update.column);
                    if (maage_status_failed(operation)) {
                        outcome = MAAGE_STATUS_PROPAGATE(trace, operation, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SNAPSHOT);
                        sprintf(failure_detail, "Client replica rejected column %d,%d; revision=%lu; base=%lu; resident=%u; capacity=%u.", column_update.x, column_update.z, column_update.revision, column_update.column ? column_update.column->base_revision : 0UL, maage_world_resident_columns(visual_world), replica_capacity);
                        failure_message = failure_detail;
                    }
                    if (maage_status_code(operation) == MAAGE_STATUS_SUCCESS && (column_update.column->unload || !column_update.column->base_revision)) {
                        size_t index;
                        for (index = 0; index < cache.capacity; ++index)
                            if (cache.slots[index].used && cache.slots[index].x == column_update.x && cache.slots[index].z == column_update.z) maage_client_mesh_cache_drop(&cache, index, visual_world);
                    }
                }
                if (prediction.diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "column=%d,%d revision=%lu local_transport_ms=%.3f applied_at=%.6f receive_apply_ms=%.3f", column_update.x, column_update.z, column_update.revision, column_update.offered_at >= 0 ? (maage_time_monotonic_seconds() - column_update.offered_at) * 1000 : -1, maage_time_monotonic_seconds(), (maage_time_monotonic_seconds() - apply_begin) * 1000));
                maage_column_update_destroy(column_update.column);
                if (maage_status_failed(operation)) goto done;
                if ((maage_time_monotonic_seconds() - receive_begin) * 1000.0 >= 2.0) break;
            }
            if (prediction.diagnostics && latest.tick != prediction.backlog_tick && latest.tick % 20 == 0) {
                maage_local_session_backlog backlog;
                prediction.backlog_tick = latest.tick;
                if (maage_status_is_success(maage_local_session_get_backlog(session, &backlog))) MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "tick=%lu command_backlog=%lu event_backlog=%lu column_backlog=%lu", latest.tick, (unsigned long) backlog.commands, (unsigned long) backlog.events, (unsigned long) backlog.columns));
            }
            video_options.leaves = maage_client_ui_leaves_mode(ui);
            video_options.biome_blend = maage_client_ui_biome_blend(ui);
            operation = maage_world_set_biome_blend(visual_world, video_options.biome_blend);
            if (maage_status_failed(operation)) goto done;
            operation = maage_world_set_leaves_mode(visual_world, video_options.leaves);
            if (maage_status_failed(operation)) goto done;
        }
        {
            int retired = maage_client_mesh_cache_retire(&cache, visual_world, cx, cz, options.radius, maage_client_mesh_budget(&options, &debug, &mesh_activity));
            /* Pages freed by the Client and the integrated Server may remain
             * in malloc zones. Let the Server retire its old footprint first. */
            if (retired && memory_reclaim_after >= 0.0 && now >= memory_reclaim_after &&
                latest.world_stats.resident_columns <= latest.world_stats.cache_capacity) {
                maage_client_debug_release_unused_memory();
                memory_reclaim_after = -1.0;
            }
        }
        {
            double prediction_eye[3], prediction_direction[3];
            maage_world_target prediction_block;
            maage_status delivery;
            int preview_queued = 0, entity_target;
            for (i = 0; i < 3; ++i)
                prediction_eye[i] = latest.player.player.position[i];
            prediction_eye[1] += MAAGE_PLAYER_EYE_HEIGHT;
            delivery = maage_client_item_entities_receive(item_entities, session, &latest, now);
            if (maage_status_failed(delivery)) {
                operation = delivery;
                goto done;
            }
            prediction_direction[0] = sin(yaw) * cos(pitch);
            prediction_direction[1] = -sin(pitch);
            prediction_direction[2] = -cos(yaw) * cos(pitch);
            delivery = maage_world_raycast(visual_world, prediction_eye, prediction_direction, latest.player.reach, &prediction_block);
            if (maage_status_failed(delivery) && maage_status_code(delivery) != MAAGE_STATUS_NOT_FOUND) {
                operation = delivery;
                goto done;
            }
            entity_target = maage_client_item_entities_target(item_entities, &latest, prediction_eye, prediction_direction, prediction_block.distance, now);
            if (prediction_mesh_generation != cache.generation) {
                prediction_mesh_generation = cache.generation;
                prediction.dirty = 1;
            }
            operation = maage_client_prediction_update(&prediction, items, blocks, visual_world, &latest, &action_snapshot, cache.slots, cache.capacity, prediction_eye, yaw, pitch, raw_elapsed, now, outbox.input_sequence, video_options.leaves, video_options.biome_blend, active && action_snapshot.player.motion.life == latest.player.motion.life && outbox.count < MAAGE_CLIENT_OUTBOX_CAPACITY, maage_control_down(controls, controls->attack, &input) || maage_control_pressed(controls, controls->attack, &input), maage_control_pressed(controls, controls->attack, &input), maage_control_down(controls, controls->use, &input) || maage_control_pressed(controls, controls->use, &input), maage_control_pressed(controls, controls->use, &input), maage_control_down(controls, controls->sneak, &input), (unsigned int) selected_hotbar, entity_target, &command);
            if (!maage_status_failed(operation) && command.kind) {
                command.sampled_at = prediction.diagnostics ? prediction.input_at : -1;
                delivery = maage_client_outbox_queue(&outbox, &command);
                if (maage_status_failed(delivery)) {
                    operation = delivery;
                    goto done;
                }
                preview_queued = 1;
            }
            /* Flush input and its marker before replica streaming and GPU
             * preparation. Resource failure cannot hold up gameplay commands. */
            delivery = maage_client_outbox_flush(&outbox, session);
            if (maage_status_failed(delivery) && maage_status_code(delivery) != MAAGE_STATUS_BUFFER_OVERFLOW) {
                operation = delivery;
                goto done;
            }
            delivery = maage_world_stream(visual_world, latest.player.player.position[0], latest.player.player.position[2], yaw, trace);
            if (maage_status_failed(delivery)) {
                operation = delivery;
                goto done;
            }
            if (!maage_status_failed(operation)) operation = maage_client_prediction_prepare(&prediction, renderer, atlas, visual_world, cache.slots, cache.capacity, trace);
            if (maage_status_failed(operation)) {
                unsigned int prediction_index;
                if (maage_status_is_fatal(operation) ||
                    (maage_status_code(operation) != MAAGE_STATUS_ALLOCATION_FAILED &&
                     maage_status_code(operation) != MAAGE_STATUS_BUFFER_OVERFLOW &&
                     maage_status_code(operation) != MAAGE_STATUS_SYSTEM_NOT_SUPPORTED)) goto done;
                MAAGE_STATUS_PROPAGATE(trace, operation, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT);
                if (prediction.failure != operation) {
                    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.prediction", operation, trace, "Prediction resources unavailable; retaining the previous complete presentation and retrying.");
                    maage_client_ui_notify(ui, operation, now);
                    prediction.failure = operation;
                }
                for (prediction_index = 0; prediction_index < MAAGE_CLIENT_PREDICTIONS; ++prediction_index)
                    if (!preview_queued && command.kind && prediction.entries[prediction_index].id == command.prediction)
                        prediction.entries[prediction_index].active = 0; /* This preview was never sent. */
                prediction.dirty = 1;
                ++prediction.generation;
                maage_status_trace_recover(trace, maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__);
            } else
                prediction.failure = 0;
        }
        {
            maage_sound_event completion;
            /* Publish the complete local mesh/light transaction and its cue in
             * this frame; audio playback and Server acknowledgment never gate it. */
            for (;;) {
                operation = maage_client_prediction_take_break_sound(&prediction, blocks, latest.tick, &completion);
                if (maage_status_code(operation) == MAAGE_STATUS_NOOP) break;
                if (maage_status_failed(operation)) goto done;
                if (audio) (void) maage_client_audio_service_event(audio, &completion, trace);
            }
        }
        if (mesh_notice != mesh_activity.last_result) {
            mesh_notice = mesh_activity.last_result;
            maage_client_ui_notify(ui, mesh_notice, now);
        }
        alpha = (now - received_time) / MAAGE_PLAYER_TICK_SECONDS;
        if (alpha < 0.0) alpha = 0.0;
        if (alpha > 1.0 || previous.tick == latest.tick) alpha = 1.0;
        if (previous.world_epoch != latest.world_epoch || previous.session_epoch != latest.session_epoch) alpha = 1.0;
        /* Local look retains its last angles when capture is released. Delayed
         * Server echoes must neither rotate the view nor feed back into input.
         * Position still interpolates copied authoritative motion below. */
        render_yaw = yaw;
        render_pitch = pitch;
        for (axis = 0; axis < 3; ++axis)
            eye[axis] = previous.player.player.position[axis] + (latest.player.player.position[axis] - previous.player.player.position[axis]) * alpha;
        eye[1] += MAAGE_PLAYER_EYE_HEIGHT;
        forward[0] = sin(render_yaw) * cos(render_pitch);
        forward[1] = -sin(render_pitch);
        forward[2] = -cos(render_yaw) * cos(render_pitch);
        if (audio) {
            maage_client_ui_get_options(ui, &current_options);
            (void) maage_client_audio_service_apply_options(audio, &current_options);
            (void) maage_client_audio_service_update(audio, &latest, visual_world, eye, forward, now, trace);
        }
        if (visual_stats_time < 0 || now - visual_stats_time >= 0.25) {
            maage_world_get_stats(visual_world, &visual_stats);
            visual_stats_time = now;
        }
        visual_snapshot = latest;
        visual_snapshot.world_stats.worker_count += visual_stats.worker_count;
        visual_snapshot.world_stats.running_jobs += visual_stats.running_jobs;
        visual_snapshot.world_stats.meshing_queued += visual_stats.meshing_queued;
        visual_snapshot.world_stats.lighting_queued += visual_stats.lighting_queued;
        visual_snapshot.player.has_target = maage_status_code(maage_world_raycast(visual_world, eye, forward, latest.player.reach, &visual_snapshot.player.target)) == MAAGE_STATUS_SUCCESS;
        if (maage_client_item_entities_target(item_entities, &latest, eye, forward, visual_snapshot.player.target.distance, now)) visual_snapshot.player.has_target = 0;
        if (!visual_snapshot.player.has_target || !active || (!maage_control_down(controls, controls->attack, &input) && !maage_control_pressed(controls, controls->attack, &input)) ||
            visual_snapshot.player.target.x != latest.player.breaking.x ||
            visual_snapshot.player.target.y != latest.player.breaking.y ||
            visual_snapshot.player.target.z != latest.player.breaking.z)
            visual_snapshot.player.breaking_active = 0;
        {
            unsigned char visual_light = 64;
            maage_status light_result;
            visual_snapshot.observation.light_available = 0;
            light_result = maage_world_present_light(visual_world, (int) floor(eye[0]), (int) floor(eye[1]), (int) floor(eye[2]), &visual_light);
            if (maage_status_code(light_result) == MAAGE_STATUS_SUCCESS || maage_status_code(light_result) == MAAGE_STATUS_PARTIAL) {
                visual_snapshot.observation.light = visual_light;
                visual_snapshot.observation.light_available = 1;
                visual_snapshot.has_observation = 1;
            }
        }
        swing_progress = maage_client_swing_update(&local_swing, &latest, raw_elapsed, active, maage_control_down(controls, controls->attack, &input) || maage_control_pressed(controls, controls->attack, &input), maage_control_pressed(controls, controls->attack, &input), visual_snapshot.player.has_target);
        maage_client_food_animation_update(&food_animation, &latest, raw_elapsed, active && (maage_control_down(controls, controls->use, &input) || maage_control_pressed(controls, controls->use, &input)) && !maage_control_down(controls, controls->attack, &input) && !maage_control_pressed(controls, controls->attack, &input));
        rendered = maage_client_interaction_update(&interaction, renderer, atlas, blocks, &visual_snapshot, &prediction, trace);
        if (maage_status_failed(rendered)) goto done;
        eye_fluid = maage_fluid_get(maage_content_fluids(content), maage_world_fluid_at(visual_world, eye));
        visual_snapshot.player.submerged = eye_fluid && eye_fluid->submerges;
        operation = maage_client_camera_effects_update(&camera_effects, &visual_snapshot, alpha, latest.paused || maage_client_ui_pauses_game(ui), maage_client_ui_view_bobbing(ui));
        if (maage_status_failed(operation)) goto done;
        if (maage_status_is_warning(operation)) maage_client_ui_notify(ui, operation, now);
        effective_fov = maage_client_ui_fov_radians(ui) * maage_client_fov_update(&fov, &latest, raw_elapsed) * camera_effects.fov_scale * (eye_fluid ? eye_fluid->fov_scale : 1);
        if (state.resized) {
            rendered = maage_renderer_resize(renderer);
            if (maage_status_failed(rendered)) goto done;
        }
        draw_count = resident_count = first_count = 0;
        memset(&mesh_stats, 0, sizeof(mesh_stats));
        for (slot_index = 0; slot_index < cache.capacity; ++slot_index) {
            cache.slots[slot_index].first_frame = 0;
            if (cache.slots[slot_index].used) {
                ++resident_count;
                if (cache.slots[slot_index].mesh)
                    mesh_stats.gpu_bytes += cache.slots[slot_index].vertices * maage_render_gpu_mesh_vertex_bytes() + cache.slots[slot_index].indices * sizeof(unsigned int);
                /* Retain ready GPU meshes beyond a temporarily smaller view
                 * without drawing them. Server ownership/unload remains the
                 * authoritative lifetime boundary for cached replicas. */
                if (abs(cache.slots[slot_index].x - cx) > options.radius || abs(cache.slots[slot_index].z - cz) > options.radius) continue;
                if (!maage_world_chunk_loaded(visual_world, cache.slots[slot_index].x, cache.slots[slot_index].z)) continue;
                if (!cache.slots[slot_index].presented) {
                    if (!maage_world_mesh_current(visual_world, cache.slots[slot_index].x, cache.slots[slot_index].z, cache.slots[slot_index].visual_revision) || !maage_world_mesh_can_present(visual_world, cache.slots[slot_index].x, cache.slots[slot_index].z)) continue;
                    cache.slots[slot_index].first_frame = 1;
                    ++first_count;
                }
                if (cache.slots[slot_index].filtered || cache.slots[slot_index].mesh) {
                    if (cache.slots[slot_index].filtered) {
                        if (cache.slots[slot_index].prediction) cache.draws[draw_count++] = cache.slots[slot_index].prediction;
                    } else
                        cache.draws[draw_count++] = cache.slots[slot_index].mesh;
                    ++mesh_stats.meshes;
                    mesh_stats.vertices += cache.slots[slot_index].vertices;
                    mesh_stats.indices += cache.slots[slot_index].indices;
                }
            }
        }
        mesh_activity.status.gpu_resident = resident_count;
        mesh_activity.status.ready_waiting_estimate = latest.world_stats.ready > resident_count ? latest.world_stats.ready - resident_count : 0;
        if (prediction.reveal) {
            if (!cache.draws && maage_status_failed(maage_client_mesh_cache_grow(&cache))) goto done;
            cache.draws[draw_count++] = prediction.reveal;
        }
        if (interaction.mesh) {
            if (!cache.draws && maage_status_failed(maage_client_mesh_cache_grow(&cache))) {
                rendered = MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0);
                goto done;
            }
            cache.draws[draw_count++] = interaction.mesh;
        }
        debug_text = maage_client_ui_debug_visible(ui) ? maage_client_debug_collect(&debug, now, &visual_snapshot, options.radius, !active, renderer, &mesh_stats, visual_stats.resident_storage_bytes, maage_content_biomes(content)) : NULL;
        maage_client_ui_notice_time(ui, now);
        operation = maage_client_ui_draw(ui, debug_text, &ui_draw);
        if (maage_status_failed(operation)) goto done;
        if (state.framebuffer_width > 0 && state.framebuffer_height > 0) {
            rendered = maage_client_first_person_update(&first_person, renderer, atlas, items, blocks, &visual_snapshot, &camera_effects, render_yaw, render_pitch, raw_elapsed, (double) state.framebuffer_width / state.framebuffer_height, swing_progress, &food_animation, camera_mode == MAAGE_CAMERA_FIRST && !maage_client_ui_modal(ui), trace);
            if (maage_status_failed(rendered)) goto done;
            operation = maage_world_sample_camera(visual_world, (int) floor(eye[0]), (int) floor(eye[1]), (int) floor(eye[2]), &collision);
            if (maage_status_failed(operation)) goto done;
            camera_distance = maage_client_camera_position(camera_mode, eye, render_yaw, render_pitch, latest.player.sneaking, camera_effects.eye_drop, (double) state.framebuffer_width / state.framebuffer_height, effective_fov, &collision, blocks, camera_eye, &camera_yaw, &camera_pitch);
            if (camera_mode != MAAGE_CAMERA_FIRST) {
                if (eye_fluid) effective_fov /= eye_fluid->fov_scale;
                camera_fluid = maage_fluid_get(maage_content_fluids(content), maage_world_fluid_at(visual_world, camera_eye));
                if (camera_fluid) effective_fov *= camera_fluid->fov_scale;
            }
            maage_client_camera(matrix, camera_eye, camera_yaw, camera_pitch, (double) state.framebuffer_width / state.framebuffer_height, options.radius, effective_fov, camera_effects.matrix, camera_mode == MAAGE_CAMERA_FIRST);
            rendered = maage_client_player_view_update(&player_view, renderer, atlas, &resources, items, item_entities, &previous, &visual_snapshot, alpha, eye, render_yaw, render_pitch, raw_elapsed, swing_progress, &food_animation, camera_mode != MAAGE_CAMERA_FIRST, camera_distance, trace);
            if (maage_status_failed(rendered)) goto done;
            if (maage_status_is_warning(rendered)) maage_client_ui_notify(ui, rendered, now);
            rendered = maage_client_environment_update(environment_view, renderer, &latest, maage_client_ui_width(ui), maage_client_ui_height(ui), matrix, visual_world, camera_eye, camera_yaw, camera_pitch, camera_mode == MAAGE_CAMERA_FIRST, options.radius);
            if (maage_status_failed(rendered)) goto done;
            rendered = maage_client_item_entities_update(item_entities, session, visual_world, &visual_snapshot, alpha, eye, now, player_view.draws, player_view.count, cache.slots, cache.capacity, trace);
            if (maage_status_failed(rendered)) goto done;
            if (maage_status_is_warning(rendered)) maage_client_ui_notify(ui, rendered, now);
            rendered = maage_renderer_draw_frame(renderer, cache.draws, draw_count, matrix, ui_draw, trace);
            if (maage_status_failed(rendered)) goto done;
            if (maage_status_code(rendered) == MAAGE_STATUS_SUCCESS) maage_client_item_entities_submitted(item_entities);
        } else
            rendered = MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPDATE, 0);
        frame_end = maage_time_monotonic_seconds();
        if (frame_end < 0.0) goto clock_failed;
        if (first_count && maage_status_code(rendered) == MAAGE_STATUS_SUCCESS) {
            for (slot_index = 0; slot_index < cache.capacity; ++slot_index)
                if (cache.slots[slot_index].first_frame) {
                    operation = maage_world_mesh_presented(visual_world, cache.slots[slot_index].x, cache.slots[slot_index].z, cache.slots[slot_index].visual_revision);
                    if (maage_status_failed(operation)) goto done;
                    if (maage_status_code(operation) == MAAGE_STATUS_SUCCESS) {
                        cache.slots[slot_index].presented = 1;
                        if (prediction.diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "phase=first_visible_frame_submitted column=%d,%d revision=%lu state=%lu initial_ms=%.3f gpu_ready_to_submit_ms=%.3f", cache.slots[slot_index].x, cache.slots[slot_index].z, cache.slots[slot_index].visual_revision, cache.slots[slot_index].revision, (frame_end - cache.slots[slot_index].initialized_at) * 1000, (frame_end - cache.slots[slot_index].uploaded_at) * 1000));
                    }
                }
        }
        if (prediction.diagnostics && maage_status_code(rendered) == MAAGE_STATUS_SUCCESS) {
            unsigned int action;
            for (action = 0; action < MAAGE_CLIENT_PREDICTIONS; ++action)
                if (prediction.entries[action].active && prediction.entries[action].confirmed >= 0 && !prediction.entries[action].submitted && prediction.applied_generation == prediction.generation) {
                    prediction.entries[action].submitted = 1;
                    MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "action=%lu prediction_to_frame_submit_ms=%.3f frames=%lu", prediction.entries[action].id, (frame_end - prediction.entries[action].predicted_at) * 1000, prediction.frame - prediction.entries[action].frame));
                }
        }
        /* Post-generation reconciliation runs only after a successful
         * displayed frame. It does not block rendering or touch the Server. */
        if (maage_status_code(rendered) == MAAGE_STATUS_SUCCESS) {
            operation = maage_client_verify_meshes(visual_world, &cache, &mesh_verifier, cx, cz, options.radius, now);
            if (maage_status_failed(operation)) goto done;
        }
        /* Submit interaction presentation first. Budgeted background uploads
         * prepare the next frame; the next prediction update refreshes any
         * affected views once, before those cache replacements can be drawn.
         * Failed local preparation retains the complete old draw state; do not
         * replace its bases with geometry from the uncommitted correction. */
        if (maage_status_code(rendered) == MAAGE_STATUS_SUCCESS && !prediction.dirty && prediction.applied_generation == prediction.generation) {
            operation = maage_client_stream_meshes(visual_world, renderer, atlas, &cache, &mesh_activity, &prediction, &options, &debug, cx, cz);
            if (maage_status_failed(operation)) goto done;
        } else {
            mesh_activity.status.uploaded_this_frame = 0;
            mesh_activity.status.frame_upload_ms = 0;
        }
        frame_end = maage_time_monotonic_seconds();
        if (frame_end < 0.0) goto clock_failed;
        tick_count = latest.tick >= last_seen_tick ? latest.tick - last_seen_tick : 0;
        last_seen_tick = latest.tick;
        maage_client_debug_frame(&debug, frame_end, raw_elapsed, maage_status_code(rendered) == MAAGE_STATUS_SUCCESS, tick_count, 0.0);
        ++frames;
        if (now - diagnostic_time >= 5.0) {
            MAAGE_LOG_VERBOSE_EVENT("maage.client.server", maage_log_verbose("maage.client.server", "Tick=%lu; loaded=%u; pending=%u; ready=%u; gpu=%lu; uploads=%.1f/s; frame=%.1fms.", latest.tick, latest.world_stats.loaded, latest.world_stats.pending, latest.world_stats.ready, (unsigned long) resident_count, mesh_activity.status.uploads_per_second, frames ? (now - diagnostic_time) * 1000.0 / frames : 0.0));
            diagnostic_time = now;
            frames = 0;
        }
        if (maage_status_code(rendered) == MAAGE_STATUS_NOOP) {
            /* A visible Metal mailbox can be full for just one refresh.
             * Sleeping 100 ms here caused severe, avoidable frame hitches
             * under both unlimited FPS and high caps. Keep AppKit responsive
             * without busy-spinning while presentation catches up. */
            int wait_ms = state.visible && !state.mode_transitioning &&
                                  state.framebuffer_width > 0 && state.framebuffer_height > 0
                              ? 1
                              : 16;
            pacing.deadline = 0;
            pacing.fps = 0;
            result = maage_window_wait_events(system, wait_ms);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        } else {
            operation = maage_client_pacing_wait(&pacing, system, window, maage_client_ui_max_fps(ui), now, &resize_pending);
            if (maage_status_failed(operation)) {
                failure_message = "Presentation pacing failed.";
                goto done;
            }
        }
    }
    goto done;
return_to_title:
    operation = maage_client_audio_service_reset_world(audio, trace);
    if (maage_status_failed(operation)) goto done;
    result = maage_window_capture_mouse(window, 0);
    if (maage_status_failed(result)) goto window_failed;
    maage_server_runtime_request_stop(server);
    /* The Server flushes and the Client replica workers drain on a separate
     * lifecycle Jobs worker. Do not join a saving world on the UI thread. */
    if (retired_resources.view) retired_in_shutdown = 1;
    operation = maage_client_frontend_leave_world(frontend, server, session, visual_world, trace);
    if (maage_status_failed(operation)) goto done;
    connected = 0; /* The shutdown worker now owns the eventual disconnect. */
    server = NULL;
    server_started = 0;
    session = NULL;
    visual_world = NULL;
    maage_client_prediction_destroy(&prediction);
    operation = maage_client_mesh_cache_clear(&cache, NULL);
    if (maage_status_failed(operation)) goto done;
    maage_client_ui_get_options(ui, &video_options);
    rendered = maage_renderer_set_scene(renderer, 1, NULL, NULL);
    if (maage_status_failed(rendered)) goto done;
    rendered = maage_renderer_set_foreground(renderer, NULL, NULL, 0, 0);
    if (maage_status_failed(rendered)) goto done;
    first_person.initialized = 0;
    memset(&camera_effects, 0, sizeof(camera_effects));
    operation = maage_client_item_entities_clear(item_entities);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_player_view_destroy(&player_view);
    if (maage_status_failed(operation)) goto done;
    goto choose_world;

clock_failed:
    outcome = maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_QUERY, 0);
    failure_message = "Monotonic presentation clock unavailable.";
    goto done;
window_failed:
    outcome = MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    failure_message = "Native window/input cannot continue.";
done:
    /* Capture the failing occurrence before saving options overwrites operation.
     * An explicitly retained Server/window/clock failure remains authoritative. */
    if (splash.close_requested) ok = 1;
    if (ok) outcome = maage_status_make(maage_status_is_success(resource_notice) ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP, 0);
    else if (!maage_status_failed(outcome)) {
        outcome = maage_status_failed(rendered) ? rendered : (maage_status_failed(result) ? result : operation);
        if (!maage_status_failed(outcome)) outcome = maage_status_make(MAAGE_STATUS_FATAL, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP, 0);
    }
    /* Window teardown must not wait on a saving Server, draining Client
     * World workers, or a disk sync. Retain all borrowed resources until
     * their background operations finish after the UI is hidden. */
    if (window) (void) maage_window_set_visible(window, 0);
    maage_log_info("maage.client", "Client shutting down...");
    if (options_ready && ui) {
        operation = maage_client_options_store(frontend, ui, &saved_video_options, 1);
        if (maage_status_failed(operation)) maage_log_status(MAAGE_LOG_WARNING, "maage.client.options", operation, "Unable to persist config/options.ini during shutdown.");
    }
    if (!ok) {
        MAAGE_STATUS_PROPAGATE(trace, outcome, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP);
        maage_log_trace_status(MAAGE_LOG_FATAL, "maage.client", outcome, trace, "%s", failure_message);
    }
    maage_status_trace_init(&cleanup_trace);
    if (window) maage_client_cleanup_result(maage_window_capture_mouse(window, 0), &outcome, &cleanup_trace, trace);
    /* Stop command production first. The Server controls all authoritative
     * World flush/persistence; no Client World store/save API is ever invoked. */
    if (server) maage_server_runtime_request_stop(server);
    if (ui) maage_client_ui_set_click_callback(ui, NULL, NULL);
    operation = maage_client_audio_service_destroy(audio, &cleanup_trace);
    if (maage_status_failed(operation)) retain_content = 1;
    maage_client_cleanup_result(operation, &outcome, &cleanup_trace, trace);
    operation = maage_world_destroy(visual_world);
    if (maage_status_failed(operation)) retain_content = 1;
    maage_client_cleanup_result(operation, &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_prediction_destroy(&prediction), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_mesh_cache_clear(&cache, NULL), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_environment_destroy(environment_view), &outcome, &cleanup_trace, trace);
    if (renderer) maage_client_cleanup_result(maage_renderer_set_instances(renderer, NULL, 0), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_player_view_destroy(&player_view), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_item_entities_destroy(item_entities), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_render_mesh_destroy(interaction.mesh), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_first_person_destroy(&first_person, renderer), &outcome, &cleanup_trace, trace);
    operation = maage_client_frontend_destroy(frontend);
    if (maage_status_failed(operation)) {
        maage_client_storage_retain();
        return MAAGE_STATUS_PROPAGATE(trace, operation, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP);
    }
    frontend = NULL;
    maage_client_cleanup_result(maage_client_ui_destroy(ui), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_controls_destroy(controls), &outcome, &cleanup_trace, trace);
    controls = NULL;
    maage_client_cleanup_result(maage_render_texture_destroy(atlas), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_splash_destroy(&splash), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_renderer_destroy(renderer, &cleanup_trace), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_window_destroy(window), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_window_system_destroy(system), &outcome, &cleanup_trace, trace);
    if (server) {
        if (server_started) {
            /* join returns the authoritative outcome, which may be a save or
             * simulation failure even when its thread joined successfully.
             * Do not confuse that with an unsafe native join failure. */
            operation = maage_server_runtime_join(server, &cleanup_trace);
            if (maage_status_failed(operation)) {
                if (!maage_status_failed(outcome)) {
                    outcome = operation;
                    if (trace) *trace = cleanup_trace;
                }
                maage_log_trace_status(MAAGE_LOG_ERROR, "maage.client.server", operation, &cleanup_trace, "Authoritative Server reported a shutdown or persistence failure.");
            }
        }
        /* Destroy retries an actual failed thread join and refuses to release
         * a World whose workers still borrow content/storage. */
        operation = maage_server_runtime_destroy(server);
        if (maage_status_failed(operation)) {
            maage_client_storage_retain();
            maage_log_trace_status(MAAGE_LOG_FATAL, "maage.client.server", operation, &cleanup_trace, "Integrated Server did not fully release its World; retaining borrowed content and instance storage.");
            return MAAGE_STATUS_PROPAGATE(trace, operation, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP);
        }
    }
    if (session) {
        maage_client_cleanup_result(maage_local_session_close(session), &outcome, &cleanup_trace, trace);
        maage_client_cleanup_result(maage_local_session_destroy(session), &outcome, &cleanup_trace, trace);
    }
    if (connected) maage_log_info("maage.client", "Disconnected from integrated server.");
    /* Failed worker teardown retains its borrowed immutable definitions. */
    if (!retain_content) {
        maage_client_cleanup_result(maage_client_resources_destroy(&retired_resources), &outcome, &cleanup_trace, trace);
        maage_client_cleanup_result(maage_client_resources_destroy(&resources), &outcome, &cleanup_trace, trace);
        maage_client_cleanup_result(maage_content_destroy(content), &outcome, &cleanup_trace, trace);
    }
    (void) valid_snapshot;
    return MAAGE_STATUS_PROPAGATE(trace, outcome, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP);
}
