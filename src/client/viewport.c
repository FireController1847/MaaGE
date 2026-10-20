#include "maage/window.h"
#include "maage/input.h"
#include "maage/render.h"
#include "maage/storage.h"
#include "maage/minecraft_assets.h"
#include "maage/time.h"
#include "maage/player.h"
#include "maage/world_store.h"
#include "maage/log.h"
#include "resources.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* Phase 1 camera tuning, pending 1.7.10 feel/fidelity verification. */
#define MAAGE_CLIENT_MOUSE_RADIANS 0.0025
#define MAAGE_CLIENT_PITCH_LIMIT 1.553343034
#define MAAGE_CLIENT_FOV_RADIANS 1.221730476
#define MAAGE_CLIENT_MAX_FRAME_SECONDS 0.25

typedef struct maage_client_world_options {
    const char *id, *name;
    maage_world_seed seed;
    int create, seeded, radius;
} maage_client_world_options;
typedef struct maage_client_chunk_mesh {
    int x, z, used;
    unsigned long revision;
    maage_render_mesh *mesh;
} maage_client_chunk_mesh;

/* Textual options are resolved before any world/runtime work begins. */
static int maage_client_world_options_parse(int argc, char *const argv[], maage_client_world_options *options)
{
    int i;
    const char *argument;
    char *end;
    long distance;
    memset(options, 0, sizeof(*options)); options->radius = 3;
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
            errno = 0; distance = strtol(argument + 18, &end, 10);
            if (errno || !argument[18] || *end || distance < 1 || distance > MAAGE_WORLD_MAX_RADIUS) return 0;
            options->radius = (int)distance;
        } else if (!strcmp(argument, "--world") || !strcmp(argument, "--create-world") ||
            !strcmp(argument, "--seed") || !strcmp(argument, "--world-name") || !strcmp(argument, "--render-distance")) return 0;
    }
    if (options->seeded && options->id && !options->create) return 0;
    if (options->name && !options->create && !options->seeded) return 0;
    return 1;
}

/* Client owns GPU meshes only; no renderer retains pointers into World columns.
 * Upload at most one changed column per frame and retain unchanged resources. */
static maage_render_result maage_client_stream_meshes(maage_world *world,
    maage_renderer *renderer, maage_render_texture *atlas, maage_client_chunk_mesh *cache,
    int center_x, int center_z, int radius)
{
    maage_world_mesh_update update;
    const maage_mesh_data *geometry;
    maage_render_mesh *replacement = NULL;
    maage_render_result result = MAAGE_RENDER_OK;
    int i, selected = -1;
    for (i = 0; i < MAAGE_WORLD_MAX_CHUNKS; ++i) {
        if (cache[i].used && (abs(cache[i].x - center_x) > radius || abs(cache[i].z - center_z) > radius ||
            !maage_world_chunk_loaded(world, cache[i].x, cache[i].z))) {
            maage_render_mesh_destroy(cache[i].mesh); memset(&cache[i], 0, sizeof(cache[i]));
        }
    }
    if (!maage_world_take_mesh(world, &update)) return MAAGE_RENDER_OK;
    for (i = 0; i < MAAGE_WORLD_MAX_CHUNKS; ++i) {
        if (cache[i].used && cache[i].x == update.x && cache[i].z == update.z) { selected = i; break; }
        if (!cache[i].used && selected < 0) selected = i;
    }
    if (selected < 0) result = MAAGE_RENDER_OUT_OF_MEMORY;
    else if (!cache[selected].used || update.revision > cache[selected].revision) {
        geometry = maage_world_mesh_data(update.mesh);
        if (geometry->index_count) result = maage_renderer_upload_geometry(renderer, geometry, atlas, &replacement);
        if (result == MAAGE_RENDER_OK) {
            maage_render_mesh_destroy(cache[selected].mesh);
            cache[selected].mesh = replacement; cache[selected].used = 1;
            cache[selected].x = update.x; cache[selected].z = update.z; cache[selected].revision = update.revision;
        }
    }
    maage_world_mesh_destroy(update.mesh);
    return result;
}

