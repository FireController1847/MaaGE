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
            ++prediction->generation;
            if (!event->accepted) prediction->entries[i].generation = prediction->generation;
            prediction->dirty = 1;
            return;
        }
}
static maage_status maage_prediction_filter(maage_renderer *renderer, maage_render_texture *atlas, const maage_world_mesh *source, int x, int z, const maage_block_position *centers, size_t count, maage_render_mesh **replacement, int *filtered, maage_status_trace *trace) {
    maage_mesh_data data;
    unsigned int *indices = NULL;
    size_t i;
    maage_status result;
    *replacement = NULL;
    *filtered = 0;
    for (i = 0; i < count; ++i)
        if (maage_world_chunk_coordinate(centers[i].x - 3) <= x && maage_world_chunk_coordinate(centers[i].x + 3) >= x && maage_world_chunk_coordinate(centers[i].z - 3) <= z && maage_world_chunk_coordinate(centers[i].z + 3) >= z) break;
    if (i == count) return maage_prediction_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MESH);
    if (!source) return maage_prediction_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_MESH);
    result = maage_world_mesh_filter(source, centers, count, &data, &indices);
    if (!maage_status_failed(result) && data.index_count) result = maage_renderer_upload_geometry(renderer, &data, atlas, replacement, trace);
    free(indices);
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) *filtered = 1;
    return result;
}
maage_status maage_client_prediction_mesh(const maage_client_prediction *prediction, maage_renderer *renderer, maage_render_texture *atlas, const maage_world_mesh *source, int x, int z, maage_render_mesh **replacement, int *filtered, maage_status_trace *trace) {
    return maage_prediction_filter(renderer, atlas, source, x, z, prediction->published, prediction->published_count, replacement, filtered, trace);
}
static maage_status maage_prediction_present(maage_client_prediction *prediction, maage_world *visual) {
    maage_block_position positions[MAAGE_CLIENT_PREDICTIONS];
    maage_block_state_id expected[MAAGE_CLIENT_PREDICTIONS], replacements[MAAGE_CLIENT_PREDICTIONS];
    unsigned long confirmed[MAAGE_CLIENT_PREDICTIONS];
    unsigned int order[MAAGE_CLIENT_PREDICTIONS], i, j, count = 0, entry;
    /* Array vacancies are reused. Compose overlapping edits in action order,
     * so a pending break followed by placement keeps the new block visible. */
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active && prediction->entries[i].confirmed >= 0) {
            for (j = count; j && prediction->entries[order[j - 1]].id > prediction->entries[i].id; --j)
                order[j] = order[j - 1];
            order[j] = i;
            ++count;
        }
    for (i = 0; i < count; ++i) {
        entry = order[i];
        positions[i].x = prediction->entries[entry].target.x;
        positions[i].y = prediction->entries[entry].target.y;
        positions[i].z = prediction->entries[entry].target.z;
        expected[i] = prediction->entries[entry].target.state_id;
        replacements[i] = prediction->entries[entry].replacement;
        confirmed[i] = prediction->entries[entry].confirmed > 0 ? prediction->entries[entry].revisions[4] : 0;
    }
    return maage_world_present_predictions(visual, positions, expected, replacements, confirmed, count, prediction->generation);
}
static maage_status maage_prediction_rebuild(maage_client_prediction *prediction, maage_renderer *renderer, maage_render_texture *atlas, maage_world *visual, maage_client_chunk_mesh *meshes, size_t capacity, maage_status_trace *trace) {
    maage_block_position centers[MAAGE_CLIENT_PREDICTIONS];
    maage_world_mesh *patch = NULL;
    maage_mesh_data data;
    maage_render_mesh *reveal = NULL;
    unsigned int i, count = 0;
    size_t column;
    maage_status result = maage_prediction_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
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
        if (data.index_count) result = maage_renderer_upload_geometry(renderer, &data, atlas, &reveal, trace);
        maage_world_mesh_destroy(patch);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto failed;
    }
    /* Prepare every replacement before changing any draw pointer. A failed
     * allocation/upload leaves the previous complete transaction drawable. */
    for (column = 0; column < capacity; ++column)
        if (meshes[column].used) {
            if (meshes[column].filtered_revision == meshes[column].visual_revision && meshes[column].filtered_generation == prediction->generation) continue;
            result = maage_prediction_filter(renderer, atlas, meshes[column].source, meshes[column].x, meshes[column].z, centers, count, &meshes[column].prepared, &meshes[column].prepared_filtered, trace);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) goto failed;
            meshes[column].prepared_changed = 1;
        }
    for (column = 0; column < capacity; ++column)
        if (meshes[column].prepared_changed) {
            maage_render_mesh_destroy(meshes[column].prediction);
            meshes[column].prediction = meshes[column].prepared;
            meshes[column].prepared = NULL;
            meshes[column].filtered = meshes[column].prepared_filtered;
            meshes[column].prepared_changed = 0;
            meshes[column].filtered_revision = meshes[column].visual_revision;
            meshes[column].filtered_generation = prediction->generation;
        }
    maage_render_mesh_destroy(prediction->reveal);
    prediction->reveal = reveal;
    memcpy(prediction->published, centers, count * sizeof(*centers));
    prediction->published_count = count;
    prediction->applied_generation = prediction->generation;
    prediction->visual_revision = maage_world_presentation_revision(visual);
    prediction->dirty = 0;
    return result;
