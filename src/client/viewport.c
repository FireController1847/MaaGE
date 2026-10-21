#include "maage/window.h"
#include "maage/input.h"
#include "maage/render.h"
#include "maage/storage.h"
#include "maage/minecraft_assets.h"
#include "maage/time.h"
#include "maage/player.h"
#include "maage/gameplay.h"
#include "maage/world_store.h"
#include "maage/log.h"
#include "resources.h"
#include "ui/ui.h"
#include "debug.h"
#include "interaction.h"
#include "environment.h"
#include "storage_context.h"
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
    double sample_start, previous_upload_ms, expected_upload_ms;
    unsigned long sample_uploads, sample_vertices;
} maage_client_mesh_activity;

/* Textual options are resolved before any world/runtime work begins. */
static int maage_client_world_options_parse(int argc, char *const argv[], maage_client_world_options *options)
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
            if (options->id || options->create) return 0;
            options->id = argument + 8;
        } else if (!strncmp(argument, "--create-world=", 15)) {
            if (options->id || options->create) return 0;
            options->id = argument + 15; options->create = 1;
        } else if (!strcmp(argument, "--new-world")) {
            if (options->id || options->create) return 0;
            options->create = 1;
        } else if (!strncmp(argument, "--seed=", 7)) {
            if (!maage_world_seed_parse(argument + 7, &options->seed)) return 0;
            options->seeded = 1;
        } else if (!strncmp(argument, "--world-name=", 13)) options->name = argument + 13;
        else if (!strncmp(argument, "--render-distance=", 18)) {
            errno = 0; value = strtol(argument + 18, &end, 10);
            if (errno || !argument[18] || *end || value < 1 || value > INT_MAX) return 0;
            options->radius = (int)value;
        } else if (!strncmp(argument, "--mesh-upload-budget-ms=", 24)) {
            errno = 0; options->mesh_budget_ms = strtod(argument + 24, &end);
            if (errno || !argument[24] || *end ||
                !(options->mesh_budget_ms >= 0.0 && options->mesh_budget_ms <= DBL_MAX)) return 0;
        } else if (!strncmp(argument, "--mesh-frame-target-ms=", 23)) {
            errno = 0; options->frame_target_ms = strtod(argument + 23, &end);
            if (errno || !argument[23] || *end ||
                !(options->frame_target_ms > 0.0 && options->frame_target_ms <= DBL_MAX)) return 0;
        } else if (!strncmp(argument, "--generator-version=", 20)) {
            maage_generation_policy policy;
            if (argument[20] < '0' || argument[20] > '9') return 0;
            errno = 0; options->generator_version = strtoul(argument + 20, &end, 10);
            if (errno || *end || !maage_generation_policy_default(options->generator_version, &policy)) return 0;
            generator_selected = 1;
        } else if (!strncmp(argument, "--world-workers=", 16)) {
            if (argument[16] < '0' || argument[16] > '9') return 0;
            errno = 0; workers = strtoul(argument + 16, &end, 10);
            if (errno || *end || workers > UINT_MAX) return 0;
            options->workers = (unsigned int)workers;
        } else if (!strncmp(argument, "--gamemode=", 11)) {
            if (!strcmp(argument + 11, "creative")) options->mode = MAAGE_GAME_MODE_CREATIVE;
            else if (!strcmp(argument + 11, "survival")) options->mode = MAAGE_GAME_MODE_SURVIVAL;
            else return 0;
        } else if (!strncmp(argument, "--interaction-range=", 20)) {
            errno = 0; options->reach = strtod(argument + 20, &end);
            if (errno || !argument[20] || *end || !(options->reach > 0 && options->reach <= 64)) return 0;
        } else if (!strcmp(argument, "--world") || !strcmp(argument, "--create-world") ||
            !strcmp(argument, "--seed") || !strcmp(argument, "--world-name") || !strcmp(argument, "--render-distance") ||
            !strcmp(argument, "--world-workers") || !strcmp(argument, "--generator-version") || !strcmp(argument, "--mesh-upload-budget-ms") ||
            !strcmp(argument, "--mesh-frame-target-ms") || !strcmp(argument, "--gamemode") ||
            !strcmp(argument, "--interaction-range")) return 0;
    }
    if (options->seeded && options->id && !options->create) return 0;
    if (options->name && !options->create && !options->seeded) return 0;
    if (generator_selected && !options->create && !options->seeded) return 0;
    return 1;
}

