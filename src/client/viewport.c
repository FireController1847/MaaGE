#include "maage/window.h"
#include "maage/input.h"
#include "maage/render.h"
#include "maage/storage.h"
#include "maage/minecraft_assets.h"
#include "maage/time.h"
#include "maage/player.h"
#include "maage/log.h"
#include "resources.h"
#include <math.h>
#include <string.h>

/* Phase 1 camera tuning, pending 1.7.10 feel/fidelity verification. */
#define MAAGE_CLIENT_MOUSE_RADIANS 0.0025
#define MAAGE_CLIENT_PITCH_LIMIT 1.553343034
#define MAAGE_CLIENT_FOV_RADIANS 1.221730476
#define MAAGE_CLIENT_MAX_FRAME_SECONDS 0.25

static maage_block_registry *maage_client_register(const maage_client_resources *resources)
{
    static const char *const names[] = { "minecraft:air", "minecraft:stone", "minecraft:grass", "minecraft:dirt" };
    /* Temporary ambient/directional approximation, resolved before meshing. */
    static const float shade[MAAGE_FACE_COUNT] = { 0.6f, 0.6f, 0.5f, 1.0f, 0.8f, 0.8f };
    maage_block_registry *registry;
    maage_block_definition block;
    maage_block_state_definition state;
    int i, face, texture, channel;
    registry = maage_block_registry_create(16, 16);
    if (!registry) return NULL;
    for (i = 0; i < 4; ++i) {
        block.id = (maage_block_id)i;
        block.solid = block.opaque = i != MAAGE_BLOCK_AIR;
        if (maage_block_registry_register_block(registry, names[i], &block) != MAAGE_REGISTRY_OK) goto failed;
        memset(&state, 0, sizeof(state)); state.id = (maage_block_state_id)i; state.block_id = block.id;
        for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
            texture = MAAGE_CLIENT_TEXTURE_STONE;
            if (i == MAAGE_BLOCK_DIRT) texture = MAAGE_CLIENT_TEXTURE_DIRT;
            if (i == MAAGE_BLOCK_GRASS) {
                texture = MAAGE_CLIENT_TEXTURE_GRASS_SIDE;
                if (face == MAAGE_FACE_NEGATIVE_Y) texture = MAAGE_CLIENT_TEXTURE_DIRT;
                if (face == MAAGE_FACE_POSITIVE_Y) texture = MAAGE_CLIENT_TEXTURE_GRASS_TOP;
            }
            state.faces[face].texture_id = (maage_texture_id)texture;
            memcpy(state.faces[face].uv, resources->uv[texture], sizeof(state.faces[face].uv));
            for (channel = 0; channel < 4; ++channel)
                state.faces[face].tint[channel] = channel == 3 ? 1.0f : shade[face];
        }
        if (maage_block_registry_register_state(registry, &state) != MAAGE_REGISTRY_OK) goto failed;
    }
    if (maage_block_registry_finalize(registry) != MAAGE_REGISTRY_OK) goto failed;
    maage_log_info("maage.client.world", "Finalized %lu block types / %lu states; runtime uses direct numeric indexing.",
        (unsigned long)maage_block_registry_block_count(registry), (unsigned long)maage_block_registry_state_count(registry));
    return registry;
