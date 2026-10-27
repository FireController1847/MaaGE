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
#include "debug.h"
#include "interaction.h"
#include "item_entities.h"
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

/* Phase 1 camera tuning, pending 1.7.10 feel/fidelity verification. */
#define MAAGE_CLIENT_MOUSE_RADIANS 0.0025
#define MAAGE_CLIENT_PITCH_LIMIT 1.553343034
#define MAAGE_CLIENT_MAX_FRAME_SECONDS 0.25
/* Presentation policy: discard effects older than the Server's four-tick
 * catch-up window instead of replaying a stale burst after a Client stall. */
#define MAAGE_CLIENT_SOUND_MAX_AGE_TICKS 4UL
#define MAAGE_CLIENT_SOUND_MAX_AGE_SECONDS 0.2

typedef struct maage_client_world_options {
    const char *id, *name;
    maage_world_seed seed;
    unsigned int workers;
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
    int i;
    const char *argument;
    char *end;
    long value;
    unsigned long workers;
    memset(options, 0, sizeof(*options)); options->radius = 8; options->mode = -1;
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
            !strcmp(argument, "--world-workers") || !strcmp(argument, "--mesh-upload-budget-ms") ||
            !strcmp(argument, "--mesh-frame-target-ms") || !strcmp(argument, "--gamemode") ||
            !strcmp(argument, "--interaction-range")) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    }
    if (options->seeded && options->id && !options->create) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    if (options->name && !options->create && !options->seeded) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
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
/* Only the Server takes World meshes. The bounded session mailbox transfers
 * detached CPU ownership; GPU objects and revision keys remain Client-owned. */
static void maage_client_mesh_cache_remove(maage_client_mesh_cache *cache,
    int x, int z, unsigned long revision)
{
    size_t i;
    for (i = 0; i < cache->capacity; ++i) {
        maage_client_chunk_mesh *slot = &cache->slots[i];
        if (slot->used && slot->x == x && slot->z == z &&
            slot->revision <= revision) {
            maage_render_mesh_destroy(slot->mesh);
            memset(slot, 0, sizeof(*slot));
        }
    }
}

static maage_status maage_client_stream_meshes(maage_local_session *session,
    maage_renderer *renderer, maage_render_texture *atlas,
    maage_client_mesh_cache *cache, maage_client_mesh_activity *activity,
    const maage_client_world_options *options, const maage_client_debug *debug,
    unsigned long world_epoch, int center_x, int center_z)
{
    maage_local_mesh_update update;
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
             abs(cache->slots[i].z - center_z) > options->radius)) {
            maage_render_mesh_destroy(cache->slots[i].mesh);
            memset(&cache->slots[i], 0, sizeof(cache->slots[i]));
        }
    }
    begin = maage_time_monotonic_seconds();
    if (begin < 0.0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR,
        MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPDATE, 0);
    for (;;) {
        current = maage_time_monotonic_seconds();
        if (current < 0.0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR,
            MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPDATE, 0);
        if (processed && ((current - begin) * 1000.0 >= budget_ms ||
            (current - begin) * 1000.0 + activity->expected_upload_ms > budget_ms)) break;
        memset(&update, 0, sizeof(update));
        result = maage_local_session_take_mesh(session, &update);
        if (maage_status_code(result) == MAAGE_STATUS_NOOP ||
            maage_status_code(result) == MAAGE_STATUS_INVALID_STATE) break;
        if (maage_status_failed(result)) return result;
        ++processed;
        if (update.removed) {
            if (update.world_epoch == world_epoch)
                maage_client_mesh_cache_remove(cache, update.x, update.z, update.revision);
            if (update.mesh) maage_world_mesh_destroy(update.mesh);
            continue;
        }
        if (!update.mesh || update.world_epoch != world_epoch ||
            abs(update.x - center_x) > options->radius ||
            abs(update.z - center_z) > options->radius) {
            ++activity->status.rejected;
            maage_world_mesh_destroy(update.mesh);
            continue;
        }
        selected = vacant = cache->capacity;
        for (i = 0; i < cache->capacity; ++i) {
            if (cache->slots[i].used && cache->slots[i].x == update.x &&
                cache->slots[i].z == update.z) { selected = i; break; }
            if (!cache->slots[i].used && vacant == cache->capacity) vacant = i;
        }
        if (selected != cache->capacity &&
            cache->slots[selected].revision >= update.revision) {
            ++activity->status.rejected;
            maage_world_mesh_destroy(update.mesh);
            continue;
        }
        if (selected == cache->capacity) {
            selected = vacant;
            if (selected == cache->capacity) {
                selected = cache->capacity;
                result = maage_client_mesh_cache_grow(cache);
                if (maage_status_failed(result)) {
                    maage_world_mesh_destroy(update.mesh);
                    return result;
                }
            }
        }
        geometry = maage_world_mesh_data(update.mesh);
        replacement = NULL;
        result = MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS,
            MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_SUBMIT, 0);
        if (geometry && geometry->index_count) {
            upload_begin = maage_time_monotonic_seconds();
            if (upload_begin < 0.0) {
                maage_world_mesh_destroy(update.mesh);
                return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR,
                    MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPDATE, 0);
            }
            result = maage_renderer_upload_geometry(renderer, geometry,
                atlas, &replacement, NULL);
            current = maage_time_monotonic_seconds();
            if (current < 0.0) {
                maage_render_mesh_destroy(replacement);
                maage_world_mesh_destroy(update.mesh);
                return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR,
                    MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPDATE, 0);
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
            cache->slots[selected].x = update.x;
            cache->slots[selected].z = update.z;
            cache->slots[selected].revision = update.revision;
            cache->slots[selected].vertices = geometry ? geometry->vertex_count : 0;
            cache->slots[selected].indices = geometry ? geometry->index_count : 0;
        } else {
            maage_render_mesh_destroy(replacement);
            if (maage_status_code(result) == MAAGE_STATUS_ALLOCATION_FAILED) {
                if (activity->last_result != result) {
                    activity->last_result = result;
                    maage_log_status(MAAGE_LOG_WARNING, "maage.client.render", result,
                        "GPU upload failed for column %d,%d; retaining old visual.",
                        update.x, update.z);
                }
                maage_world_mesh_destroy(update.mesh);
                continue;
            }
            maage_world_mesh_destroy(update.mesh);
            return result;
        }
        maage_world_mesh_destroy(update.mesh);
    }
    current = maage_time_monotonic_seconds();
    if (current < 0.0) return MAAGE_STATUS_VALUE(MAAGE_STATUS_ERROR,
        MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPDATE, 0);
    activity->previous_upload_ms = activity->status.frame_upload_ms;
    activity->status.stream_ms = (current - begin) * 1000.0;
    maage_client_mesh_rates(activity, current);
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS,
        MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPDATE, 0);
}