/* Mesh resources remain Client-owned. Capacity grows with actual resident
 * meshes, independently of the World's scheduling/cache implementation. */
static int maage_client_mesh_cache_grow(maage_client_mesh_cache *cache)
{
    maage_client_chunk_mesh *slots;
    const maage_render_mesh **draws;
    size_t previous = cache->capacity, grown;
    if (previous > ((size_t)-1 - 1) / 2) grown = previous + 1;
    else grown = previous ? previous * 2 : 32;
    if (grown <= previous || grown > (size_t)-1 / sizeof(*slots) ||
        grown >= (size_t)-1 / sizeof(*draws)) return 0;
    slots = (maage_client_chunk_mesh *)realloc(cache->slots, grown * sizeof(*slots));
    if (!slots) return 0;
    cache->slots = slots;
    draws = (const maage_render_mesh **)realloc(cache->draws, (grown + 1) * sizeof(*draws));
    if (!draws) return 0;
    cache->draws = draws;
    memset(cache->slots + previous, 0, (grown - previous) * sizeof(*slots));
    cache->capacity = grown;
    return 1;
}
static void maage_client_mesh_cache_clear(maage_client_mesh_cache *cache)
{
    size_t i;
    for (i = 0; i < cache->capacity; ++i)
        maage_render_mesh_destroy(cache->slots[i].mesh);
    free(cache->slots); free(cache->draws);
    memset(cache, 0, sizeof(*cache));
}
static double maage_client_mesh_budget(const maage_client_world_options *options,
    const maage_client_debug *debug, const maage_client_mesh_activity *activity)
{
    double other_work_ms, headroom;
    if (options->mesh_budget_ms > 0.0) return options->mesh_budget_ms;
    other_work_ms = debug->timing_ready ? debug->frame_ms - activity->previous_upload_ms : 0.0;
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
static maage_render_result maage_client_stream_meshes(maage_world *world,
    maage_renderer *renderer, maage_render_texture *atlas, maage_client_mesh_cache *cache,
    maage_client_mesh_activity *activity, const maage_client_world_options *options,
    const maage_client_debug *debug, int center_x, int center_z)
{
    maage_world_mesh_update update;
    const maage_mesh_data *geometry;
    maage_render_mesh *replacement;
    maage_render_result result;
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
    if (begin < 0.0) return MAAGE_RENDER_PLATFORM_ERROR;
    for (;;) {
        current = maage_time_monotonic_seconds();
        if (current < 0.0) return MAAGE_RENDER_PLATFORM_ERROR;
        if (processed && ((current - begin) * 1000.0 >= budget_ms ||
            (current - begin) * 1000.0 + activity->expected_upload_ms > budget_ms)) break;
        if (!maage_world_take_mesh(world, &update)) break;
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
                if (!maage_client_mesh_cache_grow(cache)) {
                    maage_world_mesh_destroy(update.mesh);
                    return MAAGE_RENDER_OUT_OF_MEMORY;
                }
            }
        }
        if (!cache->slots[selected].used || update.revision != cache->slots[selected].revision) {
            geometry = maage_world_mesh_data(update.mesh);
            replacement = NULL; result = MAAGE_RENDER_OK;
            if (geometry->index_count) {
                upload_begin = maage_time_monotonic_seconds();
                if (upload_begin < 0.0) { maage_world_mesh_destroy(update.mesh); return MAAGE_RENDER_PLATFORM_ERROR; }
                result = maage_renderer_upload_geometry(renderer, geometry, atlas, &replacement);
                current = maage_time_monotonic_seconds();
                if (current < 0.0) {
                    maage_render_mesh_destroy(replacement);
                    maage_world_mesh_destroy(update.mesh); return MAAGE_RENDER_PLATFORM_ERROR;
                }
                upload_ms = (current - upload_begin) * 1000.0;
                activity->status.frame_upload_ms += upload_ms;
                if (result == MAAGE_RENDER_OK) {
                    activity->expected_upload_ms = activity->status.gpu_uploads ?
                        activity->expected_upload_ms * 0.75 + upload_ms * 0.25 : upload_ms;
                    ++activity->status.gpu_uploads; ++activity->sample_uploads;
                    activity->status.vertices_uploaded += (unsigned long)geometry->vertex_count;
                    activity->sample_vertices += (unsigned long)geometry->vertex_count;
                    ++activity->status.uploaded_this_frame;
                }
            }
            if (result == MAAGE_RENDER_OK) {
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
                return result;
            }
        }
        maage_world_mesh_destroy(update.mesh);
    }
    current = maage_time_monotonic_seconds();
    if (current < 0.0) return MAAGE_RENDER_PLATFORM_ERROR;
    activity->previous_upload_ms = activity->status.frame_upload_ms;
    activity->status.stream_ms = (current - begin) * 1000.0;
    maage_client_mesh_rates(activity, current);
    return MAAGE_RENDER_OK;
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

