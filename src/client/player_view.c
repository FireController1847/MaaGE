#include "player_view.h"
#include "maage/log.h"
#include <math.h>
#include <string.h>

static double player_arc(double angle) {
    return atan2(sin(angle), cos(angle));
}
static maage_status player_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_CLIENT, operation, 0);
}
static maage_status player_geometry(maage_client_player_view *view, maage_renderer *renderer, maage_render_texture *atlas, const maage_client_resources *resources, maage_status_trace *trace) {
    maage_mesh_vertex vertices[48];
    unsigned int indices[72], part;
    maage_mesh_data data;
    maage_status result, outcome = player_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPLOAD);
    if (view->attempted) return player_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPLOAD);
    view->attempted = 1;
    for (part = 0; part < MAAGE_PLAYER_PARTS; ++part) {
        maage_player_part_geometry(resources->skin_model, part, resources->skin_uv, vertices, indices, &data);
        result = maage_renderer_upload_geometry(renderer, &data, atlas, &view->parts[part], trace);
        view->results[part] = result;
        if (maage_status_failed(result)) {
            maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.player", result, trace, "Player part unavailable; omitting it until resource reload.");
            maage_status_trace_recover(trace, player_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_PRESENT), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__);
            outcome = player_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_UPLOAD);
        }
    }
    return outcome;
}
static void player_held_matrix(float matrix[16], const maage_item_definition *item, unsigned int model) {
    const double radians = 0.017453292519943295;
    maage_player_matrix_translate(matrix, model == MAAGE_SKIN_SLIM ? 0.5 : 1, -7, 1);
    maage_player_matrix_scale(matrix, 16, 16, 16);
    if (item->model == MAAGE_ITEM_MODEL_BLOCK) {
        maage_player_matrix_translate(matrix, 0, -0.1875, -0.3125);
        maage_player_matrix_rotate(matrix, 0, -20 * radians);
        maage_player_matrix_rotate(matrix, 1, -45 * radians);
        maage_player_matrix_scale(matrix, 0.375, 0.375, 0.375);
    } else {
        if (item->tool_class) {
            maage_player_matrix_translate(matrix, 0, -0.1875, 0);
            maage_player_matrix_scale(matrix, 0.625, -0.625, 0.625);
            maage_player_matrix_rotate(matrix, 0, 100 * radians);
            maage_player_matrix_rotate(matrix, 1, -45 * radians);
        } else {
            maage_player_matrix_translate(matrix, -0.25, -0.1875, -0.1875);
            maage_player_matrix_scale(matrix, 0.375, 0.375, 0.375);
            maage_player_matrix_rotate(matrix, 2, 60 * radians);
            maage_player_matrix_rotate(matrix, 0, 90 * radians);
            maage_player_matrix_rotate(matrix, 2, 20 * radians);
        }
        /* Equipped-item transform, converted from the model's X/Y-down
         * basis. The final half-unit offset addresses our centered cache. */
        maage_player_matrix_translate(matrix, 0, 0.3, 0);
        maage_player_matrix_scale(matrix, 1.5, 1.5, 1.5);
        maage_player_matrix_rotate(matrix, 1, -50 * radians);
        maage_player_matrix_rotate(matrix, 2, -25 * radians);
        maage_player_matrix_translate(matrix, 0.9375, 0.0625, 0);
        maage_player_matrix_rotate(matrix, 2, 3.141592653589793);
        maage_player_matrix_translate(matrix, 0.5, 0.5, 0);
        if (item->tool_class) {
            /* Both icon faces have identical XY/UVs. Exchange their thickness
             * planes to cancel the tool profile's reflection and retain the
             * shared culled pipelines, without mirroring its visible texture. */
            maage_player_matrix_translate(matrix, 0, 0, -0.0625);
            maage_player_matrix_scale(matrix, 1, 1, -1);
        }
    }
}
maage_status maage_client_player_view_update(maage_client_player_view *view, maage_renderer *renderer, maage_render_texture *atlas, const maage_client_resources *resources, const maage_item_registry *items, maage_client_item_entities *models, const maage_session_snapshot *previous, const maage_session_snapshot *snapshot, double interpolation, const double eye[3], double yaw, double pitch, double elapsed, double swing_progress, int visible, double camera_distance, maage_status_trace *trace) {
    const maage_item_stack *held;
    const maage_item_definition *item;
    const maage_render_mesh *mesh = NULL;
    maage_player_pose pose;
    maage_render_instance *draw;
    maage_status result, outcome = player_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
    double dx, dz, target, heading, relative, ticks, phase, amount, swing, age, death;
    float root[16], part_matrix[16], hand[16], sky = 1, light = 0;
    unsigned int part;
    int changed;
    if (!view || !renderer || !atlas || !resources || !items || !models || !previous || !snapshot || !eye) return player_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    view->count = 0;
    changed = !view->initialized || view->session != snapshot->session_epoch || view->world != snapshot->world_epoch || view->life != snapshot->player.motion.life;
    if (changed) {
        view->session = snapshot->session_epoch;
        view->world = snapshot->world_epoch;
        view->tick = snapshot->tick;
        view->life = snapshot->player.motion.life;
        view->body_yaw = yaw;
        view->phase = view->previous_phase = view->amount = view->previous_amount = 0;
        view->gait_sign = 1;
        view->initialized = 1;
    }
    if (elapsed < 0) elapsed = 0;
    if (elapsed > 0.1) elapsed = 0.1;
    if (snapshot->tick != view->tick) {
        ticks = snapshot->tick > previous->tick ? (double) (snapshot->tick - previous->tick) : 1;
        dx = (snapshot->player.player.position[0] - previous->player.player.position[0]) / ticks;
        dz = (snapshot->player.player.position[2] - previous->player.player.position[2]) / ticks;
        target = sqrt(dx * dx + dz * dz) * 4;
        if (target > 1) target = 1;
        view->previous_phase = view->phase;
        view->previous_amount = view->amount;
        view->amount += (target - view->amount) * (1 - pow(0.6, ticks));
        view->phase += view->amount * ticks;
        view->tick = snapshot->tick;
    }
    dx = snapshot->player.player.position[0] - previous->player.player.position[0];
    dz = snapshot->player.player.position[2] - previous->player.player.position[2];
    ticks = snapshot->tick > previous->tick ? (double) (snapshot->tick - previous->tick) : 1;
    heading = view->body_yaw;
    view->gait_sign = 1;
    if ((dx * dx + dz * dz) / (ticks * ticks) > 0.0025) {
        heading = atan2(dx, -dz);
        if (cos(heading - yaw) < 0) {
            heading += 3.141592653589793;
            view->gait_sign = -1;
        }
    }
    if (snapshot->player.swing > 0) heading = yaw;
    if (!snapshot->paused) view->body_yaw += player_arc(heading - view->body_yaw) * (1 - pow(0.7, elapsed / 0.05));
    relative = player_arc(yaw - view->body_yaw);
    if (relative > 1.308996938995747) relative = 1.308996938995747;
    if (relative < -1.308996938995747) relative = -1.308996938995747;
    if (!snapshot->paused) {
        view->body_yaw += player_arc(yaw - relative - view->body_yaw) * (1 - pow(0.7, elapsed / 0.05));
        if (fabs(relative) > 0.872664625997165) view->body_yaw += relative * (1 - pow(0.8, elapsed / 0.05));
    }
    if (!visible || camera_distance < 0.35) return outcome;
    result = player_geometry(view, renderer, atlas, resources, trace);
    if (maage_status_is_warning(result)) outcome = result;
    held = maage_inventory_selected(&snapshot->player.inventory);
    item = held && held->count ? maage_item_registry_get(items, held->item_id) : NULL;
    phase = view->previous_phase + (view->phase - view->previous_phase) * interpolation;
    amount = view->previous_amount + (view->amount - view->previous_amount) * interpolation;
    swing = swing_progress;
    age = (double) previous->tick + ((double) snapshot->tick - previous->tick) * interpolation;
    relative = player_arc(yaw - view->body_yaw);
    if (relative > 1.308996938995747) relative = 1.308996938995747;
    if (relative < -1.308996938995747) relative = -1.308996938995747;
    maage_player_pose_calculate(resources->skin_model, phase, amount * view->gait_sign, age, relative, pitch, swing, item != NULL, snapshot->player.sneaking, &pose);
    maage_player_matrix_identity(root);
    maage_player_matrix_translate(root, eye[0], eye[1] - MAAGE_PLAYER_EYE_HEIGHT, eye[2]);
    if (snapshot->player.sneaking) maage_player_matrix_translate(root, 0, -3 * 0.9375 / 16, 0);
    maage_player_matrix_rotate(root, 1, -view->body_yaw);
    if (!snapshot->player.health) {
        death = (double) snapshot->tick - snapshot->player.death_tick + interpolation - 1;
        death = death > 0 ? sqrt(death * 0.08) : 0;
        if (death > 1) death = 1;
        maage_player_matrix_rotate(root, 2, death * 1.5707963267948966);
    }
    maage_player_matrix_scale(root, 0.9375 / 16, 0.9375 / 16, 0.9375 / 16);
    if (snapshot->has_observation && snapshot->observation.light_available) {
        sky = (float) (snapshot->observation.light >> 4) / 15;
        light = (float) (snapshot->observation.light & 15) / 15;
    }
    for (part = 0; part < MAAGE_PLAYER_PARTS; ++part) {
        maage_player_part_matrix(part_matrix, &pose, part);
        maage_player_matrix_multiply(part_matrix, root, part_matrix);
        if (part == MAAGE_PLAYER_RIGHT_ARM) memcpy(hand, part_matrix, sizeof(hand));
        /* A compressed boom can put the near plane inside the head. */
        if (!view->parts[part] || (part == MAAGE_PLAYER_HEAD && camera_distance < 0.8)) continue;
        draw = &view->draws[view->count++];
        memset(draw, 0, sizeof(*draw));
        draw->mesh = view->parts[part];
        memcpy(draw->transform, part_matrix, sizeof(part_matrix));
        draw->sky_light = sky;
        draw->block_light = light;
    }
    if (item) {
        result = maage_client_item_entities_mesh(models, held, &mesh, trace);
        if (maage_status_failed(result)) {
            if (result != view->item_failure) {
                maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.player", result, trace, "Held item unavailable; omitting it until resource reload.");
                view->item_failure = result;
                outcome = player_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_PRESENT);
            }
            maage_status_trace_recover(trace, player_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_PRESENT), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__);
        } else {
            player_held_matrix(hand, item, resources->skin_model);
            draw = &view->draws[view->count++];
            memset(draw, 0, sizeof(*draw));
            draw->mesh = mesh;
            memcpy(draw->transform, hand, sizeof(hand));
            draw->sky_light = sky;
            draw->block_light = light;
        }
    }
    return outcome;
}
maage_status maage_client_player_view_destroy(maage_client_player_view *view) {
    maage_status result, outcome = player_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
    unsigned int part;
    if (!view) return player_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (part = 0; part < MAAGE_PLAYER_PARTS; ++part) {
        result = maage_render_mesh_destroy(view->parts[part]);
        if (maage_status_failed(result)) outcome = result;
    }
    memset(view, 0, sizeof(*view));
    return outcome;
}