typedef struct maage_client_fov {
    double multiplier;
    unsigned long session_epoch, world_epoch;
    int initialized;
} maage_client_fov;

static double maage_client_fov_update(maage_client_fov *view,
    const maage_session_snapshot *snapshot, double elapsed)
{
    double target = snapshot->player.player.sprinting ?
        (MAAGE_PLAYER_SPRINT_SPEED + 1.0) * 0.5 : 1.0;
    if (snapshot->player.flying) target *= 1.1;
    if (!view->initialized || view->session_epoch != snapshot->session_epoch ||
        view->world_epoch != snapshot->world_epoch) {
        view->multiplier = target; view->initialized = 1;
        view->session_epoch = snapshot->session_epoch; view->world_epoch = snapshot->world_epoch;
    } else if (!snapshot->paused && elapsed > 0) {
        /* Half the remaining difference per reference 20 Hz tick, expressed
         * continuously so rendering FPS and duplicate snapshots cannot reset it.
         * A newly observed target never fast-forwards across a long frame stall. */
        if (elapsed > 0.1) elapsed = 0.1;
        view->multiplier += (target-view->multiplier) *
            (1.0-pow(0.5, elapsed/MAAGE_PLAYER_TICK_SECONDS));
    }
    return view->multiplier;
}

static void maage_client_camera(float output[16], const double eye[3], double yaw, double pitch,
    double aspect, int radius, double fov, const float bob[16])
{
    double sy = sin(yaw), cy = cos(yaw), sp = sin(pitch), cp = cos(pitch);
    double forward[3], right[3], up[3], focal, far_clip;
    float view[16], projection[16], bobbed_view[16];
    int row, column, k;
    forward[0] = sy * cp; forward[1] = -sp; forward[2] = -cy * cp;
    right[0] = cy; right[1] = 0; right[2] = sy;
    up[0] = sy * sp; up[1] = cp; up[2] = -cy * sp;
    memset(view, 0, sizeof(view)); memset(projection, 0, sizeof(projection));
    for (k = 0; k < 3; ++k) {
        view[k * 4] = (float)right[k]; view[k * 4 + 1] = (float)up[k]; view[k * 4 + 2] = (float)-forward[k];
        view[12] -= (float)(right[k] * eye[k]); view[13] -= (float)(up[k] * eye[k]); view[14] += (float)(forward[k] * eye[k]);
    }
    view[15] = 1.0f; focal = 1.0 / tan(fov * 0.5);
    projection[0] = (float)(focal / aspect); projection[5] = (float)focal;
    /* Include the furthest chunk corner and full world height, even while
     * moving within the center chunk; keep the established minimum range. */
    far_clip = ((double)radius + 2.0) * MAAGE_CHUNK_EDGE * 1.4142135623730951 + MAAGE_WORLD_HEIGHT;
    if (far_clip < 512.0) far_clip = 512.0;
    /* Right-handed perspective, near=.05, depth [0,1]. */
    projection[10] = (float)(far_clip / (0.05 - far_clip)); projection[11] = -1.0f;
    projection[14] = (float)(0.05 * far_clip / (0.05 - far_clip));
    for (column = 0; column < 4; ++column) for (row = 0; row < 4; ++row) {
        bobbed_view[column * 4 + row] = 0;
        for (k = 0; k < 4; ++k)
            bobbed_view[column * 4 + row] += bob[k * 4 + row] * view[column * 4 + k];
    }
    for (column = 0; column < 4; ++column) for (row = 0; row < 4; ++row) {
        output[column * 4 + row] = 0;
        for (k = 0; k < 4; ++k) output[column * 4 + row] += projection[k * 4 + row] * bobbed_view[column * 4 + k];
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
    return command->kind == MAAGE_CMD_INPUT && !command->intent.jump_pressed &&
        !command->intent.attack_pressed && !command->intent.use_pressed && !command->intent.forward_pressed;
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
            outbox->commands[previous].intent.movement.jump == command.intent.movement.jump &&
            outbox->commands[previous].intent.movement.forward == command.intent.movement.forward &&
            outbox->commands[previous].intent.movement.strafe == command.intent.movement.strafe &&
            outbox->commands[previous].intent.movement.sprint == command.intent.movement.sprint &&
            outbox->commands[previous].intent.descend == command.intent.descend) {
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

static void maage_client_audio_click(void *context)
{
    maage_client_audio_service *audio = (maage_client_audio_service *)context;
    if (audio) (void)maage_client_audio_service_ui_click(audio, NULL);
}

/* The presentation thread never owns a World or a mutable gameplay instance.
 * It sends commands to the same reusable Server runtime as dedicated hosting,
 * consumes copied snapshots, and owns only GUI/audio/GPU presentation state. */
maage_status maage_client_run(int argc, char *const argv[], maage_status_trace *trace)
{
    maage_window_system *system = NULL;
    maage_window *window = NULL;
    maage_renderer *renderer = NULL;
    maage_render_texture *atlas = NULL;
    maage_client_ui *ui = NULL;
    maage_client_frontend *frontend = NULL;
    maage_client_world_choice choice;
    int direct_world = 0;
    maage_client_environment *environment_view = NULL;
    maage_client_audio_service *audio = NULL;
    maage_local_session *session = NULL;
    maage_server_runtime *server = NULL;
    maage_content *content = NULL;
    maage_biome_registry *biomes = NULL;
    const maage_block_registry *blocks = NULL;
    const maage_item_registry *items = NULL;
    maage_instance *instance = maage_client_storage_instance();
    maage_client_resources resources;
    maage_client_mesh_cache cache;
    maage_client_mesh_activity mesh_activity;
    maage_client_world_options options;
    maage_client_options video_options, saved_video_options, current_options;
    maage_client_interaction_visual interaction;
    maage_client_first_person first_person;
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
    maage_session_snapshot latest, previous, incoming;
    maage_session_event event;
    maage_session_command command;
    maage_client_outbox outbox;
    maage_status result = 0, operation = 0, rendered = 0, outcome = 0;
    maage_status resource_notice = 0, mesh_notice = 0, audio_notice = 0;
    maage_status_trace cleanup_trace;
    const char *failure_message = "Client presentation cannot continue.";
    char directory[MAAGE_PATH_CAPACITY];
    char window_title[128];
    double last = 0.0, now = 0.0, raw_elapsed = 0.0, frame_end = 0.0;
    double diagnostic_time = 0.0, received_time = 0.0, alpha = 1.0, startup_began = 0.0;
    maage_client_pacing pacing = {0, 0.0};
    maage_client_fov fov = {0.0, 0, 0, 0};
    double effective_fov;
    double eye[3], forward[3], yaw = 0.0, pitch = 0.0, wheel = 0.0;
    double render_yaw = 0.0, render_pitch = 0.0, yaw_arc = 0.0;
    unsigned long world_epoch = 0, last_sound_sequence = 0, last_seen_tick = 0;
    unsigned long sound_epoch = 0, sound_world_epoch = 0, frames = 0, tick_count = 0;
    float matrix[16];
    size_t draw_count, resident_count, slot_index;
    int valid_snapshot = 0, ok = 0, options_ready = 0, server_started = 0, i, axis;
    int initial_capture = 1, active = 0, was_active = 0;
    int was_modal = 0, was_focused = 0, changing_mode = 0, was_changing_mode = 0;
    int was_options_visible = 0, options_save_needed = 0, resize_pending = 0;
    int logged_max_fps = -1, logged_vsync = -1, paused_command = -1;
    int selected_hotbar = 0, queued_hotbar = -1, view_sent = 0, disconnected = 0, cx = 0, cz = 0;
    int jump_latched = 0, attack_latched = 0, use_latched = 0, respawn_latched = 0, forward_latched = 0;
    unsigned int ui_actions = 0, event_budget;
    memset(&resources, 0, sizeof(resources));
    memset(&cache, 0, sizeof(cache));
    memset(&mesh_activity, 0, sizeof(mesh_activity));
    memset(&interaction, 0, sizeof(interaction));
    memset(&first_person, 0, sizeof(first_person));
    memset(&debug, 0, sizeof(debug));
    memset(&outbox, 0, sizeof(outbox));
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
        operation = maage_status_make(MAAGE_STATUS_INVALID_STATE,
            MAAGE_CONTEXT_INSTANCE, MAAGE_OPERATION_SELECT, 0);
        failure_message = "No validated instance was selected at startup.";
        goto done;
    }
    operation = maage_client_options_load(instance, &video_options);
    if (maage_status_failed(operation))
        maage_log_status(MAAGE_LOG_WARNING, "maage.client.options", operation,
            "Unable to read config/options.ini; using defaults.");
    saved_video_options = video_options;
    strcpy(directory, maage_client_storage_data_root());
    maage_log_info("maage.client.instance", "Instance: %s (%s).",
        maage_instance_get_info(instance)->name,
        maage_instance_get_info(instance)->id);
    biomes = (maage_biome_registry *)calloc(1, sizeof(*biomes));
    if (!biomes) {
        operation = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
        goto done;
    }
    operation = maage_biome_registry_init(biomes);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_resources_load(directory, &resources, biomes, trace);
    if (maage_status_failed(operation)) goto done;
    resource_notice = resources.load_status;
    maage_status_trace_init(trace);
    operation = maage_biome_registry_finalize(biomes);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_content_create(&resources, biomes, &content, trace);
    if (maage_status_failed(operation)) {
        failure_message = "Shared immutable content registration failed.";
        goto done;
    }
    blocks = maage_content_blocks(content);
    items = maage_content_items(content);
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
    render_config.window = window;
    render_config.required_features |= MAAGE_RENDER_CAP_TEXTURED_MESH |
        MAAGE_RENDER_CAP_UI | MAAGE_RENDER_CAP_SCENE;
    rendered = maage_renderer_create(&render_config, &renderer, trace);
    if (maage_status_failed(rendered)) goto done;
    rendered = maage_renderer_create_texture(renderer, &resources.atlas, &atlas, trace);
    if (maage_status_code(rendered) != MAAGE_STATUS_SUCCESS) goto done;
    operation = maage_client_interaction_init(&interaction, &resources);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_item_entities_create(renderer, atlas, &resources, items, blocks, &item_entities, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_first_person_init(&first_person, &resources);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_ui_create(renderer, window, directory, &resources.atlas, &ui, trace);
    (void)maage_image_destroy(&resources.image);
    resources.atlas.pixels = NULL;
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_ui_set_content(ui, items, blocks, first_person.grass_color, resources.uv);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_frontend_create(ui, instance, items, &frontend, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_client_ui_finalize(ui);
    if (maage_status_failed(operation)) goto done;
    if (maage_status_is_success(resource_notice))
        resource_notice = maage_client_ui_resource_status(ui);
    operation = maage_client_environment_create(renderer, directory, &environment_view, trace);
    if (maage_status_failed(operation)) goto done;
    if (maage_status_is_success(resource_notice))
        resource_notice = maage_client_environment_resource_status(environment_view, NULL);
    /* The service remains valid in silent-device recovery. Rendering and the
     * authoritative Server must not depend on sound playback succeeding. */
    operation = maage_client_audio_native_install();
    if (maage_status_failed(operation)) {
        maage_log_status(MAAGE_LOG_WARNING, "maage.client.audio", operation,
            "Audio component registration failed; falling back to silent service.");
    }
    operation = maage_client_audio_service_create(directory, &video_options, &audio, trace);
    if (maage_status_failed(operation) && !audio) {
        maage_log_status(MAAGE_LOG_WARNING, "maage.client.audio", operation,
            "Audio setup failed; gameplay presentation continues.");
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
choose_world:
    fov.initialized = 0;
    if (!direct_world) {
        operation = maage_client_frontend_run(frontend, system, audio, &choice, trace);
        if (maage_status_code(operation) == MAAGE_STATUS_NOOP) { ok = 1; goto done; }
        if (maage_status_failed(operation)) goto done;
        maage_client_ui_get_options(ui, &video_options);
        operation = maage_client_options_store(instance, ui, &saved_video_options, 1);
        if (maage_status_failed(operation)) goto done;
        options.id = choice.create ? NULL : choice.id;
        options.name = choice.create ? choice.name : NULL;
        options.create = choice.create; options.mode = choice.create ? choice.mode : -1;
        options.seeded = choice.create && choice.seed[0];
        if (options.seeded) {
            operation = maage_world_seed_parse(choice.seed, &options.seed);
            if (maage_status_failed(operation)) goto done;
        }
    }
    direct_world = 0;
    memset(&outbox, 0, sizeof(outbox));
    memset(&mesh_activity, 0, sizeof(mesh_activity));
    world_epoch = last_sound_sequence = last_seen_tick = sound_epoch = sound_world_epoch = 0;
    active = was_active = was_modal = was_focused = was_changing_mode = 0;
    paused_command = -1; view_sent = disconnected = 0; initial_capture = 1;
    jump_latched = attack_latched = use_latched = respawn_latched = forward_latched = 0;
    pacing.deadline = 0; pacing.fps = 0;
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
    server_config.content = content;
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
        if (state.close_requested) { ok = 1; goto done; }
        operation = maage_local_session_read_snapshot(session, &incoming);
        if (maage_status_code(operation) == MAAGE_STATUS_SUCCESS &&
            incoming.protocol_version == MAAGE_SESSION_PROTOCOL_VERSION &&
            incoming.sequence) {
            latest = previous = incoming;
            world_epoch = latest.world_epoch;
            last_seen_tick = latest.tick;
            selected_hotbar = queued_hotbar = (int)latest.player.inventory.selected;
            valid_snapshot = 1;
            break;
        }
        if (maage_status_failed(operation) &&
            maage_status_code(operation) != MAAGE_STATUS_NOOP) goto done;
        if (maage_server_runtime_finished(server)) {
            operation = maage_status_make(MAAGE_STATUS_INVALID_STATE,
                MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_START, 0);
            failure_message = "The authoritative Server exited before producing an initial snapshot.";
            goto done;
        }
        now = maage_time_monotonic_seconds();
        if (now < 0.0) goto clock_failed;
        if (now - startup_began >= 120.0) {
            operation = maage_status_make(MAAGE_STATUS_INVALID_STATE,
                MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_START, 0);
            failure_message = "Integrated Server bootstrap timed out without a copied snapshot.";
            goto done;
        }
        operation = maage_client_frontend_loading(frontend, trace);
        if (maage_status_failed(operation)) goto done;
        result = maage_window_wait_events(system, 1);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
    }
    while (maage_client_ui_modal(ui)) (void)maage_client_ui_back(ui);
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
    if (maage_status_failed(operation))
        maage_log_status(MAAGE_LOG_WARNING, "maage.client.options", operation,
            "Some saved video choices could not be applied.");
    options_ready = 1;
    yaw = latest.player.yaw; pitch = latest.player.pitch;
    last = maage_time_monotonic_seconds();
    if (last < 0.0) goto clock_failed;
    received_time = diagnostic_time = mesh_activity.sample_start = last;
    maage_client_ui_notify(ui, resource_notice, last);
    if (audio_notice) maage_client_ui_notify(ui, audio_notice, last);
    maage_log_info("maage.client",
        "Integrated authoritative Server active. Input/UI use numeric commands; GPU streaming and audio consume copied Server publications.");
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
        now = maage_time_monotonic_seconds();
        if (now < 0.0) goto clock_failed;
        raw_elapsed = now - last; last = now;
        if (raw_elapsed < 0.0) raw_elapsed = 0.0;
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
        ui_input.mouse_x = (float)input.mouse_x;
        ui_input.mouse_y = (float)input.mouse_y;
        operation = maage_client_ui_set_safe_area(ui, &state.safe_area);
        if (maage_status_failed(operation)) goto done;
        operation = maage_client_ui_set_window_state(ui, &state);
        if (maage_status_failed(operation)) goto done;
        ui_actions = 0;
        operation = maage_client_ui_update(ui, &ui_input,
            state.framebuffer_width, state.framebuffer_height,
            state.width, state.height, &ui_actions);
        if (maage_status_failed(operation)) {
            maage_client_ui_notify(ui, operation, now);
            maage_log_status(MAAGE_LOG_WARNING, "maage.client.ui", operation,
                "Presentation UI rejected an action.");
        }
        changing_mode = maage_client_ui_window_mode_changing(ui);
        if (was_options_visible && !maage_client_ui_options_visible(ui))
            options_save_needed = 1;
        if (was_changing_mode && !changing_mode) {
            initial_capture = 1; options_save_needed = 1;
        }
        if (options_save_needed && !changing_mode) {
            operation = maage_client_options_store(instance, ui, &saved_video_options, 0);
            options_save_needed = 0;
            if (maage_status_failed(operation)) {
                maage_client_ui_notify(ui, operation, now);
                maage_log_status(MAAGE_LOG_WARNING, "maage.client.options",
                    operation, "Unable to save config/options.ini.");
            }
        }
        if (!changing_mode && !was_changing_mode &&
            ((was_focused && !input.focused) || (was_active && !input.captured))) {
            if (!maage_client_ui_modal(ui))
                (void)maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
        }
        was_changing_mode = changing_mode;
        was_focused = input.focused;
        if (ui_actions & MAAGE_CLIENT_UI_EXIT) goto return_to_title;
        if (maage_client_ui_modal(ui)) {
            initial_capture = 0;
            if (input.captured) result = maage_window_capture_mouse(window, 0);
            input.captured = 0;
        } else if (!changing_mode && input.focused && !input.captured &&
            (initial_capture || was_modal || input.primary_pressed)) {
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
        was_active = active;
        if (logged_max_fps != maage_client_ui_max_fps(ui) ||
            logged_vsync != maage_client_ui_vsync(ui)) {
            logged_max_fps = maage_client_ui_max_fps(ui);
            logged_vsync = maage_client_ui_vsync(ui);
            maage_log_info("maage.client.options", "FPS=%d (zero unlimited); VSync=%s.",
                logged_max_fps, logged_vsync ? "on" : "off");
        }
        /* First advance queued reliable commands; transport queue-full is
         * retained as backpressure and never treated as successful delivery. */
        operation = maage_client_outbox_flush(&outbox, session);
        if (maage_status_failed(operation) &&
            maage_status_code(operation) != MAAGE_STATUS_BUFFER_OVERFLOW) goto done;
        if (ui_actions & MAAGE_CLIENT_UI_RESPAWN) respawn_latched = 1;
        if (respawn_latched) {
            memset(&command, 0, sizeof(command));
            command.kind = MAAGE_CMD_RESPAWN;
            operation = maage_client_outbox_queue(&outbox, &command);
            if (!maage_status_failed(operation)) {
                respawn_latched = 0;
                (void)maage_client_ui_back(ui);
            } else maage_client_ui_notify(ui, operation, now);
        }
        for (i = 0; i < 64 && outbox.count < MAAGE_CLIENT_OUTBOX_CAPACITY; ++i) {
            operation = maage_client_ui_take_command(ui, &command);
            if (maage_status_code(operation) == MAAGE_STATUS_NOOP) break;
            if (maage_status_failed(operation)) {
                maage_client_ui_notify(ui, operation, now); break;
            }
            operation = maage_client_outbox_queue(&outbox, &command);
            if (maage_status_failed(operation)) {
                /* The GUI request was taken but never submitted. Return it to
                 * Agent 10's FIFO head so retries retain causal ordering. */
                (void)maage_client_ui_retry_command(ui, &command);
                maage_client_ui_notify(ui, operation, now);
                break;
            }
        }
        if (paused_command != (maage_client_ui_modal(ui) ? 1 : 0) &&
            outbox.count < MAAGE_CLIENT_OUTBOX_CAPACITY) {
            memset(&command, 0, sizeof(command));
            command.kind = MAAGE_CMD_PAUSE;
            command.paused = maage_client_ui_modal(ui) ? 1 : 0;
            operation = maage_client_outbox_queue(&outbox, &command);
            if (!maage_status_failed(operation)) paused_command = command.paused;
        }
        if (!view_sent || maage_client_ui_leaves_mode(ui) != video_options.leaves) {
            if (outbox.count < MAAGE_CLIENT_OUTBOX_CAPACITY) {
                memset(&command, 0, sizeof(command));
                command.kind = MAAGE_CMD_VIEW;
                command.render_radius = options.radius;
                command.leaves_mode = maage_client_ui_leaves_mode(ui);
                command.view_yaw = yaw;
                operation = maage_client_outbox_queue(&outbox, &command);
                if (!maage_status_failed(operation)) {
                    video_options.leaves = maage_client_ui_leaves_mode(ui);
                    view_sent = 1;
                }
            }
        }
        if (active) {
            yaw = fmod(yaw + input.mouse_delta_x * MAAGE_CLIENT_MOUSE_RADIANS, 6.283185307179586);
            pitch += input.mouse_delta_y * MAAGE_CLIENT_MOUSE_RADIANS;
            if (pitch > MAAGE_CLIENT_PITCH_LIMIT) pitch = MAAGE_CLIENT_PITCH_LIMIT;
            if (pitch < -MAAGE_CLIENT_PITCH_LIMIT) pitch = -MAAGE_CLIENT_PITCH_LIMIT;
        }
        if (active && outbox.count < MAAGE_CLIENT_OUTBOX_CAPACITY) {
            for (i = 0; i < (int)MAAGE_HOTBAR_SLOTS; ++i) {
                if (input.pressed[MAAGE_INPUT_KEY_1 + i]) selected_hotbar = i;
            }
            wheel += input.scroll_y;
            if (wheel >= 1.0 || wheel <= -1.0) {
                i = (int)wheel;
                selected_hotbar = (selected_hotbar - i + 900) % (int)MAAGE_HOTBAR_SLOTS;
                wheel -= (double)i;
            }
            if (selected_hotbar != queued_hotbar) {
                memset(&command, 0, sizeof(command));
                command.kind = MAAGE_CMD_HOTBAR;
                command.hotbar_slot = (unsigned int)selected_hotbar;
                operation = maage_client_outbox_queue(&outbox, &command);
                if (maage_status_failed(operation))
                    maage_client_ui_notify(ui, operation, now);
                else queued_hotbar = selected_hotbar;
            }
            if (input.pressed[MAAGE_INPUT_KEY_Q]) {
                memset(&command, 0, sizeof(command)); command.kind = MAAGE_CMD_DROP;
                command.inventory_slot = (unsigned int)selected_hotbar;
                command.secondary_click = input.keys[MAAGE_INPUT_KEY_CONTROL] != 0;
                operation = maage_client_outbox_queue(&outbox, &command);
                if (maage_status_failed(operation)) maage_client_ui_notify(ui, operation, now);
            }
        } else wheel = 0.0;
        if (active) {
            if (input.pressed[MAAGE_INPUT_KEY_SPACE]) jump_latched = 1;
            if (input.pressed[MAAGE_INPUT_KEY_W] && !input.keys[MAAGE_INPUT_KEY_S]) forward_latched = 1;
            if (input.primary_pressed) attack_latched = 1;
            if (input.secondary_pressed) use_latched = 1;
        } else {
            jump_latched = attack_latched = use_latched = forward_latched = 0;
        }
        if (outbox.count < MAAGE_CLIENT_OUTBOX_CAPACITY) {
            memset(&command, 0, sizeof(command));
            command.kind = MAAGE_CMD_INPUT;
            command.intent.movement.yaw = yaw;
            command.intent.pitch = pitch;
            if (active) {
                command.intent.movement.forward =
                    (double)(input.keys[MAAGE_INPUT_KEY_W] - input.keys[MAAGE_INPUT_KEY_S]);
                command.intent.movement.strafe =
                    (double)(input.keys[MAAGE_INPUT_KEY_D] - input.keys[MAAGE_INPUT_KEY_A]);
                command.intent.movement.jump = input.keys[MAAGE_INPUT_KEY_SPACE];
                command.intent.movement.sprint = input.keys[MAAGE_INPUT_KEY_LEFT_CONTROL];
                command.intent.forward_pressed = forward_latched;
                command.intent.jump_pressed = jump_latched;
                command.intent.descend = input.keys[MAAGE_INPUT_KEY_SHIFT];
                command.intent.attack = input.primary_down || attack_latched;
                command.intent.use = input.secondary_down || use_latched;
                command.intent.attack_pressed = attack_latched;
                command.intent.use_pressed = use_latched;
            }
            operation = maage_client_outbox_queue(&outbox, &command);
            if (maage_status_failed(operation))
                maage_client_ui_notify(ui, operation, now);
            else jump_latched = attack_latched = use_latched = forward_latched = 0;
        } else {
            /* The input edge latches above are retained until an ordered
             * command can be queued; never acknowledge an unsent click. */
            maage_client_ui_notify(ui, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW,
                MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SUBMIT, 0), now);
        }
        operation = maage_client_outbox_flush(&outbox, session);
        if (maage_status_failed(operation) &&
            maage_status_code(operation) != MAAGE_STATUS_BUFFER_OVERFLOW) goto done;
        operation = maage_local_session_read_snapshot(session, &incoming);
        if (maage_status_code(operation) == MAAGE_STATUS_SUCCESS &&
            incoming.protocol_version == MAAGE_SESSION_PROTOCOL_VERSION &&
            (incoming.session_epoch != latest.session_epoch ||
             incoming.world_epoch != latest.world_epoch ||
             incoming.sequence > latest.sequence)) {
            if (incoming.session_epoch != latest.session_epoch ||
                incoming.world_epoch != latest.world_epoch) {
                (void)maage_client_mesh_cache_clear(&cache);
                previous = incoming;
                world_epoch = incoming.world_epoch;
                view_sent = 0;
                sound_epoch = sound_world_epoch = last_sound_sequence = 0;
                operation = maage_client_audio_service_reset_world(audio, trace);
                if (maage_status_failed(operation)) goto done;
                yaw = incoming.player.yaw; pitch = incoming.player.pitch;
                selected_hotbar = queued_hotbar = (int)incoming.player.inventory.selected;
                jump_latched = attack_latched = use_latched = respawn_latched = forward_latched = 0;
            } else previous = latest;
            latest = incoming;
            received_time = now;
            operation = maage_client_ui_set_snapshot(ui, &latest);
            if (maage_status_failed(operation)) goto done;
            if (!latest.player.health &&
                latest.player.game_mode == MAAGE_GAME_MODE_SURVIVAL &&
                !maage_client_ui_modal(ui))
                (void)maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
        } else if (maage_status_failed(operation) &&
            maage_status_code(operation) != MAAGE_STATUS_NOOP &&
            maage_status_code(operation) != MAAGE_STATUS_INVALID_STATE) goto done;
        for (event_budget = 0; event_budget < 256U; ++event_budget) {
            operation = maage_local_session_take_event(session, &event);
            if (maage_status_code(operation) == MAAGE_STATUS_NOOP ||
                maage_status_code(operation) == MAAGE_STATUS_INVALID_STATE) break;
            if (maage_status_failed(operation)) goto done;
            if (event.protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) continue;
            if (event.kind == MAAGE_EVENT_DISCONNECTED) {
                disconnected = 1;
                if (maage_status_failed(event.status)) {
                    outcome = event.status;
                    if (event.has_trace && trace) *trace = event.trace;
                } else ok = 1;
                break;
            }
            if (event.session_epoch != latest.session_epoch ||
                event.world_epoch != latest.world_epoch) continue;
            if (event.kind == MAAGE_EVENT_SOUND) {
                if (sound_epoch != event.session_epoch ||
                    sound_world_epoch != event.world_epoch) {
                    last_sound_sequence = 0;
                    sound_epoch = event.session_epoch;
                    sound_world_epoch = event.world_epoch;
                }
                if (event.sequence > last_sound_sequence) {
                    last_sound_sequence = event.sequence;
                    if (audio && (latest.tick <= event.tick ||
                        latest.tick - event.tick <= MAAGE_CLIENT_SOUND_MAX_AGE_TICKS) &&
                        (event.emitted_at < 0.0 || now - event.emitted_at <=
                            MAAGE_CLIENT_SOUND_MAX_AGE_SECONDS))
                        (void)maage_client_audio_service_event(audio, &event.sound, trace);
                }
            } else if (event.kind == MAAGE_EVENT_STATUS) {
                maage_client_ui_notify(ui, event.status, now);
                if (maage_status_failed(event.status))
                    maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.server",
                        event.status, event.has_trace ? &event.trace : NULL,
                        "Authoritative Server reported an operation failure.");
            } else if (event.kind == MAAGE_EVENT_MESH_REMOVE) {
                maage_client_mesh_cache_remove(&cache,
                    event.column_x, event.column_z, event.mesh_revision);
            }
        }
        if (disconnected) break;
        if (maage_server_runtime_finished(server)) {
            /* Drain any terminal STATUS/DISCONNECT next frame; a closed
             * Server without a terminal event is not successful gameplay. */
            operation = maage_status_make(MAAGE_STATUS_INVALID_STATE,
                MAAGE_CONTEXT_SERVER, MAAGE_OPERATION_STOP, 0);
            failure_message = "The authoritative Server stopped unexpectedly.";
            goto done;
        }
        cx = maage_world_chunk_coordinate((int)floor(latest.player.player.position[0]));
        cz = maage_world_chunk_coordinate((int)floor(latest.player.player.position[2]));
        rendered = maage_client_stream_meshes(session, renderer, atlas, &cache,
            &mesh_activity, &options, &debug, world_epoch, cx, cz);
        if (maage_status_failed(rendered)) goto done;
        if (mesh_notice != mesh_activity.last_result) {
            mesh_notice = mesh_activity.last_result;
            maage_client_ui_notify(ui, mesh_notice, now);
        }
        alpha = (now - received_time) / MAAGE_PLAYER_TICK_SECONDS;
        if (alpha < 0.0) alpha = 0.0;
        if (alpha > 1.0 || previous.tick == latest.tick) alpha = 1.0;
        if (previous.world_epoch != latest.world_epoch ||
            previous.session_epoch != latest.session_epoch) alpha = 1.0;
        /* Shortest-arc angular interpolation uses only copied authoritative
         * snapshots. Local mouse-look is an independent presentation override
         * while captured; it never changes the Server state in a snapshot. */
        yaw_arc = atan2(sin(latest.player.yaw - previous.player.yaw),
            cos(latest.player.yaw - previous.player.yaw));
        render_yaw = previous.player.yaw + yaw_arc * alpha;
        render_pitch = previous.player.pitch +
            (latest.player.pitch - previous.player.pitch) * alpha;
        if (active) {
            render_yaw = yaw; render_pitch = pitch;
        } else {
            yaw = render_yaw; pitch = render_pitch;
        }
        for (axis = 0; axis < 3; ++axis)
            eye[axis] = previous.player.player.position[axis] +
                (latest.player.player.position[axis] -
                previous.player.player.position[axis]) * alpha;
        eye[1] += MAAGE_PLAYER_EYE_HEIGHT;
        forward[0] = sin(render_yaw) * cos(render_pitch);
        forward[1] = -sin(render_pitch);
        forward[2] = -cos(render_yaw) * cos(render_pitch);
        if (audio) {
            maage_client_ui_get_options(ui, &current_options);
            (void)maage_client_audio_service_apply_options(audio, &current_options);
            (void)maage_client_audio_service_update(audio, &latest,
                eye, forward, now, trace);
        }
        rendered = maage_client_interaction_update(&interaction, renderer,
            atlas, blocks, &latest, trace);
        if (maage_status_failed(rendered)) goto done;
        effective_fov = maage_client_ui_fov_radians(ui) * maage_client_fov_update(&fov, &latest, raw_elapsed);
        rendered = maage_client_environment_update(environment_view,
            renderer, &latest, maage_client_ui_width(ui),
            maage_client_ui_height(ui), effective_fov);
        if (maage_status_failed(rendered)) goto done;
        if (state.resized) {
            rendered = maage_renderer_resize(renderer);
            if (maage_status_failed(rendered)) goto done;
        }
        draw_count = resident_count = 0;
        memset(&mesh_stats, 0, sizeof(mesh_stats));
        for (slot_index = 0; slot_index < cache.capacity; ++slot_index)
            if (cache.slots[slot_index].used) {
                ++resident_count;
                if (cache.slots[slot_index].mesh) {
                    cache.draws[draw_count++] = cache.slots[slot_index].mesh;
                    ++mesh_stats.meshes;
                    mesh_stats.vertices += cache.slots[slot_index].vertices;
                    mesh_stats.indices += cache.slots[slot_index].indices;
                }
            }
        mesh_activity.status.gpu_resident = resident_count;
        mesh_activity.status.ready_waiting_estimate =
            latest.world_stats.ready > resident_count ?
                latest.world_stats.ready - resident_count : 0;
        if (interaction.mesh) {
            if (!cache.draws && maage_status_failed(maage_client_mesh_cache_grow(&cache))) {
                rendered = MAAGE_STATUS_VALUE(MAAGE_STATUS_ALLOCATION_FAILED,
                    MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_CREATE, 0);
                goto done;
            }
            cache.draws[draw_count++] = interaction.mesh;
        }
        debug_text = maage_client_ui_debug_visible(ui) ?
            maage_client_debug_collect(&debug, now, &latest, options.radius,
                !active, renderer, &mesh_stats) : NULL;
        maage_client_ui_notice_time(ui, now);
        operation = maage_client_ui_draw(ui, debug_text, &ui_draw);
        if (maage_status_failed(operation)) goto done;
        if (state.framebuffer_width > 0 && state.framebuffer_height > 0) {
            rendered = maage_client_first_person_update(&first_person, renderer, atlas,
                items, blocks, &previous, &latest, alpha, eye, render_yaw, render_pitch,
                raw_elapsed, (double)state.framebuffer_width / state.framebuffer_height,
                !maage_client_ui_modal(ui), trace);
            if (maage_status_failed(rendered)) goto done;
            maage_client_camera(matrix, eye, render_yaw, render_pitch,
                (double)state.framebuffer_width / state.framebuffer_height,
                options.radius, effective_fov, first_person.camera_bob);
            rendered = maage_client_item_entities_update(item_entities, session, &latest, alpha, eye, now, trace);
            if (maage_status_failed(rendered)) goto done;
            if (maage_status_is_warning(rendered)) maage_client_ui_notify(ui, rendered, now);
            rendered = maage_renderer_draw_frame(renderer, cache.draws,
                draw_count, matrix, ui_draw, trace);
            if (maage_status_failed(rendered)) goto done;
        } else rendered = MAAGE_STATUS_VALUE(MAAGE_STATUS_NOOP,
            MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_UPDATE, 0);
        frame_end = maage_time_monotonic_seconds();
        if (frame_end < 0.0) goto clock_failed;
        tick_count = latest.tick >= last_seen_tick ?
            latest.tick - last_seen_tick : 0;
        last_seen_tick = latest.tick;
        maage_client_debug_frame(&debug, frame_end, raw_elapsed,
            maage_status_code(rendered) == MAAGE_STATUS_SUCCESS,
            tick_count, 0.0);
        ++frames;
        if (now - diagnostic_time >= 5.0) {
            maage_log_verbose("maage.client.server",
                "Tick=%lu; loaded=%u; pending=%u; ready=%u; gpu=%lu; uploads=%.1f/s; frame=%.1fms.",
                latest.tick, latest.world_stats.loaded, latest.world_stats.pending,
                latest.world_stats.ready, (unsigned long)resident_count,
                mesh_activity.status.uploads_per_second,
                frames ? (now - diagnostic_time) * 1000.0 / frames : 0.0);
            diagnostic_time = now; frames = 0;
        }
        if (maage_status_code(rendered) == MAAGE_STATUS_NOOP) {
            /* A visible Metal mailbox can be full for just one refresh.
             * Sleeping 100 ms here caused severe, avoidable frame hitches
             * under both unlimited FPS and high caps. Keep AppKit responsive
             * without busy-spinning while presentation catches up. */
            int wait_ms = state.visible && !state.mode_transitioning &&
                state.framebuffer_width > 0 && state.framebuffer_height > 0 ? 1 : 16;
            pacing.deadline = 0; pacing.fps = 0;
            result = maage_window_wait_events(system, wait_ms);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto window_failed;
        } else {
            operation = maage_client_pacing_wait(&pacing, system, window,
                maage_client_ui_max_fps(ui), now, &resize_pending);
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
    operation = maage_server_runtime_join(server, trace);
    if (maage_status_failed(operation)) goto done;
    operation = maage_server_runtime_destroy(server);
    if (maage_status_failed(operation)) goto done;
    server = NULL; server_started = 0;
    operation = maage_local_session_close(session);
    if (maage_status_failed(operation)) goto done;
    operation = maage_local_session_destroy(session);
    if (maage_status_failed(operation)) goto done;
    session = NULL;
    operation = maage_client_mesh_cache_clear(&cache);
    if (maage_status_failed(operation)) goto done;
    maage_client_ui_get_options(ui, &video_options);
    rendered = maage_renderer_set_scene(renderer, 1, NULL, NULL);
    if (maage_status_failed(rendered)) goto done;
    rendered = maage_renderer_set_foreground(renderer, NULL, NULL, 0, 0);
    if (maage_status_failed(rendered)) goto done;
    first_person.initialized = 0;
    operation = maage_client_item_entities_clear(item_entities);
    if (maage_status_failed(operation)) goto done;
    goto choose_world;

clock_failed:
    outcome = maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED,
        MAAGE_CONTEXT_CORE, MAAGE_OPERATION_QUERY, 0);
    failure_message = "Monotonic presentation clock unavailable.";
    goto done;
window_failed:
    outcome = MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    failure_message = "Native window/input cannot continue.";
done:
    if (options_ready && ui) {
        operation = maage_client_options_store(instance, ui, &saved_video_options, 1);
        if (maage_status_failed(operation))
            maage_log_status(MAAGE_LOG_WARNING, "maage.client.options",
                operation, "Unable to persist config/options.ini during shutdown.");
    }
    if (ok) outcome = maage_status_make(maage_status_is_success(resource_notice) ?
        MAAGE_STATUS_SUCCESS : MAAGE_STATUS_PARTIAL,
        MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP, 0);
    else if (!maage_status_failed(outcome)) {
        outcome = maage_status_failed(rendered) ? rendered :
            (maage_status_failed(result) ? result : operation);
        if (!maage_status_failed(outcome))
            outcome = maage_status_make(MAAGE_STATUS_FATAL,
                MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP, 0);
    }
    if (!ok) {
        MAAGE_STATUS_PROPAGATE(trace, outcome, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP);
        maage_log_trace_status(MAAGE_LOG_FATAL, "maage.client", outcome, trace,
            "%s", failure_message);
    }
    maage_status_trace_init(&cleanup_trace);
    if (window) maage_client_cleanup_result(maage_window_capture_mouse(window, 0),
        &outcome, &cleanup_trace, trace);
    /* Stop command production first. The Server controls all authoritative
     * World flush/persistence; no Client World store/save API is ever invoked. */
    if (server) maage_server_runtime_request_stop(server);
    if (ui) maage_client_ui_set_click_callback(ui, NULL, NULL);
    maage_client_cleanup_result(maage_client_audio_service_destroy(audio, &cleanup_trace),
        &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_mesh_cache_clear(&cache),
        &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_environment_destroy(environment_view),
        &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_item_entities_destroy(item_entities),
        &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_render_mesh_destroy(interaction.mesh),
        &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_client_first_person_destroy(&first_person, renderer),
        &outcome, &cleanup_trace, trace);
    operation = maage_client_frontend_destroy(frontend);
    if (maage_status_failed(operation)) {
        maage_client_storage_retain();
        return MAAGE_STATUS_PROPAGATE(trace, operation, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP);
    }
    frontend = NULL;
    maage_client_cleanup_result(maage_client_ui_destroy(ui),
        &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_render_texture_destroy(atlas),
        &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_renderer_destroy(renderer, &cleanup_trace),
        &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_window_destroy(window),
        &outcome, &cleanup_trace, trace);
    maage_client_cleanup_result(maage_window_system_destroy(system),
        &outcome, &cleanup_trace, trace);
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
                maage_log_trace_status(MAAGE_LOG_ERROR, "maage.client.server",
                    operation, &cleanup_trace,
                    "Authoritative Server reported a shutdown or persistence failure.");
            }
        }
        /* Destroy retries an actual failed thread join and refuses to release
         * a World whose workers still borrow content/storage. */
        operation = maage_server_runtime_destroy(server);
        if (maage_status_failed(operation)) {
            maage_client_storage_retain();
            maage_log_trace_status(MAAGE_LOG_FATAL, "maage.client.server",
                operation, &cleanup_trace,
                "Integrated Server did not fully release its World; retaining borrowed content and instance storage.");
            return MAAGE_STATUS_PROPAGATE(trace, operation,
                MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP);
        }
    }
    if (session) {
        maage_client_cleanup_result(maage_local_session_close(session),
            &outcome, &cleanup_trace, trace);
        maage_client_cleanup_result(maage_local_session_destroy(session),
            &outcome, &cleanup_trace, trace);
    }
    maage_client_cleanup_result(maage_content_destroy(content),
        &outcome, &cleanup_trace, trace);
    if (biomes) {
        maage_client_cleanup_result(maage_biome_registry_destroy(biomes),
            &outcome, &cleanup_trace, trace);
        free(biomes);
    }
    maage_client_cleanup_result(maage_client_resources_destroy(&resources),
        &outcome, &cleanup_trace, trace);
    (void)valid_snapshot;
    return MAAGE_STATUS_PROPAGATE(trace, outcome,
        MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_STOP);
}
