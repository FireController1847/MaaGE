#include "maage/window.h"
#include "maage/input.h"
#include "maage/render.h"
#include "maage/storage.h"
#include "maage/minecraft_assets.h"
#include "maage/time.h"
#include "maage/player.h"
#include "maage/gameplay.h"
#include "maage/world_store.h"
#include "maage/session_protocol.h"
#include "maage/local_session.h"
#include "maage/log.h"
#include "resources.h"
#include "ui/ui.h"
#include "debug.h"
#include "interaction.h"
#include "environment.h"
#include "storage_context.h"
#include "options.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <float.h>
#include <stdio.h>
#include "mesh_diagnostics.h"

/* Phase 1 camera tuning, pending 1.7.10 feel/fidelity verification. */
#define MAAGE_CLIENT_MOUSE_RADIANS 0.0025
#define MAAGE_CLIENT_PITCH_LIMIT 1.553343034
#define MAAGE_CLIENT_FOV_RADIANS 1.221730476
#define MAAGE_CLIENT_MAX_FRAME_SECONDS 0.25

typedef struct maage_client_world_options {
    const char *id, *name;
    maage_world_seed seed;
    unsigned int workers;
    unsigned long generator_version;
    int create, seeded, radius, mode;
    double reach, mesh_budget_ms, frame_target_ms;
} maage_client_world_options;
typedef struct maage_client_chunk_mesh {
    int x, z, used;
    unsigned long revision;
    maage_render_mesh *mesh;
    size_t vertices, indices;
} maage_client_chunk_mesh;
typedef struct maage_client_mesh_cache {
    maage_client_chunk_mesh *slots;
    const maage_render_mesh **draws;
    size_t capacity;
} maage_client_mesh_cache;
typedef struct maage_client_mesh_activity {
    maage_client_mesh_diagnostics status;
    maage_status last_result;
    double sample_start, previous_upload_ms, expected_upload_ms;
    unsigned long sample_uploads, sample_vertices;
} maage_client_mesh_activity;

