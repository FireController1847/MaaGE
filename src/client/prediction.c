#include "prediction.h"
#include "maage/time.h"
#include "maage/log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static maage_status maage_prediction_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_CLIENT, operation, 0);
}
static maage_client_chunk_mesh *maage_prediction_column(maage_client_chunk_mesh *meshes, size_t capacity, int x, int z) {
    size_t i;
    for (i = 0; i < capacity; ++i)
        if (meshes[i].used && meshes[i].x == x && meshes[i].z == z) return &meshes[i];
    return NULL;
}
int maage_client_prediction_hides(const maage_client_prediction *prediction, int x, int y, int z) {
    unsigned int i;
    if (prediction->suspended) return 0;
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active && prediction->entries[i].confirmed >= 0 && prediction->entries[i].target.x == x && prediction->entries[i].target.y == y && prediction->entries[i].target.z == z) return 1;
    return 0;
}
void maage_client_prediction_confirm(maage_client_prediction *prediction, const maage_session_event *event) {
    unsigned int i;
    if (!prediction || !event || event->kind != MAAGE_EVENT_BLOCK_ACTION || event->session_epoch != prediction->session || event->world_epoch != prediction->world) return;
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active && !prediction->entries[i].confirmed && prediction->entries[i].id == event->prediction) {
            if (event->block.x != prediction->entries[i].target.x || event->block.y != prediction->entries[i].target.y || event->block.z != prediction->entries[i].target.z) return;
            if (prediction->diagnostics) maage_log_info("maage.client.latency", "action=%lu accepted=%d acknowledgment_ms=%.3f", event->prediction, event->accepted, (maage_time_monotonic_seconds() - prediction->entries[i].predicted_at) * 1000);
            prediction->entries[i].confirmed = event->accepted ? 1 : -1;
            memcpy(prediction->entries[i].revisions, event->column_revisions, sizeof(event->column_revisions));
            /* Rejected geometry is rebuilt from the effective confirmed state
             * now, and retained until complete meshes absorb this correction. */
            if (!event->accepted) prediction->entries[i].generation = ++prediction->generation;
            prediction->dirty = 1;
            return;
        }
}
static maage_status maage_prediction_rebuild(maage_client_prediction *prediction, maage_renderer *renderer, maage_render_texture *atlas, maage_world *visual, maage_client_chunk_mesh *meshes, size_t capacity, maage_status_trace *trace) {
    maage_block_position centers[MAAGE_CLIENT_PREDICTIONS];
    maage_world_mesh *patch = NULL;
    maage_mesh_data data;
    maage_render_mesh *replacement = NULL;
    unsigned int *indices;
    unsigned int i, count = 0;
    size_t column;
    maage_status result;
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active) {
            centers[count].x = prediction->entries[i].target.x;
            centers[count].y = prediction->entries[i].target.y;
            centers[count++].z = prediction->entries[i].target.z;
        }
    if (count) {
        result = maage_world_mesh_patch(visual, centers, count, prediction->leaves, &patch, trace);
        if (maage_status_failed(result)) return result;
        data = *maage_world_mesh_data(patch);
        if (data.index_count) result = maage_renderer_upload_geometry(renderer, &data, atlas, &replacement, trace);
        maage_world_mesh_destroy(patch);
        if (maage_status_failed(result)) return result;
    }
    maage_render_mesh_destroy(prediction->reveal);
    prediction->reveal = replacement;
    for (column = 0; column < capacity; ++column)
        if (meshes[column].used) {
            for (i = 0; i < count; ++i)
                if (maage_world_chunk_coordinate(centers[i].x - 3) <= meshes[column].x && maage_world_chunk_coordinate(centers[i].x + 3) >= meshes[column].x && maage_world_chunk_coordinate(centers[i].z - 3) <= meshes[column].z && maage_world_chunk_coordinate(centers[i].z + 3) >= meshes[column].z) break;
            if (i == count) {
                maage_render_mesh_destroy(meshes[column].prediction);
                meshes[column].prediction = NULL;
                meshes[column].filtered = 0;
                continue;
            }
            if (!meshes[column].source || (meshes[column].filtered && meshes[column].filtered_revision == meshes[column].visual_revision && meshes[column].filtered_generation == prediction->generation)) continue;
            result = maage_world_mesh_filter(meshes[column].source, centers, count, &data, &indices);
            if (maage_status_failed(result)) return result;
            replacement = NULL;
            if (data.index_count) result = maage_renderer_upload_geometry(renderer, &data, atlas, &replacement, trace);
            free(indices);
            if (maage_status_failed(result)) return result;
            maage_render_mesh_destroy(meshes[column].prediction);
            meshes[column].prediction = replacement;
            meshes[column].filtered = 1;
            meshes[column].filtered_revision = meshes[column].visual_revision;
            meshes[column].filtered_generation = prediction->generation;
        }
    prediction->applied_generation = prediction->generation;
    prediction->visual_revision = maage_world_presentation_revision(visual);
    prediction->dirty = 0;
    return maage_prediction_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
}
maage_status maage_client_prediction_update(maage_client_prediction *prediction, maage_renderer *renderer, maage_render_texture *atlas, const maage_item_registry *items, const maage_block_registry *blocks, maage_world *visual, const maage_session_snapshot *snapshot, maage_client_chunk_mesh *meshes, size_t capacity, const double eye[3], double yaw, double pitch, double elapsed, double input_time, maage_leaves_mode leaves, int active, int held, int pressed, maage_session_command *command, maage_status_trace *trace) {
    maage_world_target target;
    double direction[3], damage;
    maage_block_position positions[MAAGE_CLIENT_PREDICTIONS];
    maage_block_state_id expected[MAAGE_CLIENT_PREDICTIONS];
    size_t count = 0;
    const maage_item_stack *stack;
    const maage_item_definition *tool;
    const maage_block_definition *block;
    maage_client_chunk_mesh *column;
    maage_status result;
    unsigned int i, index, vacant = MAAGE_CLIENT_PREDICTIONS;
    int dx, dz, ready, pending = 0;
    memset(command, 0, sizeof(*command));
    if (prediction->session != snapshot->session_epoch || prediction->world != snapshot->world_epoch) {
        maage_client_prediction_destroy(prediction);
        prediction->diagnostics = getenv("MAAGE_WORLD_DIAGNOSTICS") && !strcmp(getenv("MAAGE_WORLD_DIAGNOSTICS"), "1");
        prediction->session = snapshot->session_epoch;
        prediction->world = snapshot->world_epoch;
        ++prediction->generation;
    }
    ++prediction->frame;
    if (pressed) prediction->input_at = prediction->diagnostics ? input_time : -1;
    if (prediction->leaves != leaves) {
        prediction->leaves = leaves;
        ++prediction->generation;
    }
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i) {
        if (prediction->entries[i].active && !maage_world_chunk_loaded(visual, maage_world_chunk_coordinate(prediction->entries[i].target.x), maage_world_chunk_coordinate(prediction->entries[i].target.z))) {
            prediction->entries[i].active = 0;
            ++prediction->generation;
        }
        if (prediction->entries[i].active && prediction->entries[i].confirmed) {
            ready = 1;
            for (dz = -1; dz <= 1; ++dz)
                for (dx = -1; dx <= 1; ++dx) {
                    int x = maage_world_chunk_coordinate(prediction->entries[i].target.x + dx);
                    int z = maage_world_chunk_coordinate(prediction->entries[i].target.z + dz);
                    index = (unsigned int) (dx + 1 + 3 * (dz + 1));
                    if (prediction->entries[i].confirmed > 0 && maage_world_column_revision(visual, x, z) < prediction->entries[i].revisions[index]) ready = 0;
                    column = maage_prediction_column(meshes, capacity, x, z);
                    if (column && column->presentation_generation < prediction->entries[i].generation) ready = 0;
                }
            for (dz = maage_world_chunk_coordinate(prediction->entries[i].target.z - 3); dz <= maage_world_chunk_coordinate(prediction->entries[i].target.z + 3); ++dz)
                for (dx = maage_world_chunk_coordinate(prediction->entries[i].target.x - 3); dx <= maage_world_chunk_coordinate(prediction->entries[i].target.x + 3); ++dx) {
                    column = maage_prediction_column(meshes, capacity, dx, dz);
                    if (column && column->presentation_generation < prediction->entries[i].generation) ready = 0;
                }
            if (ready) {
                if (prediction->diagnostics) maage_log_info("maage.client.latency", "action=%lu accepted=%d reconciled_geometry_ms=%.3f frames=%lu", prediction->entries[i].id, prediction->entries[i].confirmed > 0, (maage_time_monotonic_seconds() - prediction->entries[i].predicted_at) * 1000, prediction->frame - prediction->entries[i].frame);
                prediction->entries[i].active = 0;
                ++prediction->generation;
            }
        }
        if (!prediction->entries[i].active) vacant = i;
        else pending = 1;
    }
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active && prediction->entries[i].confirmed >= 0 && !prediction->suspended) {
            positions[count].x = prediction->entries[i].target.x;
            positions[count].y = prediction->entries[i].target.y;
            positions[count].z = prediction->entries[i].target.z;
            expected[count++] = prediction->entries[i].target.state_id;
        }
    result = maage_world_present_predictions(visual, positions, expected, count, prediction->generation);
    if (maage_status_failed(result)) return result;
    count = 0;
    if (prediction->suspended) {
        if (pending || !pressed) goto reconcile;
        prediction->suspended = 0;
    }
    if (elapsed > 0.25) elapsed = 0.25;
    if (prediction->delay > 0) prediction->delay -= elapsed;
    if (pressed) prediction->delay = 0;
    direction[0] = sin(yaw) * cos(pitch);
    direction[1] = -sin(pitch);
    direction[2] = -cos(yaw) * cos(pitch);
    result = maage_world_raycast(visual, eye, direction, snapshot->player.reach, &target);
    if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_NOT_FOUND) return result;
    if (!active || !held || !snapshot->player.health || maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
        prediction->targeting = 0;
        prediction->progress = 0;
    } else {
        stack = maage_inventory_selected(&snapshot->player.inventory);
        if (!prediction->targeting || target.x != prediction->target.x || target.y != prediction->target.y || target.z != prediction->target.z || target.state_id != prediction->target.state_id || stack->item_id != prediction->held.item_id || stack->metadata != prediction->held.metadata || stack->damage != prediction->held.damage || pressed) {
            prediction->targeting = 1;
            prediction->target = target;
            prediction->held = *stack;
            prediction->progress = 0;
        }
        block = maage_block_registry_block(blocks, target.block_id);
        tool = stack->count ? maage_item_registry_get(items, stack->item_id) : NULL;
        damage = snapshot->player.game_mode == MAAGE_GAME_MODE_CREATIVE ? 1 : maage_item_mining_damage(tool, block, snapshot->player.player.grounded, snapshot->player.submerged);
        if (prediction->delay <= 0) {
            if (snapshot->player.breaking_active && snapshot->player.breaking.x == target.x && snapshot->player.breaking.y == target.y && snapshot->player.breaking.z == target.z && snapshot->player.breaking.state_id == target.state_id && snapshot->player.break_progress > prediction->progress) prediction->progress = snapshot->player.break_progress;
            prediction->progress += damage * elapsed / 0.05;
            if (damage >= 1) prediction->progress = 1;
            if (prediction->progress >= 1 && vacant < MAAGE_CLIENT_PREDICTIONS && prediction->next_id < 4294967295UL) {
                prediction->entries[vacant].predicted_at = prediction->diagnostics ? maage_time_monotonic_seconds() : -1;
                prediction->entries[vacant].frame = prediction->frame;
                prediction->entries[vacant].submitted = 0;
                if (prediction->diagnostics) maage_log_info("maage.client.latency", "action=%lu input_to_prediction_ms=%.3f predicted_at=%.6f", prediction->next_id + 1, (prediction->entries[vacant].predicted_at - prediction->input_at) * 1000, prediction->entries[vacant].predicted_at);
                prediction->entries[vacant].active = 1;
                prediction->entries[vacant].confirmed = 0;
                prediction->entries[vacant].id = ++prediction->next_id;
                prediction->entries[vacant].target = target;
                memset(prediction->entries[vacant].revisions, 0, sizeof(prediction->entries[vacant].revisions));
                command->kind = MAAGE_CMD_BREAK_PREVIEW;
                command->prediction = prediction->next_id;
                command->predicted_target = target;
                prediction->progress = 0;
                prediction->targeting = 0;
                prediction->delay = snapshot->player.game_mode == MAAGE_GAME_MODE_CREATIVE || damage < 1 ? 0.25 : 0.05;
                prediction->entries[vacant].generation = ++prediction->generation;
            }
        }
    }