int maage_client_run(int argc, char *const argv[])
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
    maage_world_store *store = NULL;
    maage_instance *instance = maage_client_storage_instance();
    maage_client_mesh_cache cache;
    maage_client_mesh_activity mesh_activity;
    maage_client_world_options options;
    maage_client_resources resources;
    maage_biome_registry biomes;
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
    maage_window_result result = MAAGE_WINDOW_OK;
    maage_render_result rendered = MAAGE_RENDER_OK;
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
    int attack_pending = 0, use_pending = 0;
    memset(&resources, 0, sizeof(resources)); memset(&cache, 0, sizeof(cache)); memset(&mesh_activity, 0, sizeof(mesh_activity));
    memset(&debug, 0, sizeof(debug));
    memset(&interaction, 0, sizeof(interaction)); memset(&intent, 0, sizeof(intent));
    maage_render_desc_init(&render_config);
    rendered = maage_render_desc_parse_args(argc, argv, &render_config);
    if (rendered != MAAGE_RENDER_OK) goto done;
    if (!maage_client_world_options_parse(argc, argv, &options)) {
        maage_log_error("maage.client.world", "Invalid world options. Use --new-world or --create-world=<id>, optional --seed=<signed64> / --world-name=<name>, --generator-version=1|2|3 for creation, --world=<id> to load, --render-distance=<positive int>, --world-workers=<nonnegative count> (0 automatic), --mesh-upload-budget-ms=<milliseconds> (0 auto), and --mesh-frame-target-ms=<milliseconds>."); goto done;
    }
    if (!instance) { maage_log_error("maage.client.instance", "No validated instance was selected at startup."); goto done; }
    strcpy(directory, maage_client_storage_data_root());
    maage_log_info("maage.client.instance", "Instance: %s (%s).", maage_instance_get_info(instance)->name,
        maage_instance_get_info(instance)->id);
    if (maage_world_store_legacy_pending(directory)) {
        if (maage_client_storage_migrate_requested() || maage_instance_only_configured(instance)) {
            if (!maage_world_store_migrate_legacy(directory, maage_instance_directory(instance), error)) {
                maage_log_error("maage.client.instance", "%s", error); goto done;
            }
            maage_log_info("maage.client.instance", "Legacy world migration completed for the selected instance.");
        } else maage_log_warn("maage.client.instance", "Legacy global worlds are preserved pending an explicit destination. Close Clients, then launch --instance=<UUID> --migrate-legacy-worlds to choose this instance. Its private worlds remain independent.");
    }
    maage_biome_registry_init(&biomes);
    maage_log_info("maage.client.resources", "Resource directory: %s/minecraft/assets/reconst", directory);
    if (!maage_client_resources_load(directory, &resources, &biomes) || !maage_biome_registry_finalize(&biomes)) goto done;
    registry = maage_client_register_blocks(&resources, &biomes);
    if (!registry) { maage_log_error("maage.client.world", "Block registration failed."); goto done; }
    items = maage_item_registry_create_builtin(registry);
    if (!items) { maage_log_error("maage.client.gameplay", "Item registration failed."); goto done; }
    maage_gameplay_init(&game, items);
    result = maage_window_system_create(MAAGE_WINDOW_AUTO, &system);
    if (result != MAAGE_WINDOW_OK) goto window_failed;
    maage_window_config_init(&window_config); window_config.title = "MaaGE Client"; window_config.visible = 0;
    result = maage_window_create(system, &window_config, &window);
    if (result != MAAGE_WINDOW_OK) goto window_failed;
    render_config.window = window; render_config.required_features |= MAAGE_RENDER_CAP_TEXTURED_MESH | MAAGE_RENDER_CAP_UI | MAAGE_RENDER_CAP_SCENE;
    rendered = maage_renderer_create(&render_config, &renderer);
    if (rendered != MAAGE_RENDER_OK) goto done;
    rendered = maage_renderer_create_texture(renderer, &resources.atlas, &atlas);
    if (rendered != MAAGE_RENDER_OK) goto done;
    maage_client_interaction_init(&interaction, &resources);
    ui = maage_client_ui_create(renderer, directory, &resources.atlas);
    maage_client_resources_destroy(&resources);
    if (!ui || !maage_client_ui_finalize(ui)) { maage_log_error("maage.client.ui", "Cannot initialize Client GUI resources or screen registry."); goto done; }
    environment_view = maage_client_environment_create(renderer, directory);
    if (!environment_view) { maage_log_error("maage.client.environment", "Cannot initialize environmental presentation resources."); goto done; }
    maage_log_info("maage.client", "Renderer initialized: %s.", maage_render_backend_name(maage_renderer_get_backend(renderer)));
    maage_generation_policy_default(options.generator_version, &generation_policy);
    if (!maage_world_store_open_configured(maage_instance_directory(instance), options.id, options.create, options.name,
        options.seeded ? &options.seed : NULL, options.id && !options.create ? NULL : &generation_policy,
        items, &store, &record, error)) {
        maage_log_error("maage.client.world", "%s", error); goto done;
    }
    memset(&world_config, 0, sizeof(world_config));
    world_config.registry = registry; world_config.biomes = &biomes; world_config.seed = record.seed;
    world_config.worker_count = options.workers;
    world_config.generation_policy = record.generation_policy;
    world_config.initial_environment = &record.environment;
    world_config.store = store; world_config.render_distance = options.radius; world_config.new_world = record.new_world;
    memcpy(world_config.position, record.position, sizeof(world_config.position));
    world = maage_world_create(&world_config);
    if (!world) { maage_log_error("maage.client.world", "World initialization failed (worker pool, generator policy, or memory)."); goto done; }
    maage_world_get_stats(world, &stats);
    maage_player_spawn(&player, world); previous = player; player_ready = 1;
    if (!maage_gameplay_restore(&game, items, &record.gameplay)) {
        maage_log_error("maage.client.gameplay", "Saved player gameplay data does not match registered items."); player_ready = 0; goto done;
    }
    if (record.new_world) maage_gameplay_init(&game, items);
    if (options.mode >= 0) {
        maage_gameplay_set_mode(&game, (unsigned int)options.mode);
        if (record.new_world && options.mode == MAAGE_GAME_MODE_SURVIVAL)
            memset(&game.inventory, 0, sizeof(game.inventory));
    }
    game.reach = options.reach;
    maage_client_ui_set_gameplay(ui, items, &game, registry);
    yaw = record.yaw; pitch = record.pitch;
    maage_world_seed_format(record.seed, seed_text, sizeof(seed_text));
    maage_log_info("maage.client.world", "%s world %s; seed=%s; generator=%lu; render distance=%d; workers=%u. Loading nearby columns asynchronously.",
        record.new_world ? "Created" : "Restored", record.id, seed_text,
        record.generation_policy.generator_version, options.radius, stats.worker_count);
    rendered = maage_renderer_set_clear_color(renderer, 0.53f, 0.76f, 1.0f, 1.0f);
    if (rendered != MAAGE_RENDER_OK) goto done;
    rendered = maage_renderer_prepare_frame(renderer);
    if (rendered == MAAGE_RENDER_UNSUPPORTED) rendered = MAAGE_RENDER_OK;
    if (rendered != MAAGE_RENDER_OK && rendered != MAAGE_RENDER_SKIPPED) goto done;
    result = maage_window_set_visible(window, 1);
    if (result != MAAGE_WINDOW_OK) goto window_failed;
    maage_log_info("maage.client", "WASD walks; Space jumps (double tap for Creative flight), Shift descends; mouse buttons break/place; 1-9 or wheel selects; E opens inventory; Escape returns; F3 shows diagnostics. Options selects game mode. Unavailable columns block interaction and movement.");
    last = maage_time_monotonic_seconds(); diagnostic_time = last;
    mesh_activity.sample_start = last;
    if (last < 0) goto clock_failed;
    for (;;) {
        result = maage_window_poll_events(system, 0);
        if (result != MAAGE_WINDOW_OK) goto window_failed;
        result = maage_window_get_state(window, &state);
        if (result != MAAGE_WINDOW_OK) goto window_failed;
        if (state.close_requested) { ok = 1; break; }
        result = maage_window_get_input(window, &input);
        if (result != MAAGE_WINDOW_OK) goto window_failed;
        was_modal = maage_client_ui_modal(ui);
        memset(&ui_input, 0, sizeof(ui_input));
        if (input.focused) {
            ui_input.escape = input.pressed[MAAGE_INPUT_KEY_ESCAPE];
            ui_input.f3 = input.pressed[MAAGE_INPUT_KEY_F3];
            ui_input.accept = input.pressed[MAAGE_INPUT_KEY_ENTER];
            ui_input.previous = input.pressed[MAAGE_INPUT_KEY_UP];
            ui_input.next = input.pressed[MAAGE_INPUT_KEY_TAB] || input.pressed[MAAGE_INPUT_KEY_DOWN];
            ui_input.primary_pressed = input.primary_pressed;
            ui_input.secondary_pressed = input.secondary_pressed;
            ui_input.inventory = input.pressed[MAAGE_INPUT_KEY_E];
        }
        ui_input.mouse_x = (float)input.mouse_x; ui_input.mouse_y = (float)input.mouse_y;
        maage_client_ui_set_safe_area(ui, &state.safe_area);
        ui_actions = maage_client_ui_update(ui, &ui_input, state.framebuffer_width, state.framebuffer_height, state.width, state.height);
        if ((was_focused && !input.focused) || (was_active && !input.captured)) {
            if (!maage_client_ui_modal(ui)) maage_client_ui_open(ui, MAAGE_CLIENT_UI_PAUSE);
        }
        was_focused = input.focused;
        if (ui_actions & MAAGE_CLIENT_UI_EXIT) { ok = 1; break; }
        if (ui_actions & MAAGE_CLIENT_UI_RESPAWN) {
            maage_gameplay_respawn(&game, &player, world); previous = player;
            maage_client_ui_back(ui);
        }
        if (maage_client_ui_modal(ui)) {
            initial_capture = 0;
            if (input.captured) result = maage_window_capture_mouse(window, 0);
            input.captured = 0;
        } else if (input.focused && !input.captured && (initial_capture || was_modal || input.primary_pressed)) {
            result = maage_window_capture_mouse(window, 1); initial_capture = 0;
            if (result == MAAGE_WINDOW_OK) {
                input.captured = 1; input.mouse_delta_x = input.mouse_delta_y = 0;
            }
        }
        if (result != MAAGE_WINDOW_OK) goto window_failed;
        now = maage_time_monotonic_seconds();
        if (now < 0) goto clock_failed;
        raw_elapsed = now - last; last = now; elapsed = raw_elapsed;
        if (elapsed < 0) elapsed = 0;
        if (elapsed > MAAGE_CLIENT_MAX_FRAME_SECONDS) elapsed = MAAGE_CLIENT_MAX_FRAME_SECONDS;
        cx = maage_world_chunk_coordinate((int)floor(player.position[0]));
        cz = maage_world_chunk_coordinate((int)floor(player.position[2]));
        /* One owner commits worker results before the ordered fixed-step player
         * phase. Collision observes stable published columns without waiting. */
        maage_world_stream(world, player.position[0], player.position[2], yaw);
        if (maage_world_failed(world)) { maage_log_error("maage.client.world", "Streaming stopped after a world error; preserving existing save files."); goto done; }
        if (maage_client_ui_debug_visible(ui) || now - diagnostic_time >= 5.0)
            maage_world_get_stats(world, &stats);
        rendered = maage_client_stream_meshes(world, renderer, atlas, &cache, &mesh_activity, &options, &debug, cx, cz);
        if (rendered != MAAGE_RENDER_OK) goto done;
        active = !maage_client_ui_modal(ui) && input.focused && input.captured && state.visible && state.framebuffer_width > 0 && state.framebuffer_height > 0;
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
                previous = player; maage_gameplay_tick(&game, &player, world, items, &intent);
                maage_world_tick(world);
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
        maage_gameplay_target(&game, &player, world, yaw, pitch);
        rendered = maage_client_interaction_update(&interaction, renderer, atlas, world, &game);
        if (rendered != MAAGE_RENDER_OK) goto done;
        for (axis = 0; axis < 3; ++axis) eye[axis] = previous.position[axis] + (player.position[axis] - previous.position[axis]) * alpha;
        eye[1] += MAAGE_PLAYER_EYE_HEIGHT;
        maage_world_environment(world, &environment_state);
        rendered = maage_client_environment_update(environment_view, renderer, &environment_state,
            world, &player, yaw, pitch, maage_client_ui_width(ui), maage_client_ui_height(ui), MAAGE_CLIENT_FOV_RADIANS);
        if (rendered != MAAGE_RENDER_OK) goto done;
        if (state.resized) maage_renderer_resize(renderer);
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
            if (!cache.draws && !maage_client_mesh_cache_grow(&cache)) {
                rendered = MAAGE_RENDER_OUT_OF_MEMORY; goto done;
            }
            cache.draws[draw_count++] = interaction.mesh;
        }
        debug_text = maage_client_ui_debug_visible(ui) ? maage_client_debug_collect(&debug, now, world, &record,
            &stats, &player, yaw, pitch, options.radius, !active, renderer, &mesh_stats, &game) : NULL;
        if (debug_text) maage_client_debug_mesh_diagnostics(&debug.text, &mesh_activity.status);
        ui_draw = maage_client_ui_draw(ui, debug_text);
        if (!ui_draw) { maage_log_error("maage.client.ui", "Client UI batch could not be constructed."); goto done; }
        if (state.framebuffer_width > 0 && state.framebuffer_height > 0) {
            maage_client_camera(matrix, eye, yaw, pitch, (double)state.framebuffer_width / state.framebuffer_height, options.radius);
            rendered = maage_renderer_draw_frame(renderer, cache.draws, draw_count, matrix, ui_draw);
            if (rendered != MAAGE_RENDER_OK && rendered != MAAGE_RENDER_SKIPPED) goto done;
        } else rendered = MAAGE_RENDER_SKIPPED;
        frame_end = maage_time_monotonic_seconds();
        if (frame_end < 0) goto clock_failed;
        maage_client_debug_frame(&debug, frame_end, raw_elapsed > 0 ? raw_elapsed : 0,
            rendered == MAAGE_RENDER_OK, tick_count, tick_seconds);
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
        if (rendered == MAAGE_RENDER_SKIPPED) {
            result = maage_window_wait_events(system, 100);
            if (result != MAAGE_WINDOW_OK) goto window_failed;
        }
    }
    goto done;