/* Textual options are resolved before any world/runtime work begins. */
static maage_status maage_client_world_options_parse(int argc, char *const argv[], maage_client_world_options *options)
{
    int i, generator_selected = 0;
    const char *argument;
    char *end;
    long value;
    unsigned long workers;
    memset(options, 0, sizeof(*options)); options->radius = 8; options->mode = -1;
    options->generator_version = MAAGE_GENERATION_FEATURES_VERSION;
    options->frame_target_ms = 1000.0 / 60.0;
    for (i = 1; i < argc; ++i) {
        argument = argv[i];
        if (!strncmp(argument, "--world=", 8)) {
            if (options->id || options->create) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->id = argument + 8;
        } else if (!strncmp(argument, "--create-world=", 15)) {
            if (options->id || options->create) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->id = argument + 15; options->create = 1;
        } else if (!strcmp(argument, "--new-world")) {
            if (options->id || options->create) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->create = 1;
        } else if (!strncmp(argument, "--seed=", 7)) {
            if (maage_status_failed(maage_world_seed_parse(argument + 7, &options->seed))) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->seeded = 1;
        } else if (!strncmp(argument, "--world-name=", 13)) options->name = argument + 13;
        else if (!strncmp(argument, "--render-distance=", 18)) {
            errno = 0; value = strtol(argument + 18, &end, 10);
            if (errno || !argument[18] || *end || value < 1 || value > INT_MAX) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->radius = (int)value;
        } else if (!strncmp(argument, "--mesh-upload-budget-ms=", 24)) {
            errno = 0; options->mesh_budget_ms = strtod(argument + 24, &end);
            if (errno || !argument[24] || *end ||
                !(options->mesh_budget_ms >= 0.0 && options->mesh_budget_ms <= DBL_MAX)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
        } else if (!strncmp(argument, "--mesh-frame-target-ms=", 23)) {
            errno = 0; options->frame_target_ms = strtod(argument + 23, &end);
            if (errno || !argument[23] || *end ||
                !(options->frame_target_ms > 0.0 && options->frame_target_ms <= DBL_MAX)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
        } else if (!strncmp(argument, "--generator-version=", 20)) {
            maage_generation_policy policy;
            if (argument[20] < '0' || argument[20] > '9') return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            errno = 0; options->generator_version = strtoul(argument + 20, &end, 10);
            if (errno || *end || maage_status_failed(maage_generation_policy_default(options->generator_version, &policy))) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            generator_selected = 1;
        } else if (!strncmp(argument, "--world-workers=", 16)) {
            if (argument[16] < '0' || argument[16] > '9') return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            errno = 0; workers = strtoul(argument + 16, &end, 10);
            if (errno || *end || workers > UINT_MAX) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
            options->workers = (unsigned int)workers;
        } else if (!strncmp(argument, "--gamemode=", 11)) {
            if (!strcmp(argument + 11, "creative")) options->mode = MAAGE_GAME_MODE_CREATIVE;
            else if (!strcmp(argument + 11, "survival")) options->mode = MAAGE_GAME_MODE_SURVIVAL;
            else return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
        } else if (!strncmp(argument, "--interaction-range=", 20)) {
            errno = 0; options->reach = strtod(argument + 20, &end);
            if (errno || !argument[20] || *end || !(options->reach > 0 && options->reach <= 64)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
        } else if (!strcmp(argument, "--world") || !strcmp(argument, "--create-world") ||
            !strcmp(argument, "--seed") || !strcmp(argument, "--world-name") || !strcmp(argument, "--render-distance") ||
            !strcmp(argument, "--world-workers") || !strcmp(argument, "--generator-version") || !strcmp(argument, "--mesh-upload-budget-ms") ||
            !strcmp(argument, "--mesh-frame-target-ms") || !strcmp(argument, "--gamemode") ||
            !strcmp(argument, "--interaction-range")) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    }
    if (options->seeded && options->id && !options->create) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    if (options->name && !options->create && !options->seeded) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    if (generator_selected && !options->create && !options->seeded) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PARSE, 0);
}

/* Mesh resources remain Client-owned. Capacity grows with actual resident
 * meshes, independently of the World's scheduling/cache implementation. */
static maage_status maage_client_mesh_cache_grow(maage_client_mesh_cache *cache)
{
    maage_client_chunk_mesh *slots;
    const maage_render_mesh **draws;
    size_t previous = cache->capacity, grown;
    if (previous > ((size_t)-1 - 1) / 2) grown = previous + 1;
    else grown = previous ? previous * 2 : 32;
    if (grown <= previous || grown > (size_t)-1 / sizeof(*slots) ||
        grown >= (size_t)-1 / sizeof(*draws)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    slots = (maage_client_chunk_mesh *)realloc(cache->slots, grown * sizeof(*slots));
    if (!slots) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    cache->slots = slots;
    draws = (const maage_render_mesh **)realloc(cache->draws, (grown + 1) * sizeof(*draws));
    if (!draws) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    cache->draws = draws;
    memset(cache->slots + previous, 0, (grown - previous) * sizeof(*slots));
    cache->capacity = grown;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
static maage_status maage_client_mesh_cache_clear(maage_client_mesh_cache *cache)
{
    size_t i;
    for (i = 0; i < cache->capacity; ++i)
        maage_render_mesh_destroy(cache->slots[i].mesh);
    free(cache->slots); free(cache->draws);
    memset(cache, 0, sizeof(*cache));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
/* Cleanup continues after a reported failure while retaining the first failed
 * outcome. Each independent cleanup occurrence gets its own bounded trace. */
/* Save only at explicit option boundaries, never from the rendering path.
 * A snapshot uses the last confirmed window mode during AppKit transitions. */
static maage_status maage_client_options_store(const maage_instance *instance,
    const maage_client_ui *ui, maage_client_options *saved, int force)
{
    maage_client_options current;
    maage_status result;
    maage_client_ui_get_options(ui, &current);
    if (!force && maage_client_options_equal(&current, saved))
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SAVE, 0);
    result = maage_client_options_save(instance, &current);
    if (!maage_status_failed(result)) *saved = current;
    return result;
}

static void maage_client_cleanup_result(maage_status result, maage_status *outcome,
    maage_status_trace *trace, maage_status_trace *failure_trace)
{
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
static double maage_client_mesh_budget(const maage_client_world_options *options,
    const maage_client_debug *debug, const maage_client_mesh_activity *activity)
{
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
static void maage_client_mesh_rates(maage_client_mesh_activity *activity, double now)
{
    double interval = now - activity->sample_start;
    if (!(interval >= 1.0)) return;
    activity->status.uploads_per_second = (double)activity->sample_uploads / interval;
    activity->status.vertices_per_second = (double)activity->sample_vertices / interval;
    activity->sample_uploads = activity->sample_vertices = 0;
    activity->sample_start = now;
}
/* Drain ready World meshes as long as measured work fits the millisecond
 * budget. The first attempt ensures progress even after an expensive frame;
 * later attempts predict cost to avoid repeatedly overrunning frame pacing.
 * No CPU mesh or render handle survives a failed/stale transfer. */
static maage_status maage_client_stream_meshes(maage_world *world,
    maage_renderer *renderer, maage_render_texture *atlas, maage_client_mesh_cache *cache,
    maage_client_mesh_activity *activity, const maage_client_world_options *options,
    const maage_client_debug *debug, int center_x, int center_z)
{
    maage_world_mesh_update update;
    const maage_mesh_data *geometry;
    maage_render_mesh *replacement;
    maage_status result;
    double begin, current, upload_begin, upload_ms;
    double budget_ms = maage_client_mesh_budget(options, debug, activity);
    size_t i, selected, vacant;
    unsigned int processed = 0;
    activity->status.uploaded_this_frame = 0;
    activity->status.frame_upload_ms = 0;
    activity->status.budget_ms = budget_ms;
    for (i = 0; i < cache->capacity; ++i) {
        if (cache->slots[i].used &&
            (abs(cache->slots[i].x - center_x) > options->radius ||
             abs(cache->slots[i].z - center_z) > options->radius ||
             !maage_world_chunk_loaded(world, cache->slots[i].x, cache->slots[i].z))) {
            maage_render_mesh_destroy(cache->slots[i].mesh);
            memset(&cache->slots[i], 0, sizeof(cache->slots[i]));
        }
    }
    begin = maage_time_monotonic_seconds();
    if (begin < 0.0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    for (;;) {
        current = maage_time_monotonic_seconds();
        if (current < 0.0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
        if (processed && ((current - begin) * 1000.0 >= budget_ms ||
            (current - begin) * 1000.0 + activity->expected_upload_ms > budget_ms)) break;
        result = maage_world_take_mesh(world, &update);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) break;
        ++processed;
        if (abs(update.x - center_x) > options->radius || abs(update.z - center_z) > options->radius ||
            !maage_world_mesh_current(world, update.x, update.z, update.revision)) {
            ++activity->status.rejected;
            maage_world_mesh_destroy(update.mesh);
            continue;
        }
        selected = cache->capacity; vacant = cache->capacity;
        for (i = 0; i < cache->capacity; ++i) {
            if (cache->slots[i].used && cache->slots[i].x == update.x &&
                cache->slots[i].z == update.z) { selected = i; break; }
            if (!cache->slots[i].used && vacant == cache->capacity) vacant = i;
        }
        if (selected == cache->capacity) {
            selected = vacant;
            if (selected == cache->capacity) {
                selected = cache->capacity;
                if (maage_status_failed(maage_client_mesh_cache_grow(cache))) {
                    maage_world_mesh_destroy(update.mesh);
                    return MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
                }
            }
        }
        if (!cache->slots[selected].used || update.revision != cache->slots[selected].revision) {
            geometry = maage_world_mesh_data(update.mesh);
            replacement = NULL; result = MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
            if (geometry->index_count) {
                upload_begin = maage_time_monotonic_seconds();
                if (upload_begin < 0.0) { maage_world_mesh_destroy(update.mesh); return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0); }
                result = maage_renderer_upload_geometry(renderer, geometry, atlas, &replacement, NULL);
                current = maage_time_monotonic_seconds();
                if (current < 0.0) {
                    maage_render_mesh_destroy(replacement);
                    maage_world_mesh_destroy(update.mesh); return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
                }
                upload_ms = (current - upload_begin) * 1000.0;
                activity->status.frame_upload_ms += upload_ms;
                if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
                    activity->expected_upload_ms = activity->status.gpu_uploads ?
                        activity->expected_upload_ms * 0.75 + upload_ms * 0.25 : upload_ms;
                    ++activity->status.gpu_uploads; ++activity->sample_uploads;
                    activity->status.vertices_uploaded += (unsigned long)geometry->vertex_count;
                    activity->sample_vertices += (unsigned long)geometry->vertex_count;
                    ++activity->status.uploaded_this_frame;
                }
            }
            if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
                maage_render_mesh_destroy(cache->slots[selected].mesh);
                cache->slots[selected].mesh = replacement;
                cache->slots[selected].used = 1;
                cache->slots[selected].vertices = geometry->vertex_count;
                cache->slots[selected].indices = geometry->index_count;
                cache->slots[selected].x = update.x;
                cache->slots[selected].z = update.z;
                cache->slots[selected].revision = update.revision;
            } else {
                maage_render_mesh_destroy(replacement);
                maage_world_mesh_destroy(update.mesh);
                if (maage_status_code(result) == MAAGE_STATUS_ALLOCATION_FAILED) {
                    /* The consumed revision is not retried every frame. Keep
                     * the previous valid GPU mesh until a later World update. */
                    if (activity->last_result != result) {
                        activity->last_result = result;
                        maage_log_status(MAAGE_LOG_WARNING, "maage.client.render", activity->last_result,
                            "Chunk %d,%d revision %lu: retaining the previous visual or omitting this mesh.",
                            update.x, update.z, update.revision);
                    }
                    continue;
                }
                return result;
            }
        }
        maage_world_mesh_destroy(update.mesh);
    }
    current = maage_time_monotonic_seconds();
    if (current < 0.0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    activity->previous_upload_ms = activity->status.frame_upload_ms;
    activity->status.stream_ms = (current - begin) * 1000.0;
    maage_client_mesh_rates(activity, current);
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
}

static void maage_client_camera(float output[16], const double eye[3], double yaw, double pitch, double aspect, int radius)
{
    double sy = sin(yaw), cy = cos(yaw), sp = sin(pitch), cp = cos(pitch);
    double forward[3], right[3], up[3], focal, far_clip;
    float view[16], projection[16];
    int row, column, k;
    forward[0] = sy * cp; forward[1] = -sp; forward[2] = -cy * cp;
    right[0] = cy; right[1] = 0; right[2] = sy;
    up[0] = sy * sp; up[1] = cp; up[2] = -cy * sp;
    memset(view, 0, sizeof(view)); memset(projection, 0, sizeof(projection));
    for (k = 0; k < 3; ++k) {
        view[k * 4] = (float)right[k]; view[k * 4 + 1] = (float)up[k]; view[k * 4 + 2] = (float)-forward[k];
        view[12] -= (float)(right[k] * eye[k]); view[13] -= (float)(up[k] * eye[k]); view[14] += (float)(forward[k] * eye[k]);
    }
    view[15] = 1.0f; focal = 1.0 / tan(MAAGE_CLIENT_FOV_RADIANS * 0.5);
    projection[0] = (float)(focal / aspect); projection[5] = (float)focal;
    /* Include the furthest chunk corner and full world height, even while
     * moving within the center chunk; keep the established minimum range. */
    far_clip = ((double)radius + 2.0) * MAAGE_CHUNK_EDGE * 1.4142135623730951 + MAAGE_WORLD_HEIGHT;
    if (far_clip < 512.0) far_clip = 512.0;
    /* Right-handed perspective, near=.05, depth [0,1]. */
    projection[10] = (float)(far_clip / (0.05 - far_clip)); projection[11] = -1.0f;
    projection[14] = (float)(0.05 * far_clip / (0.05 - far_clip));
    for (column = 0; column < 4; ++column) for (row = 0; row < 4; ++row) {
        output[column * 4 + row] = 0;
        for (k = 0; k < 4; ++k) output[column * 4 + row] += projection[k * 4 + row] * view[column * 4 + k];
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
    unsigned long next_sequence;
} maage_client_outbox;

static int maage_client_input_coalescible(const maage_session_command *command)
{
    /* Held actions are levels, but a freshly pressed jump is a one-shot.
     * Coalesce only when held action levels have not changed, so rapid
     * click/release transitions cannot be swallowed by frame-rate sampling. */
    return command->kind == MAAGE_CMD_INPUT && !command->intent.jump_pressed;
}

static maage_status maage_client_outbox_queue(maage_client_outbox *outbox,
    const maage_session_command *input)
{
    maage_session_command command;
    unsigned int tail, previous;
    if (!outbox || !input) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 0);
    command = *input;
    command.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    if (outbox->count && maage_client_input_coalescible(&command)) {
        previous = (outbox->head + outbox->count - 1U) % MAAGE_CLIENT_OUTBOX_CAPACITY;
        if (maage_client_input_coalescible(&outbox->commands[previous]) &&
            outbox->commands[previous].intent.attack == command.intent.attack &&
            outbox->commands[previous].intent.use == command.intent.use &&
            outbox->commands[previous].intent.movement.jump == command.intent.movement.jump) {
            /* Keep the original FIFO sequence; the last held-only input wins.
             * No edge-triggered or authoritative command is overwritten. */
            command.sequence = outbox->commands[previous].sequence;
            outbox->commands[previous] = command;
            return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
        }
    }
    if (outbox->count == MAAGE_CLIENT_OUTBOX_CAPACITY)
        return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 0);
    if (outbox->next_sequence == ULONG_MAX)
        return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 1);
    command.sequence = ++outbox->next_sequence;
    tail = (outbox->head + outbox->count) % MAAGE_CLIENT_OUTBOX_CAPACITY;
    outbox->commands[tail] = command;
    ++outbox->count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}

/* A full local mailbox is not a lost command. Leave the FIFO head intact,
 * pump native events/presentation, and retry before sampling another input. */
static maage_status maage_client_outbox_flush(maage_client_outbox *outbox,
    maage_local_session *session)
{
    maage_status result;
    if (!outbox || !session)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 0);
    while (outbox->count) {
        result = maage_local_session_send(session, &outbox->commands[outbox->head]);
        if (maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW)
            return result;
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS)
            return maage_status_make(MAAGE_STATUS_INVALID_STATE,
                MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 0);
        outbox->head = (outbox->head + 1U) % MAAGE_CLIENT_OUTBOX_CAPACITY;
        --outbox->count;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}

maage_status maage_client_run(int argc, char *const argv[], maage_status_trace *trace)
{
    maage_window_system *system = NULL;
    maage_window *window = NULL;
    maage_renderer *renderer = NULL;
    maage_render_texture *atlas = NULL;
    maage_client_ui *ui = NULL;
    const maage_render_ui_data *ui_draw;
    const maage_client_ui_debug *debug_text;
    maage_client_ui_input ui_input;
    maage_client_debug debug;
    maage_client_mesh_stats mesh_stats;
    maage_block_registry *registry = NULL;
    maage_item_registry *items = NULL;
    maage_gameplay game;
    maage_gameplay_intent intent;
    maage_client_interaction_visual interaction;
    maage_world *world = NULL;
    maage_leaves_mode applied_leaves = MAAGE_LEAVES_FANCY;
    maage_world_store *store = NULL;
    maage_instance *instance = maage_client_storage_instance();
    maage_client_mesh_cache cache;
    maage_client_mesh_activity mesh_activity;
    maage_client_world_options options;
    maage_client_options video_options, saved_video_options;
    maage_client_resources resources;
    maage_biome_registry *biomes = NULL;
    maage_world_config world_config;
    maage_world_record record;
    maage_generation_policy generation_policy;
    maage_environment_state environment_state;
    maage_client_environment *environment_view = NULL;
    maage_world_stats stats;
    maage_window_config window_config;
    maage_window_state state;
    maage_input_snapshot input;
    maage_player player, previous;
    maage_render_desc render_config;
    maage_status result = MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WINDOW, MAAGE_OPERATION_INITIALIZE, 0);
    maage_status rendered = MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    maage_status operation = 0, outcome = 0, teardown, resource_notice = 0, world_notice = 0, mesh_notice = 0, interaction_notice = 0, gameplay_notice = 0;
    maage_status_trace cleanup_trace;
    const char *failure_message = "Client operation cannot continue.";
    char directory[MAAGE_PATH_CAPACITY], error[256], seed_text[32];
    double last, now, elapsed, raw_elapsed, accumulator = 0, alpha, eye[3], yaw = 0, pitch = 0, diagnostic_time;
    double tick_start = 0, tick_seconds, frame_end;
    double wheel = 0;
    float matrix[16];
    size_t draw_count, resident_count, slot_index;
    unsigned long frames = 0, tick_count;
    unsigned ui_actions;
    int ok = 0, initial_capture = 1, active, was_active = 0, jump_pending = 0, axis;
    int player_ready = 0, cx, cz, i, was_modal, was_focused = 0;
    int attack_pending = 0, use_pending = 0, changing_mode = 0, was_changing_mode = 0;
    int options_ready = 0, options_save_needed = 0, was_options_visible = 0;
    int resize_pending = 0, logged_max_fps = -1, logged_vsync = -1;
    memset(&resources, 0, sizeof(resources)); memset(&cache, 0, sizeof(cache)); memset(&mesh_activity, 0, sizeof(mesh_activity));
    memset(&debug, 0, sizeof(debug));
    memset(&interaction, 0, sizeof(interaction)); memset(&intent, 0, sizeof(intent));
    error[0] = 0;
    maage_render_desc_init(&render_config);
    rendered = maage_render_desc_parse_args(argc, argv, &render_config);
    if (maage_status_code(rendered) != MAAGE_STATUS_SUCCESS) goto done;
    operation = maage_client_world_options_parse(argc, argv, &options);
    if (maage_status_failed(operation)) {
        failure_message = "Invalid world options. Use --new-world or --world=<id>, with valid seed, generator, render-distance, worker and mesh-budget options."; goto done;
    }
    if (!instance) {
        operation = maage_status_make(MAAGE_STATUS_INVALID_STATE, MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SELECT, 0);
        failure_message = "No validated instance was selected at startup."; goto done;
    }
    operation = maage_client_options_load(instance, &video_options);
    if (maage_status_failed(operation))
        maage_log_status(MAAGE_LOG_WARNING, "maage.client.options", operation,
            "Unable to read instance config/options.ini; using video defaults.");
    saved_video_options = video_options;
    strcpy(directory, maage_client_storage_data_root());
    maage_log_info("maage.client.instance", "Instance: %s (%s).", maage_instance_get_info(instance)->name,
        maage_instance_get_info(instance)->id);
    if (maage_world_store_legacy_pending(directory)) {
        if (maage_client_storage_migrate_requested() || maage_instance_only_configured(instance)) {
            operation = maage_world_store_migrate_legacy(directory, maage_instance_directory(instance), error, trace);
            if (maage_status_failed(operation)) {
                failure_message = error; goto done;
            }
            maage_log_info("maage.client.instance", "Legacy world migration completed for the selected instance.");
        } else maage_log_warn("maage.client.instance", "Legacy global worlds are preserved pending an explicit destination. Close Clients, then launch --instance=<UUID> --migrate-legacy-worlds to choose this instance. Its private worlds remain independent.");
    }
    biomes = (maage_biome_registry *)calloc(1, sizeof(*biomes));
    if (!biomes) { operation = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE, 0); goto done; }
    operation = maage_biome_registry_init(biomes);
    if (maage_status_failed(operation)) goto done;
    maage_log_info("maage.client.resources", "Resource directory: %s/minecraft/assets/reconst", directory);
    operation = maage_client_resources_load(directory, &resources, biomes, trace);
    if (maage_status_failed(operation)) goto done;
    resource_notice = resources.load_status;
    maage_status_trace_init(trace);
    MAAGE_STATUS_TRACE(trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    operation = maage_biome_registry_finalize(biomes);
    if (maage_status_failed(operation)) {
        failure_message = "Required biome registry could not be finalized."; goto done;
    }
    operation = maage_client_register_blocks(&resources, biomes, &registry, trace);
    if (maage_status_failed(operation)) {
        failure_message = "Required block registry could not be initialized."; goto done;
    }
    operation = maage_item_registry_create_builtin(registry, &items);
    if (maage_status_failed(operation)) {
        failure_message = "Required item registry could not be initialized."; goto done;
    }
    operation = maage_gameplay_init(&game, items);
    if (maage_status_failed(operation)) goto done;
    result = maage_window_system_create(MAAGE_WINDOW_AUTO, &system, trace);
    if (maage_status_failed(result)) goto window_failed;
    if (maage_status_is_warning(result)) {
        maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.window", result, trace, "Recovered with an available native window backend.");
        maage_status_trace_init(trace);
    }
    maage_window_config_init(&window_config); window_config.title = "MaaGE Client";
    window_config.visible = 0; window_config.full_display_content = 1;
    result = maage_window_create(system, &window_config, &window, trace);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
    render_config.window = window; render_config.required_features |= MAAGE_RENDER_CAP_TEXTURED_MESH | MAAGE_RENDER_CAP_UI | MAAGE_RENDER_CAP_SCENE;
    rendered = maage_renderer_create(&render_config, &renderer, trace);
    if (maage_status_failed(rendered)) goto done;
    if (maage_status_is_warning(rendered)) {
        maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.render", rendered, trace, "Recovered with an available renderer.");
        maage_status_trace_init(trace);
    }
    rendered = maage_renderer_create_texture(renderer, &resources.atlas, &atlas, trace);
    if (maage_status_code(rendered) != MAAGE_STATUS_SUCCESS) goto done;
    operation = maage_client_interaction_init(&interaction, &resources);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_ui_create(renderer, window, directory, &resources.atlas, &ui, trace);
    maage_client_resources_destroy(&resources);
    if (maage_status_failed(operation)) {
        failure_message = "Cannot initialize the required Client GUI."; goto done;
    }
    maage_status_trace_init(trace);
    MAAGE_STATUS_TRACE(trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    operation = maage_client_ui_finalize(ui);
    if (maage_status_failed(operation)) {
        failure_message = "Cannot finalize the Client screen registry."; goto done;
    }
    if (maage_status_is_success(resource_notice)) resource_notice = maage_client_ui_resource_status(ui);
    maage_status_trace_init(trace);
    MAAGE_STATUS_TRACE(trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    operation = maage_client_environment_create(renderer, directory, &environment_view, trace);
    if (maage_status_failed(operation)) {
        failure_message = "Cannot initialize environmental presentation resources."; goto done;
    }
    if (maage_status_is_success(resource_notice))
        resource_notice = maage_client_environment_resource_status(environment_view, NULL);
    maage_log_info("maage.client", "Renderer initialized: %s.", maage_render_backend_name(maage_renderer_get_backend(renderer)));
    maage_status_trace_init(trace);
    MAAGE_STATUS_TRACE(trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE);
    operation = maage_generation_policy_default(options.generator_version, &generation_policy);
    if (maage_status_failed(operation)) goto done;
    operation = maage_world_store_open_configured(maage_instance_directory(instance), options.id, options.create, options.name,
        options.seeded ? &options.seed : NULL, options.id && !options.create ? NULL : &generation_policy,
        items, &store, &record, error, trace);
    if (maage_status_failed(operation)) {
        failure_message = error; goto done;
    }
    memset(&world_config, 0, sizeof(world_config));
    world_config.registry = registry; world_config.biomes = biomes; world_config.seed = record.seed;
    world_config.worker_count = options.workers;
    world_config.generation_policy = record.generation_policy;
    world_config.initial_environment = &record.environment;
    world_config.store = store; world_config.render_distance = options.radius; world_config.new_world = record.new_world;
    memcpy(world_config.position, record.position, sizeof(world_config.position));
    operation = maage_world_create(&world_config, &world, trace);
    if (maage_status_failed(operation)) {
        failure_message = "World initialization failed (worker pool, generator policy, or memory)."; goto done;
    }
    maage_world_get_stats(world, &stats);
    operation = maage_player_spawn(&player, world);
    if (maage_status_failed(operation)) goto done;
    previous = player; player_ready = 1;
    operation = maage_gameplay_restore(&game, items, &record.gameplay);
    if (maage_status_failed(operation)) {
        failure_message = "Saved player gameplay data does not match registered items."; player_ready = 0; goto done;
    }
    if (record.new_world) {
        operation = maage_gameplay_init(&game, items);
        if (maage_status_failed(operation)) goto done;
    }
    if (options.mode >= 0) {
        operation = maage_gameplay_set_mode(&game, (unsigned int)options.mode);
        if (maage_status_failed(operation)) goto done;
        if (record.new_world && options.mode == MAAGE_GAME_MODE_SURVIVAL)
            memset(&game.inventory, 0, sizeof(game.inventory));
    }
    game.reach = options.reach;
    operation = maage_client_ui_set_gameplay(ui, items, &game, registry);
    if (maage_status_failed(operation)) goto done;
    yaw = record.yaw; pitch = record.pitch;
    operation = maage_world_seed_format(record.seed, seed_text, sizeof(seed_text));
    if (maage_status_failed(operation)) goto done;
    maage_log_info("maage.client.world", "%s world %s; seed=%s; generator=%lu; render distance=%d; workers=%u. Loading nearby columns asynchronously.",
        record.new_world ? "Created" : "Restored", record.id, seed_text,
        record.generation_policy.generator_version, options.radius, stats.worker_count);
    rendered = maage_renderer_set_clear_color(renderer, 0.53f, 0.76f, 1.0f, 1.0f);
    if (maage_status_code(rendered) != MAAGE_STATUS_SUCCESS) goto done;
    rendered = maage_renderer_prepare_frame(renderer, trace);
    if (maage_status_code(rendered) == MAAGE_STATUS_NOT_IMPLEMENTED) rendered = MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    if (maage_status_code(rendered) != MAAGE_STATUS_SUCCESS && maage_status_code(rendered) != MAAGE_STATUS_NOOP) goto done;
    result = maage_window_set_visible(window, 1);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
    operation = maage_client_ui_apply_options(ui, &video_options);
    if (maage_status_failed(operation))
        maage_log_status(MAAGE_LOG_WARNING, "maage.client.options", operation,
            "Some saved video settings could not be applied; retaining available defaults.");
    options_ready = 1;
    maage_log_info("maage.client", "WASD walks; Space jumps (double tap for Creative flight), Shift descends; mouse buttons break/place; 1-9 or wheel selects; E opens inventory; Escape returns; F3 shows diagnostics; F11 toggles window mode. Options selects game mode. Unavailable columns block interaction and movement.");
    last = maage_time_monotonic_seconds(); diagnostic_time = last;
    mesh_activity.sample_start = last;
    if (last < 0) goto clock_failed;
    maage_client_ui_notify(ui, resource_notice, last);
    for (;;) {
        maage_status_trace_init(trace);
        MAAGE_STATUS_TRACE(trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
        result = maage_window_poll_events(system, 0);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        result = maage_window_get_state(window, &state);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        state.resized = state.resized || resize_pending;
        resize_pending = 0;
        if (state.close_requested) { ok = 1; break; }
        result = maage_window_get_input(window, &input);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        was_modal = maage_client_ui_modal(ui);
        was_options_visible = maage_client_ui_options_visible(ui);
        memset(&ui_input, 0, sizeof(ui_input));
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
            ui_input.inventory = input.pressed[MAAGE_INPUT_KEY_E];
        }
        ui_input.mouse_x = (float)input.mouse_x; ui_input.mouse_y = (float)input.mouse_y;
        maage_client_ui_set_safe_area(ui, &state.safe_area);
        maage_client_ui_set_window_state(ui, &state);
        operation = maage_client_ui_update(ui, &ui_input, state.framebuffer_width, state.framebuffer_height, state.width, state.height, &ui_actions);
        if (maage_status_failed(operation)) {
            MAAGE_STATUS_TRACE(trace, operation, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_UPDATE);
            maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.ui", operation, trace, "GUI action rejected.");
            maage_client_ui_notify(ui, operation, maage_time_monotonic_seconds());
            maage_status_trace_init(trace);
            MAAGE_STATUS_TRACE(trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
        }
        if (logged_max_fps != maage_client_ui_max_fps(ui) || logged_vsync != maage_client_ui_vsync(ui)) {
            logged_max_fps = maage_client_ui_max_fps(ui);
            logged_vsync = maage_client_ui_vsync(ui);
            maage_log_status(MAAGE_LOG_INFO, "maage.client.options",
                maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0),
                "Frame pacing: max_fps=%d (0 is unlimited); VSync=%s.",
                logged_max_fps, logged_vsync ? "on" : "off");
        }
        changing_mode = maage_client_ui_window_mode_changing(ui);
        if (was_options_visible && !maage_client_ui_options_visible(ui))
            options_save_needed = 1;
        if (was_changing_mode && !changing_mode) {
            initial_capture = 1;
            options_save_needed = 1;
        }
        if (options_save_needed && !changing_mode) {
            operation = maage_client_options_store(instance, ui, &saved_video_options, 0);
            options_save_needed = 0;
            if (maage_status_failed(operation)) {
                maage_log_status(MAAGE_LOG_WARNING, "maage.client.options", operation,
                    "Unable to atomically save instance config/options.ini.");
                maage_client_ui_notify(ui, operation, maage_time_monotonic_seconds());
            }
        }
        if (!changing_mode && !was_changing_mode &&
            ((was_focused && !input.focused) || (was_active && !input.captured))) {
            if (!maage_client_ui_modal(ui)) maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
        }
        was_changing_mode = changing_mode;
        was_focused = input.focused;
        if (ui_actions & MAAGE_CLIENT_UI_EXIT) { ok = 1; break; }
        if (ui_actions & MAAGE_CLIENT_UI_RESPAWN) {
            operation = maage_gameplay_respawn(&game, &player, world);
            if (maage_status_failed(operation)) goto done;
            previous = player;
            operation = maage_client_ui_back(ui);
            if (maage_status_failed(operation)) goto done;
        }
        if (maage_client_ui_modal(ui)) {
            initial_capture = 0;
            if (input.captured) result = maage_window_capture_mouse(window, 0);
            input.captured = 0;
        } else if (!changing_mode && input.focused && !input.captured && (initial_capture || was_modal || input.primary_pressed)) {
            result = maage_window_capture_mouse(window, 1); initial_capture = 0;
            if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
                input.captured = 1; input.mouse_delta_x = input.mouse_delta_y = 0;
            }
        }
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        now = maage_time_monotonic_seconds();
        if (now < 0) goto clock_failed;
        raw_elapsed = now - last; last = now; elapsed = raw_elapsed;
        if (elapsed < 0) elapsed = 0;
        if (elapsed > MAAGE_CLIENT_MAX_FRAME_SECONDS) elapsed = MAAGE_CLIENT_MAX_FRAME_SECONDS;
        cx = maage_world_chunk_coordinate((int)floor(player.position[0]));
        cz = maage_world_chunk_coordinate((int)floor(player.position[2]));
        /* One owner commits worker results before the ordered fixed-step player
         * phase. Collision observes stable published columns without waiting. */
        if (maage_client_ui_leaves_mode(ui) != applied_leaves) {
            operation = maage_world_set_leaves_mode(world, maage_client_ui_leaves_mode(ui));
            if (maage_status_failed(operation)) goto done;
            applied_leaves = maage_client_ui_leaves_mode(ui);
        }
        operation = maage_world_stream(world, player.position[0], player.position[2], yaw, trace);
        if (maage_status_failed(operation)) goto done;
        teardown = maage_world_status(world);
        if (teardown != world_notice) {
            world_notice = teardown; maage_client_ui_notify(ui, teardown, now);
        }
        if (maage_client_ui_debug_visible(ui) || now - diagnostic_time >= 5.0)
            maage_world_get_stats(world, &stats);
        rendered = maage_client_stream_meshes(world, renderer, atlas, &cache, &mesh_activity, &options, &debug, cx, cz);
        if (maage_status_failed(rendered)) goto done;
        if (mesh_notice != mesh_activity.last_result) {
            mesh_notice = mesh_activity.last_result; maage_client_ui_notify(ui, mesh_notice, now);
        }
        active = !maage_client_ui_modal(ui) && !changing_mode && input.focused && input.captured && state.visible && state.framebuffer_width > 0 && state.framebuffer_height > 0;
        if (active && !was_active) elapsed = 0;
        was_active = active;
        tick_count = 0; tick_seconds = 0;
        if (active) {
            yaw = fmod(yaw + input.mouse_delta_x * MAAGE_CLIENT_MOUSE_RADIANS, 6.283185307179586);
            pitch += input.mouse_delta_y * MAAGE_CLIENT_MOUSE_RADIANS;
            if (pitch > MAAGE_CLIENT_PITCH_LIMIT) pitch = MAAGE_CLIENT_PITCH_LIMIT;
            if (pitch < -MAAGE_CLIENT_PITCH_LIMIT) pitch = -MAAGE_CLIENT_PITCH_LIMIT;
            intent.movement.forward = (double)(input.keys[MAAGE_INPUT_KEY_W] - input.keys[MAAGE_INPUT_KEY_S]);
            intent.movement.strafe = (double)(input.keys[MAAGE_INPUT_KEY_D] - input.keys[MAAGE_INPUT_KEY_A]);
            if (input.pressed[MAAGE_INPUT_KEY_SPACE]) jump_pending = 1;
            if (!was_modal) {
                if (input.primary_pressed) attack_pending = 1;
                if (input.secondary_pressed) use_pending = 1;
            }
            for (i = 0; i < (int)MAAGE_HOTBAR_SLOTS; ++i)
                if (input.pressed[MAAGE_INPUT_KEY_1 + i]) game.inventory.selected = (unsigned int)i;
            wheel += input.scroll_y;
            if (wheel >= 1 || wheel <= -1) {
                wheel = fmod(wheel, (double)MAAGE_HOTBAR_SLOTS);
                i = (int)wheel;
                game.inventory.selected = (unsigned int)(((int)game.inventory.selected - i + 9) % 9);
                wheel -= i;
            }
            intent.movement.yaw = yaw; intent.pitch = pitch;
            intent.descend = input.keys[MAAGE_INPUT_KEY_SHIFT];
            if (maage_world_chunk_loaded(world, cx, cz)) accumulator += elapsed;
            else { accumulator = 0; previous = player; }
            if (accumulator >= MAAGE_PLAYER_TICK_SECONDS) tick_start = maage_time_monotonic_seconds();
            while (accumulator >= MAAGE_PLAYER_TICK_SECONDS) {
                intent.movement.jump = input.keys[MAAGE_INPUT_KEY_SPACE] || jump_pending;
                intent.jump_pressed = jump_pending;
                intent.attack = !was_modal && (input.primary_down || attack_pending);
                intent.use = !was_modal && (input.secondary_down || use_pending);
                previous = player; operation = maage_gameplay_tick(&game, &player, world, items, &intent, trace);
                if (maage_status_failed(operation)) goto done;
                if (maage_status_is_warning(operation)) {
                    if (trace && trace->origin != gameplay_notice) {
                        gameplay_notice = trace->origin;
                        maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.gameplay", operation, trace,
                            "Gameplay action deferred; retaining current simulation state.");
                        maage_client_ui_notify(ui, gameplay_notice, now);
                    }
                    maage_status_trace_init(trace);
                    MAAGE_STATUS_TRACE(trace, 0, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
                }
                operation = maage_world_tick(world);
                if (maage_status_failed(operation)) goto done;
                accumulator -= MAAGE_PLAYER_TICK_SECONDS; ++tick_count;
                jump_pending = attack_pending = use_pending = 0;
                if (!game.health && game.mode == MAAGE_GAME_MODE_SURVIVAL) {
                    maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
                    accumulator = 0; break;
                }
            }
            if (tick_count) {
                tick_seconds = maage_time_monotonic_seconds() - tick_start;
                if (tick_start < 0 || tick_seconds < 0) goto clock_failed;
            }
            alpha = accumulator / MAAGE_PLAYER_TICK_SECONDS;
        } else {
            accumulator = 0; previous = player; alpha = 1; jump_pending = attack_pending = use_pending = 0;
            game.breaking_active = 0; game.break_progress = 0; wheel = 0;
        }
        if (!game.health && game.mode == MAAGE_GAME_MODE_SURVIVAL && !maage_client_ui_modal(ui))
            maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
        operation = maage_gameplay_target(&game, &player, world, yaw, pitch);
        if (maage_status_failed(operation) && maage_status_code(operation) != MAAGE_STATUS_NOT_FOUND) goto done;
        rendered = maage_client_interaction_update(&interaction, renderer, atlas, world, &game, trace);
        if (maage_status_failed(rendered)) goto done;
        if (maage_status_is_warning(rendered)) maage_status_trace_init(trace);
        if (interaction_notice != interaction.last_result) {
            interaction_notice = interaction.last_result; maage_client_ui_notify(ui, interaction_notice, now);
        }
        for (axis = 0; axis < 3; ++axis) eye[axis] = previous.position[axis] + (player.position[axis] - previous.position[axis]) * alpha;
        eye[1] += MAAGE_PLAYER_EYE_HEIGHT;
        maage_world_environment(world, &environment_state);
        rendered = maage_client_environment_update(environment_view, renderer, &environment_state,
            world, &player, yaw, pitch, maage_client_ui_width(ui), maage_client_ui_height(ui), MAAGE_CLIENT_FOV_RADIANS);
        if (maage_status_failed(rendered)) goto done;
        if (state.resized) {
            rendered = maage_renderer_resize(renderer);
            if (maage_status_failed(rendered)) goto done;
        }
        draw_count = resident_count = 0; memset(&mesh_stats, 0, sizeof(mesh_stats));
        for (slot_index = 0; slot_index < cache.capacity; ++slot_index) if (cache.slots[slot_index].used) {
            ++resident_count;
            if (cache.slots[slot_index].mesh) {
                cache.draws[draw_count++] = cache.slots[slot_index].mesh; ++mesh_stats.meshes;
                mesh_stats.vertices += cache.slots[slot_index].vertices;
                mesh_stats.indices += cache.slots[slot_index].indices;
            }
        }
        mesh_activity.status.gpu_resident = resident_count;
        mesh_activity.status.ready_waiting_estimate =
            stats.ready > resident_count ? stats.ready - resident_count : 0;
        if (interaction.mesh) {
            if (!cache.draws && maage_status_failed(maage_client_mesh_cache_grow(&cache))) {
                rendered = MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0); goto done;
            }
            cache.draws[draw_count++] = interaction.mesh;
        }
        debug_text = maage_client_ui_debug_visible(ui) ? maage_client_debug_collect(&debug, now, world, &record,
            &stats, &player, yaw, pitch, options.radius, !active, renderer, &mesh_stats, &game) : NULL;
        maage_client_ui_notice_time(ui, now);
        operation = maage_client_ui_draw(ui, debug_text, &ui_draw);
        if (maage_status_failed(operation)) goto done;
        if (state.framebuffer_width > 0 && state.framebuffer_height > 0) {
            maage_client_camera(matrix, eye, yaw, pitch, (double)state.framebuffer_width / state.framebuffer_height, options.radius);
            rendered = maage_renderer_draw_frame(renderer, cache.draws, draw_count, matrix, ui_draw, trace);
            if (maage_status_failed(rendered)) goto done;
        } else rendered = MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
        frame_end = maage_time_monotonic_seconds();
        if (frame_end < 0) goto clock_failed;
        maage_client_debug_frame(&debug, frame_end, raw_elapsed > 0 ? raw_elapsed : 0,
            maage_status_code(rendered) == MAAGE_STATUS_SUCCESS, tick_count, tick_seconds);
        ++frames;
        if (now - diagnostic_time >= 5.0) {
            maage_log_debug("maage.client.world", "Loaded=%u; pending=%u; ready=%u; generated=%lu; disk=%lu; meshes=%lu; frame=%.1fms.",
                stats.loaded, stats.pending, stats.ready, stats.generated, stats.loaded_from_disk, stats.meshes,
                (now - diagnostic_time) * 1000.0 / frames);
            maage_log_debug("maage.client.render", "GPU uploads=%.1f/s; geometry=%.0f vertices/s; resident=%lu; ready not presented~%lu; budget=%.2fms; last uploads=%u (%.2fms GPU / %.2fms stream); rejected=%lu.",
                mesh_activity.status.uploads_per_second, mesh_activity.status.vertices_per_second,
                (unsigned long)mesh_activity.status.gpu_resident,
                (unsigned long)mesh_activity.status.ready_waiting_estimate, mesh_activity.status.budget_ms,
                mesh_activity.status.uploaded_this_frame, mesh_activity.status.frame_upload_ms,
                mesh_activity.status.stream_ms, mesh_activity.status.rejected);
            maage_log_debug("maage.client.jobs", "Workers=%u; queued=%u; running=%u; generation=%u; lighting=%u; meshing=%u; I/O=%u; completed=%lu; cancelled=%lu; stale=%lu; scheduling=%.2fms.",
                stats.worker_count, stats.queued_jobs, stats.running_jobs, stats.generation_jobs, stats.lighting_jobs,
                stats.meshing_jobs, stats.io_jobs, stats.completed_jobs, stats.cancelled_jobs, stats.stale_jobs,
                stats.scheduling_latency_ms);
            diagnostic_time = now; frames = 0;
        }
        if (maage_status_code(rendered) == MAAGE_STATUS_NOOP) {
            result = maage_window_wait_events(system, 100);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        } else if (maage_client_ui_max_fps(ui) > 0) {
            maage_window_state waited_state;
            double deadline = now + 1.0 / maage_client_ui_max_fps(ui);
            double remaining = deadline - frame_end;
            int milliseconds;
            /* Events may wake a wait early. Keep pumping until the deadline;
             * round upward so a sub-millisecond remainder still yields. */
            while (remaining > 0) {
                milliseconds = (int)ceil(remaining * 1000.0);
                if (milliseconds > 100) milliseconds = 100;
                result = maage_window_wait_events(system, milliseconds);
                if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
                /* Consume resize notifications to let event backends wait
                 * again, retaining the renderer's resize for the next frame. */
                result = maage_window_get_state(window, &waited_state);
                if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
                resize_pending = resize_pending || waited_state.resized;
                if (waited_state.close_requested) break;
                frame_end = maage_time_monotonic_seconds();
                if (frame_end < 0) goto clock_failed;
                remaining = deadline - frame_end;
            }
        }
    }
    goto done;
clock_failed:
    outcome = maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_QUERY, 0);
    failure_message = "Monotonic clock unavailable; fixed-step simulation cannot continue."; goto done;
window_failed:
    outcome = MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    failure_message = "Native window/input cannot continue.";
done:
    if (options_ready) {
        /* Also persist non-menu changes on ordinary shutdown. If AppKit is
         * still transitioning, the UI exposes its last confirmed mode. */
        operation = maage_client_options_store(instance, ui, &saved_video_options, 1);
        if (maage_status_failed(operation))
            maage_log_status(MAAGE_LOG_WARNING, "maage.client.options", operation,
                "Unable to save instance config/options.ini during shutdown.");
    }
    if (ok) outcome = maage_status_make(maage_status_is_success(resource_notice) ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_PARTIAL,
        MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP, 0);
    else if (!maage_status_failed(outcome)) {
        outcome = maage_status_failed(rendered) ? rendered : (maage_status_failed(result) ? result : operation);
        if (!maage_status_failed(outcome)) outcome = maage_status_make(MAAGE_STATUS_FATAL, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE, 0);
    }
    if (!ok) {
        MAAGE_STATUS_PROPAGATE(trace, outcome, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_START);
        if (!world || outcome != maage_world_status(world))
            maage_log_trace_status(MAAGE_LOG_FATAL, "maage.client", outcome, trace, "%s", failure_message);
    }
    maage_status_trace_init(&cleanup_trace);
    if (window) {
        maage_status_trace_init(&cleanup_trace);
        maage_client_cleanup_result(maage_window_capture_mouse(window, 0), &outcome, &cleanup_trace, trace);
    }
    /* Waiting and final player metadata I/O occur only after rendering stops. */
    teardown = world ? maage_world_flush(world, &cleanup_trace) : 0;
    if (maage_status_failed(teardown)) {
        maage_log_info("maage.client.world", "Existing player metadata preserved after the reported World failure.");
        if (!maage_status_failed(outcome)) { outcome = teardown; if (trace) *trace = cleanup_trace; }
        player_ready = 0;
    }
    if (store && player_ready) {
        maage_world_environment(world, &environment_state);
        operation = maage_world_store_save_environment_gameplay(store, player.position,
            yaw, pitch, &game, items, &environment_state, error, &cleanup_trace);
        if (maage_status_failed(operation)) {
            maage_log_trace_status(MAAGE_LOG_ERROR, "maage.client.world", operation, &cleanup_trace, "%s", error);
            if (!maage_status_failed(outcome)) { outcome = operation; if (trace) *trace = cleanup_trace; }
        }
    }
    maage_status_trace_init(&cleanup_trace);
    maage_client_cleanup_result(maage_client_mesh_cache_clear(&cache), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_environment_destroy(environment_view), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_render_mesh_destroy(interaction.mesh), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_ui_destroy(ui), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_render_texture_destroy(atlas), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_renderer_destroy(renderer, &cleanup_trace), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_window_destroy(window), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_window_system_destroy(system), &outcome, &cleanup_trace, trace);
    teardown = maage_world_destroy(world);
    if (maage_status_failed(teardown)) {
        maage_client_storage_retain();
        maage_log_trace_status(MAAGE_LOG_FATAL, "maage.client", teardown, trace,
            "Worker shutdown incomplete; retaining borrowed World storage until process exit.");
        return MAAGE_STATUS_PROPAGATE(trace, teardown, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_DESTROY);
    }
    maage_client_cleanup_result(maage_world_store_close(store), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_block_registry_destroy(registry), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_item_registry_destroy(items), &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_biome_registry_destroy(biomes), &outcome, &cleanup_trace, trace); free(biomes);
    maage_client_cleanup_result(maage_client_resources_destroy(&resources), &outcome, &cleanup_trace, trace);
    return MAAGE_STATUS_PROPAGATE(trace, outcome, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP);
}