failed:
    maage_block_registry_destroy(registry); return NULL;
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
    /* Right-handed perspective, near=.05, far=160, clip depth [0,1]. */
    projection[10] = (float)(160.0 / (0.05 - 160.0)); projection[11] = -1.0f;
    projection[14] = (float)(0.05 * 160.0 / (0.05 - 160.0));
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
    maage_render_mesh *scene = NULL;
    maage_block_registry *registry = NULL;
    maage_world *world = NULL;
    maage_world_mesh *mesh = NULL;
    const maage_mesh_data *geometry;
    maage_client_resources resources;
    maage_window_config window_config;
    maage_window_state state;
    maage_input_snapshot input;
    maage_player player, previous;
    maage_player_input movement;
    maage_render_desc render_config;
    maage_window_result result = MAAGE_WINDOW_OK;
    maage_render_result rendered = MAAGE_RENDER_OK;
    char directory[MAAGE_PATH_CAPACITY];
    double last, now, elapsed, accumulator = 0, alpha, eye[3], yaw = 0, pitch = 0;
    float matrix[16];
    int ok = 0, initial_capture = 1, active, was_active = 0, jump_pending = 0, axis;
    memset(&resources, 0, sizeof(resources));
    maage_render_desc_init(&render_config);
    rendered = maage_render_desc_parse_args(argc, argv, &render_config);
    if (rendered != MAAGE_RENDER_OK) goto done;
    if (!maage_client_data_directory(argc, argv, directory)) {
        maage_log_error("maage.client.resources", "Cannot resolve MaaGE data directory; --data-directory requires an absolute path."); goto done;
    }
    maage_log_info("maage.client.resources", "Resource directory: %s/minecraft/assets/reconst", directory);
    if (!maage_client_resources_load(directory, &resources)) goto done;
    registry = maage_client_register(&resources);
    if (!registry) { maage_log_error("maage.client.world", "Block registration failed."); goto done; }
    result = maage_window_system_create(MAAGE_WINDOW_AUTO, &system);
    if (result != MAAGE_WINDOW_OK) goto window_failed;
    maage_window_config_init(&window_config); window_config.title = "MaaGE Client"; window_config.visible = 0;
    result = maage_window_create(system, &window_config, &window);
    if (result != MAAGE_WINDOW_OK) goto window_failed;
    render_config.window = window; render_config.required_features |= MAAGE_RENDER_CAP_TEXTURED_MESH;
    rendered = maage_renderer_create(&render_config, &renderer);
    if (rendered != MAAGE_RENDER_OK) goto done;
    maage_log_info("maage.client", "Renderer initialized: %s.", maage_render_backend_name(maage_renderer_get_backend(renderer)));
    world = maage_world_create_static(registry);
    if (!world) { maage_log_error("maage.client.world", "Static chunk world initialization failed."); goto done; }
    mesh = maage_world_mesh_create(world);
    if (!mesh) { maage_log_error("maage.client.world", "Visible-face mesh allocation failed."); goto done; }
    geometry = maage_world_mesh_data(mesh);
    maage_log_info("maage.client.world", "Static world: nine 16x16x16 sections; %lu visible faces / %lu vertices.",
        (unsigned long)(geometry->index_count / 6), (unsigned long)geometry->vertex_count);
    rendered = maage_renderer_upload_mesh(renderer, geometry, &resources.atlas, &scene);
    if (rendered != MAAGE_RENDER_OK) goto done;
    maage_world_mesh_destroy(mesh); mesh = NULL; maage_client_resources_destroy(&resources);
    maage_player_spawn(&player, world); previous = player;
    rendered = maage_renderer_set_clear_color(renderer, 0.53f, 0.76f, 1.0f, 1.0f);
    if (rendered != MAAGE_RENDER_OK) goto done;
    rendered = maage_renderer_prepare_frame(renderer);
    if (rendered == MAAGE_RENDER_UNSUPPORTED) rendered = MAAGE_RENDER_OK;
    if (rendered != MAAGE_RENDER_OK && rendered != MAAGE_RENDER_SKIPPED) goto done;
    result = maage_window_set_visible(window, 1);
    if (result != MAAGE_WINDOW_OK) goto window_failed;
    maage_log_info("maage.client", "Phase 1 initialized: WASD to walk, Space to jump, Escape to pause/release mouse, click to resume.");
    last = maage_time_monotonic_seconds();
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
            movement.yaw = yaw; movement.jump = input.keys[MAAGE_INPUT_KEY_SPACE] || jump_pending; accumulator += elapsed;
            while (accumulator >= MAAGE_PLAYER_TICK_SECONDS) {
                previous = player; maage_player_tick(&player, world, &movement); accumulator -= MAAGE_PLAYER_TICK_SECONDS;
                jump_pending = 0; movement.jump = input.keys[MAAGE_INPUT_KEY_SPACE];
            }
            alpha = accumulator / MAAGE_PLAYER_TICK_SECONDS;
        } else { accumulator = 0; previous = player; alpha = 1; jump_pending = 0; }
        for (axis = 0; axis < 3; ++axis) eye[axis] = previous.position[axis] + (player.position[axis] - previous.position[axis]) * alpha;
        eye[1] += MAAGE_PLAYER_EYE_HEIGHT;
        if (state.resized) maage_renderer_resize(renderer);
        if (state.framebuffer_width > 0 && state.framebuffer_height > 0) {
            maage_client_camera(matrix, eye, yaw, pitch, (double)state.framebuffer_width / state.framebuffer_height);
            rendered = maage_renderer_draw_mesh(renderer, scene, matrix);
            if (rendered != MAAGE_RENDER_OK && rendered != MAAGE_RENDER_SKIPPED) goto done;
        } else rendered = MAAGE_RENDER_SKIPPED;
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
        maage_log_error("maage.client", "Renderer failed: %s. Phase 1 requires textured mesh support in the selected family.", maage_render_result_string(rendered));
    if (window) maage_window_capture_mouse(window, 0);
    maage_render_mesh_destroy(scene); maage_renderer_destroy(renderer);
    if (window) maage_window_destroy(window);
    if (system) maage_window_system_destroy(system);
    maage_world_mesh_destroy(mesh); maage_world_destroy(world); maage_block_registry_destroy(registry);
    maage_client_resources_destroy(&resources);
    return ok ? 0 : 1;
}