clock_failed:
    maage_log_error("maage.client", "Monotonic clock unavailable."); goto done;
window_failed:
    maage_log_error("maage.client", "Native window/input failed: %s.", maage_window_result_string(result));
done:
    if (rendered != MAAGE_RENDER_OK && rendered != MAAGE_RENDER_SKIPPED)
        maage_log_error("maage.client", "Renderer failed: %s. Gameplay requires textured mesh support in the selected family.", maage_render_result_string(rendered));
    if (window) maage_window_capture_mouse(window, 0);
    /* Waiting and final player metadata I/O occur only after rendering stops. */
    if (world && !maage_world_flush(world)) {
        maage_log_error("maage.client.world", "World shutdown reported a generation, loading or save failure; existing player metadata is preserved.");
        ok = 0; player_ready = 0;
    }
    if (store && player_ready) {
        maage_world_environment(world, &environment_state);
        if (!maage_world_store_save_environment_gameplay(store, player.position, yaw, pitch, &game, items, &environment_state, error)) {
            maage_log_error("maage.client.world", "%s", error); ok = 0;
        }
    }
    maage_client_mesh_cache_clear(&cache);
    maage_client_environment_destroy(environment_view);
    maage_render_mesh_destroy(interaction.mesh);
    maage_client_ui_destroy(ui); maage_render_texture_destroy(atlas); maage_renderer_destroy(renderer);
    if (window) maage_window_destroy(window);
    if (system) maage_window_system_destroy(system);
    maage_world_destroy(world); maage_world_store_close(store); maage_block_registry_destroy(registry);
    maage_item_registry_destroy(items);
    maage_client_resources_destroy(&resources);
    return ok ? 0 : 1;
}