static int maage_client_data_directory(int argc, char *const argv[], char path[MAAGE_PATH_CAPACITY])
{
    int i;
    const char *selected = NULL;
    char probe[MAAGE_PATH_CAPACITY];
    for (i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "--data-directory=", 17)) selected = argv[i] + 17;
        else if (!strcmp(argv[i], "--data-directory")) return 0;
    }
    if (selected) {
        if (!*selected || strlen(selected) >= MAAGE_PATH_CAPACITY) return 0;
        strcpy(path, selected);
    } else if (!maage_storage_data_path(path, MAAGE_PATH_CAPACITY)) return 0;
    return maage_minecraft_asset_path(path, "minecraft", probe, sizeof(probe));
}

static void maage_client_camera(float output[16], const double eye[3], double yaw, double pitch, double aspect)
{
    double sy = sin(yaw), cy = cos(yaw), sp = sin(pitch), cp = cos(pitch);
    double forward[3], right[3], up[3], focal;
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
    /* Right-handed perspective, near=.05, far=512, clip depth [0,1]. */
    projection[10] = (float)(512.0 / (0.05 - 512.0)); projection[11] = -1.0f;
    projection[14] = (float)(0.05 * 512.0 / (0.05 - 512.0));
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
    maage_block_registry *registry = NULL;
    maage_world *world = NULL;
    maage_world_store *store = NULL;
    maage_client_chunk_mesh cache[MAAGE_WORLD_MAX_CHUNKS];
    const maage_render_mesh *draws[MAAGE_WORLD_MAX_CHUNKS];
    maage_client_world_options options;
    maage_client_resources resources;
    maage_biome_registry biomes;
    maage_world_config world_config;
    maage_world_record record;
    maage_world_stats stats;
    maage_window_config window_config;
    maage_window_state state;
    maage_input_snapshot input;
    maage_player player, previous;
    maage_player_input movement;
    maage_render_desc render_config;
    maage_window_result result = MAAGE_WINDOW_OK;
    maage_render_result rendered = MAAGE_RENDER_OK;
    char directory[MAAGE_PATH_CAPACITY], error[256], seed_text[32];
    double last, now, elapsed, accumulator = 0, alpha, eye[3], yaw = 0, pitch = 0, diagnostic_time;
    float matrix[16];
    size_t draw_count;
    unsigned long frames = 0;
    int ok = 0, initial_capture = 1, active, was_active = 0, jump_pending = 0, axis;
    int player_ready = 0, cx, cz, i;
    memset(&resources, 0, sizeof(resources)); memset(cache, 0, sizeof(cache));
    maage_render_desc_init(&render_config);
    rendered = maage_render_desc_parse_args(argc, argv, &render_config);
    if (rendered != MAAGE_RENDER_OK) goto done;
    if (!maage_client_world_options_parse(argc, argv, &options)) {
        maage_log_error("maage.client.world", "Invalid world options. Use --new-world or --create-world=<id>, optional --seed=<signed64> / --world-name=<name>, --world=<id> to load, and --render-distance=1..8."); goto done;
    }
    if (!maage_client_data_directory(argc, argv, directory)) {
        maage_log_error("maage.client.resources", "Cannot resolve MaaGE data directory; --data-directory requires an absolute path."); goto done;
    }
    maage_biome_registry_init(&biomes);
    maage_log_info("maage.client.resources", "Resource directory: %s/minecraft/assets/reconst", directory);
    if (!maage_client_resources_load(directory, &resources, &biomes) || !maage_biome_registry_finalize(&biomes)) goto done;
    registry = maage_client_register_blocks(&resources, &biomes);
    if (!registry) { maage_log_error("maage.client.world", "Block registration failed."); goto done; }
    result = maage_window_system_create(MAAGE_WINDOW_AUTO, &system);
    if (result != MAAGE_WINDOW_OK) goto window_failed;
    maage_window_config_init(&window_config); window_config.title = "MaaGE Client"; window_config.visible = 0;
    result = maage_window_create(system, &window_config, &window);
    if (result != MAAGE_WINDOW_OK) goto window_failed;
    render_config.window = window; render_config.required_features |= MAAGE_RENDER_CAP_TEXTURED_MESH;
    rendered = maage_renderer_create(&render_config, &renderer);
    if (rendered != MAAGE_RENDER_OK) goto done;
    rendered = maage_renderer_create_texture(renderer, &resources.atlas, &atlas);
    if (rendered != MAAGE_RENDER_OK) goto done;
    maage_client_resources_destroy(&resources);
    maage_log_info("maage.client", "Renderer initialized: %s.", maage_render_backend_name(maage_renderer_get_backend(renderer)));
    if (!maage_world_store_open(directory, options.id, options.create, options.name,
        options.seeded ? &options.seed : NULL, &store, &record, error)) {
        maage_log_error("maage.client.world", "%s", error); goto done;
    }
    memset(&world_config, 0, sizeof(world_config));
    world_config.registry = registry; world_config.biomes = &biomes; world_config.seed = record.seed;
    world_config.store = store; world_config.render_distance = options.radius; world_config.new_world = record.new_world;
    memcpy(world_config.position, record.position, sizeof(world_config.position));
    world = maage_world_create(&world_config);
    if (!world) { maage_log_error("maage.client.world", "World worker initialization failed."); goto done; }
    maage_player_spawn(&player, world); previous = player; player_ready = 1;
    yaw = record.yaw; pitch = record.pitch;
    maage_world_seed_format(record.seed, seed_text, sizeof(seed_text));
    maage_log_info("maage.client.world", "%s world %s; seed=%s; generator=1; render distance=%d. Loading nearby columns asynchronously.",
        record.new_world ? "Created" : "Restored", record.id, seed_text, options.radius);
    rendered = maage_renderer_set_clear_color(renderer, 0.53f, 0.76f, 1.0f, 1.0f);
    if (rendered != MAAGE_RENDER_OK) goto done;
    rendered = maage_renderer_prepare_frame(renderer);
    if (rendered == MAAGE_RENDER_UNSUPPORTED) rendered = MAAGE_RENDER_OK;
    if (rendered != MAAGE_RENDER_OK && rendered != MAAGE_RENDER_SKIPPED) goto done;
    result = maage_window_set_visible(window, 1);
    if (result != MAAGE_WINDOW_OK) goto window_failed;
    maage_log_info("maage.client", "WASD to walk, Space to jump, Escape to pause/release mouse, click to resume. Unavailable columns block movement until loaded.");
    last = maage_time_monotonic_seconds(); diagnostic_time = last;
    if (last < 0) goto clock_failed;
    for (;;) {
        result = maage_window_poll_events(system, 0);
        if (result != MAAGE_WINDOW_OK) goto window_failed;
        result = maage_window_get_state(window, &state);
        if (result != MAAGE_WINDOW_OK) goto window_failed;
        if (state.close_requested) { ok = 1; break; }
        result = maage_window_get_input(window, &input);
        if (result != MAAGE_WINDOW_OK) goto window_failed;
        if (input.pressed[MAAGE_INPUT_KEY_ESCAPE]) {
            result = maage_window_capture_mouse(window, 0); initial_capture = 0; input.captured = 0;
        } else if (input.focused && !input.captured && (initial_capture || input.primary_pressed)) {
            result = maage_window_capture_mouse(window, 1); initial_capture = 0;
            if (result == MAAGE_WINDOW_OK) input.captured = 1;
        }
        if (result != MAAGE_WINDOW_OK) goto window_failed;
        now = maage_time_monotonic_seconds();
        if (now < 0) goto clock_failed;
        elapsed = now - last; last = now;
        if (elapsed < 0) elapsed = 0;
        if (elapsed > MAAGE_CLIENT_MAX_FRAME_SECONDS) elapsed = MAAGE_CLIENT_MAX_FRAME_SECONDS;
        cx = maage_world_chunk_coordinate((int)floor(player.position[0]));
        cz = maage_world_chunk_coordinate((int)floor(player.position[2]));
        maage_world_stream(world, player.position[0], player.position[2], yaw);
        maage_world_get_stats(world, &stats);
        if (stats.failed) { maage_log_error("maage.client.world", "Streaming stopped after a world error; preserving existing save files."); goto done; }
        rendered = maage_client_stream_meshes(world, renderer, atlas, cache, cx, cz, options.radius);
        if (rendered != MAAGE_RENDER_OK) goto done;
        active = input.focused && input.captured && state.visible && state.framebuffer_width > 0 && state.framebuffer_height > 0;
        if (active && !was_active) elapsed = 0;
        was_active = active;
        if (active) {
            yaw = fmod(yaw + input.mouse_delta_x * MAAGE_CLIENT_MOUSE_RADIANS, 6.283185307179586);
            pitch += input.mouse_delta_y * MAAGE_CLIENT_MOUSE_RADIANS;
            if (pitch > MAAGE_CLIENT_PITCH_LIMIT) pitch = MAAGE_CLIENT_PITCH_LIMIT;
            if (pitch < -MAAGE_CLIENT_PITCH_LIMIT) pitch = -MAAGE_CLIENT_PITCH_LIMIT;
            movement.forward = (double)(input.keys[MAAGE_INPUT_KEY_W] - input.keys[MAAGE_INPUT_KEY_S]);
            movement.strafe = (double)(input.keys[MAAGE_INPUT_KEY_D] - input.keys[MAAGE_INPUT_KEY_A]);
            if (input.pressed[MAAGE_INPUT_KEY_SPACE]) jump_pending = 1;
            movement.yaw = yaw; movement.jump = input.keys[MAAGE_INPUT_KEY_SPACE] || jump_pending;
            if (maage_world_chunk_loaded(world, cx, cz)) accumulator += elapsed;
            else { accumulator = 0; previous = player; }
            while (accumulator >= MAAGE_PLAYER_TICK_SECONDS) {
                previous = player; maage_player_tick(&player, world, &movement); accumulator -= MAAGE_PLAYER_TICK_SECONDS;
                jump_pending = 0; movement.jump = input.keys[MAAGE_INPUT_KEY_SPACE];
            }
            alpha = accumulator / MAAGE_PLAYER_TICK_SECONDS;
        } else { accumulator = 0; previous = player; alpha = 1; jump_pending = 0; }
        for (axis = 0; axis < 3; ++axis) eye[axis] = previous.position[axis] + (player.position[axis] - previous.position[axis]) * alpha;
        eye[1] += MAAGE_PLAYER_EYE_HEIGHT;
        if (state.resized) maage_renderer_resize(renderer);
        draw_count = 0;
        for (i = 0; i < MAAGE_WORLD_MAX_CHUNKS; ++i) if (cache[i].used && cache[i].mesh) draws[draw_count++] = cache[i].mesh;
        if (state.framebuffer_width > 0 && state.framebuffer_height > 0) {
            maage_client_camera(matrix, eye, yaw, pitch, (double)state.framebuffer_width / state.framebuffer_height);
            rendered = maage_renderer_draw_meshes(renderer, draws, draw_count, matrix);
            if (rendered != MAAGE_RENDER_OK && rendered != MAAGE_RENDER_SKIPPED) goto done;
        } else rendered = MAAGE_RENDER_SKIPPED;
        ++frames;
        if (now - diagnostic_time >= 5.0) {
            maage_log_debug("maage.client.world", "Loaded=%u; pending=%u; ready=%u; generated=%lu; disk=%lu; meshes=%lu; frame=%.1fms.",
                stats.loaded, stats.pending, stats.ready, stats.generated, stats.loaded_from_disk, stats.meshes,
                (now - diagnostic_time) * 1000.0 / frames);
            diagnostic_time = now; frames = 0;
        }
        result = maage_window_wait_events(system, active && rendered != MAAGE_RENDER_SKIPPED ? 1 : 100);
        if (result != MAAGE_WINDOW_OK) goto window_failed;
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
    if (world && !maage_world_flush(world)) { maage_log_error("maage.client.world", "World shutdown reported a generation, loading or save failure."); ok = 0; }
    if (store && player_ready && !maage_world_store_save_player(store, player.position, yaw, pitch, error)) {
        maage_log_error("maage.client.world", "%s", error); ok = 0;
    }
    for (i = 0; i < MAAGE_WORLD_MAX_CHUNKS; ++i) maage_render_mesh_destroy(cache[i].mesh);
    maage_render_texture_destroy(atlas); maage_renderer_destroy(renderer);
    if (window) maage_window_destroy(window);
    if (system) maage_window_system_destroy(system);
    maage_world_destroy(world); maage_world_store_close(store); maage_block_registry_destroy(registry);
    maage_client_resources_destroy(&resources);
    return ok ? 0 : 1;
}