reconcile:
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active && prediction->entries[i].confirmed >= 0 && !prediction->suspended) {
            positions[count].x = prediction->entries[i].target.x;
            positions[count].y = prediction->entries[i].target.y;
            positions[count].z = prediction->entries[i].target.z;
            expected[count++] = prediction->entries[i].target.state_id;
        }
    result = maage_world_present_predictions(visual, positions, expected, count, prediction->generation);
    if (maage_status_failed(result)) return result;
    if (prediction->dirty || prediction->generation != prediction->applied_generation || (pending && prediction->visual_revision != maage_world_presentation_revision(visual))) {
        result = maage_prediction_rebuild(prediction, renderer, atlas, visual, meshes, capacity, trace);
        if (maage_status_failed(result)) return result;
        if (prediction->diagnostics && command->kind) maage_log_info("maage.client.latency", "action=%lu prediction_geometry_ms=%.3f frames=0", command->prediction, (maage_time_monotonic_seconds() - prediction->entries[vacant].predicted_at) * 1000);
    }
    return maage_prediction_status(command->kind ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
}
maage_status maage_client_prediction_destroy(maage_client_prediction *prediction) {
    maage_status result;
    if (!prediction) return maage_prediction_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    result = maage_render_mesh_destroy(prediction->reveal);
    memset(prediction, 0, sizeof(*prediction));
    return result;
}