failed:
    maage_render_mesh_destroy(reveal);
    for (column = 0; column < capacity; ++column) {
        maage_render_mesh_destroy(meshes[column].prepared);
        meshes[column].prepared = NULL;
        meshes[column].prepared_changed = 0;
    }
    return result;
}
maage_status maage_client_prediction_update(maage_client_prediction *prediction, const maage_item_registry *items, const maage_block_registry *blocks, maage_world *visual, const maage_session_snapshot *snapshot, maage_client_chunk_mesh *meshes, size_t capacity, const double eye[3], double yaw, double pitch, double elapsed, double input_time, maage_leaves_mode leaves, int active, int held, int pressed, int use, int use_pressed, int sneaking, unsigned int hotbar, maage_session_command *command) {
    maage_world_target target, destination;
    maage_block_state_id placed;
    double direction[3], damage;
    const maage_item_stack *stack;
    const maage_item_definition *tool;
    const maage_block_definition *block;
    maage_client_chunk_mesh *column;
    maage_status result;
    unsigned int i, index, vacant = MAAGE_CLIENT_PREDICTIONS;
    int dx, dz, ready;
    memset(command, 0, sizeof(*command));
    if (hotbar >= MAAGE_HOTBAR_SLOTS) return maage_prediction_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    if (prediction->session != snapshot->session_epoch || prediction->world != snapshot->world_epoch) {
        maage_client_prediction_destroy(prediction);
        prediction->diagnostics = getenv("MAAGE_WORLD_DIAGNOSTICS") && !strcmp(getenv("MAAGE_WORLD_DIAGNOSTICS"), "1");
        prediction->session = snapshot->session_epoch;
        prediction->world = snapshot->world_epoch;
        ++prediction->generation;
    }
    ++prediction->frame;
    if (pressed || use_pressed) prediction->input_at = prediction->diagnostics ? input_time : -1;
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
                    if (maage_world_chunk_loaded(visual, x, z) && (!column || column->presentation_generation < prediction->entries[i].generation || !maage_world_mesh_current(visual, x, z, column->visual_revision))) ready = 0;
                }
            for (dz = maage_world_chunk_coordinate(prediction->entries[i].target.z - 3); dz <= maage_world_chunk_coordinate(prediction->entries[i].target.z + 3); ++dz)
                for (dx = maage_world_chunk_coordinate(prediction->entries[i].target.x - 3); dx <= maage_world_chunk_coordinate(prediction->entries[i].target.x + 3); ++dx) {
                    column = maage_prediction_column(meshes, capacity, dx, dz);
                    if (maage_world_chunk_loaded(visual, dx, dz) && (!column || column->presentation_generation < prediction->entries[i].generation || !maage_world_mesh_current(visual, dx, dz, column->visual_revision))) ready = 0;
                }
            if (ready) {
                if (prediction->diagnostics) maage_log_info("maage.client.latency", "action=%lu accepted=%d reconciled_geometry_ms=%.3f frames=%lu", prediction->entries[i].id, prediction->entries[i].confirmed > 0, (maage_time_monotonic_seconds() - prediction->entries[i].predicted_at) * 1000, prediction->frame - prediction->entries[i].frame);
                prediction->entries[i].active = 0;
                ++prediction->generation;
            }
        }
        if (!prediction->entries[i].active) vacant = i;
    }
    result = maage_prediction_present(prediction, visual);
    if (maage_status_failed(result)) return result;
    if (elapsed > 0.25) elapsed = 0.25;
    if (prediction->delay > 0) prediction->delay -= elapsed;
    if (pressed) prediction->delay = 0;
    if (prediction->use_delay > 0) prediction->use_delay -= elapsed;
    if (use_pressed || !use) prediction->use_delay = 0;
    direction[0] = sin(yaw) * cos(pitch);
    direction[1] = -sin(pitch);
    direction[2] = -cos(yaw) * cos(pitch);
    result = maage_world_raycast(visual, eye, direction, snapshot->player.reach, &target);
    if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_NOT_FOUND) return result;
    if (!active || !held || !snapshot->player.health || maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
        prediction->targeting = 0;
        prediction->progress = 0;
    } else {
        stack = &snapshot->player.inventory.slots[hotbar];
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
                prediction->entries[vacant].replacement = MAAGE_STATE_AIR;
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
    /* Use still travels as ordinary INPUT. The preview command merely asks
     * the Server to acknowledge the independently determined destination. */
    if (!command->kind && active && use && prediction->use_delay <= 0 && snapshot->player.health && !snapshot->player.crafting.kind && vacant < MAAGE_CLIENT_PREDICTIONS && prediction->next_id < 4294967295UL) {
        prediction->use_delay = 0.2;
        result = maage_world_raycast(visual, eye, direction, snapshot->player.reach, &target);
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
            stack = &snapshot->player.inventory.slots[hotbar];
            if (target.block_id == MAAGE_BLOCK_CRAFTING_TABLE && (!sneaking || !stack->count)) result = maage_prediction_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
            else result = maage_gameplay_placement(&snapshot->player.player, visual, items, stack, &target, yaw, &destination, &placed);
            if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
                memset(&prediction->entries[vacant], 0, sizeof(prediction->entries[vacant]));
                prediction->entries[vacant].active = 1;
                prediction->entries[vacant].id = ++prediction->next_id;
                prediction->entries[vacant].target = destination;
                prediction->entries[vacant].replacement = placed;
                prediction->entries[vacant].predicted_at = prediction->diagnostics ? maage_time_monotonic_seconds() : -1;
                prediction->entries[vacant].frame = prediction->frame;
                prediction->entries[vacant].generation = ++prediction->generation;
                command->kind = MAAGE_CMD_PLACE_PREVIEW;
                command->prediction = prediction->next_id;
                command->predicted_target = destination;
                command->predicted_state = placed;
            } else if (maage_status_failed(result)) return result;
        } else if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_NOT_FOUND) return result;
    }
    result = maage_prediction_present(prediction, visual);
    if (maage_status_failed(result)) return result;
    return maage_prediction_status(command->kind ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
}
maage_status maage_client_prediction_prepare(maage_client_prediction *prediction, maage_renderer *renderer, maage_render_texture *atlas, maage_world *visual, maage_client_chunk_mesh *meshes, size_t capacity, maage_status_trace *trace) {
    unsigned int i;
    int pending = 0;
    maage_status result;
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active) pending = 1;
    if (!prediction->dirty && prediction->generation == prediction->applied_generation && (!pending || prediction->visual_revision == maage_world_presentation_revision(visual))) return maage_prediction_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_PRESENT);
    prediction->dirty = 1;
    result = maage_prediction_rebuild(prediction, renderer, atlas, visual, meshes, capacity, trace);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    if (prediction->diagnostics)
        for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
            if (prediction->entries[i].active && prediction->entries[i].frame == prediction->frame) maage_log_info("maage.client.latency", "action=%lu prediction_geometry_ms=%.3f frames=0", prediction->entries[i].id, (maage_time_monotonic_seconds() - prediction->entries[i].predicted_at) * 1000);
    return result;
}
maage_status maage_client_prediction_destroy(maage_client_prediction *prediction) {
    maage_status result;
    if (!prediction) return maage_prediction_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    result = maage_render_mesh_destroy(prediction->reveal);
    memset(prediction, 0, sizeof(*prediction));
    return result;
}
