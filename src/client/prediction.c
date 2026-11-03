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
            if (prediction->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "action=%lu accepted=%d acknowledgment_ms=%.3f owner_tick=%lu prediction_tick=%lu event_transport_ms=%.3f", event->prediction, event->accepted, (maage_time_monotonic_seconds() - prediction->entries[i].predicted_at) * 1000, event->tick, prediction->entries[i].tick, event->emitted_at >= 0 ? (maage_time_monotonic_seconds() - event->emitted_at) * 1000 : -1));
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
static maage_status maage_prediction_filter(const maage_render_mesh *gpu_source, const maage_world_mesh *source, int x, int z, const maage_block_position *centers, size_t count, maage_render_mesh **replacement, int *filtered, maage_status_trace *trace) {
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
    if (!maage_status_failed(result) && data.index_count) {
        if (!gpu_source) result = maage_prediction_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_MESH);
        else result = maage_renderer_upload_index_view(gpu_source, &data, maage_world_mesh_face_flags(source), replacement, trace);
    }
    free(indices);
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) *filtered = 1;
    return result;
}
maage_status maage_client_prediction_mesh(const maage_client_prediction *prediction, const maage_render_mesh *gpu_source, const maage_world_mesh *source, int x, int z, maage_render_mesh **replacement, int *filtered, maage_status_trace *trace) {
    return maage_prediction_filter(gpu_source, source, x, z, prediction->published, prediction->published_count, replacement, filtered, trace);
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
    const maage_world_mesh *sources[MAAGE_CLIENT_PREDICTIONS * 4];
    size_t source_count = 0, center;
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
    for (column = 0; column < capacity; ++column)
        if (meshes[column].used && meshes[column].source) {
            for (center = 0; center < count; ++center)
                if (maage_world_chunk_coordinate(centers[center].x - 3) <= meshes[column].x && maage_world_chunk_coordinate(centers[center].x + 3) >= meshes[column].x && maage_world_chunk_coordinate(centers[center].z - 3) <= meshes[column].z && maage_world_chunk_coordinate(centers[center].z + 3) >= meshes[column].z) break;
            if (center < count) {
                if (source_count == MAAGE_CLIENT_PREDICTIONS * 4) return maage_prediction_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_MESH);
                sources[source_count++] = meshes[column].source;
            }
        }
    if (count) {
        result = maage_world_mesh_patch(visual, centers, count, sources, source_count, prediction->leaves, prediction->biome_blend, &patch, trace);
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
            result = maage_prediction_filter(meshes[column].mesh, meshes[column].source, meshes[column].x, meshes[column].z, centers, count, &meshes[column].prepared, &meshes[column].prepared_filtered, trace);
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
maage_status maage_client_prediction_update(maage_client_prediction *prediction, const maage_item_registry *items, const maage_block_registry *blocks, maage_world *visual, const maage_session_snapshot *snapshot, maage_client_chunk_mesh *meshes, size_t capacity, const double eye[3], double yaw, double pitch, double elapsed, double input_time, unsigned long input_sequence, maage_leaves_mode leaves, maage_biome_blend blend, int active, int held, int pressed, int use, int use_pressed, int sneaking, unsigned int hotbar, int entity_target, maage_session_command *command) {
    const maage_registry_bindings *ids = maage_block_registry_bindings(maage_world_registry(visual));
    maage_world_target target, destination;
    maage_block_state_id placed;
    double direction[3], damage, progress, phase;
    const maage_item_stack *stack;
    const maage_item_definition *tool;
    const maage_block_definition *block;
    maage_client_chunk_mesh *column;
    maage_status result;
    unsigned int i, index, vacant = MAAGE_CLIENT_PREDICTIONS;
    int dx, dz, ready, completed, matching;
    memset(command, 0, sizeof(*command));
    if (hotbar >= MAAGE_HOTBAR_SLOTS) return maage_prediction_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    if (prediction->session != snapshot->session_epoch || prediction->world != snapshot->world_epoch) {
        maage_client_prediction_destroy(prediction);
        prediction->diagnostics = maage_log_enabled("maage.client.latency", MAAGE_LOG_VERBOSE) && getenv("MAAGE_WORLD_DIAGNOSTICS") && !strcmp(getenv("MAAGE_WORLD_DIAGNOSTICS"), "1");
        prediction->session = snapshot->session_epoch;
        prediction->world = snapshot->world_epoch;
        ++prediction->generation;
    }
    ++prediction->frame;
    if (pressed || use_pressed) prediction->input_at = prediction->diagnostics ? input_time : -1;
    if (prediction->leaves != leaves || prediction->biome_blend != blend) {
        prediction->biome_blend = blend;
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
            if (ready && !prediction->entries[i].sound_pending) {
                if (prediction->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "action=%lu accepted=%d reconciled_geometry_ms=%.3f frames=%lu", prediction->entries[i].id, prediction->entries[i].confirmed > 0, (maage_time_monotonic_seconds() - prediction->entries[i].predicted_at) * 1000, prediction->frame - prediction->entries[i].frame));
                prediction->entries[i].active = 0;
                ++prediction->generation;
            }
        }
        if (!prediction->entries[i].active) vacant = i;
    }
    result = maage_prediction_present(prediction, visual);
    if (maage_status_failed(result)) return result;
    if (elapsed < 0) elapsed = 0;
    if (elapsed > 0.25) elapsed = 0.25;
    if (prediction->use_delay > 0) prediction->use_delay -= elapsed;
    if (use_pressed || !use) prediction->use_delay = 0;
    direction[0] = sin(yaw) * cos(pitch);
    direction[1] = -sin(pitch);
    direction[2] = -cos(yaw) * cos(pitch);
    result = maage_world_raycast(visual, eye, direction, snapshot->player.reach, &target);
    if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_NOT_FOUND) return result;
    if (entity_target || !active || !held || !snapshot->player.health || snapshot->paused || snapshot->player.crafting.kind || maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
        prediction->targeting = 0;
        prediction->progress = 0;
    } else {
        stack = &snapshot->player.inventory.slots[hotbar];
        if (!prediction->targeting || target.x != prediction->target.x || target.y != prediction->target.y || target.z != prediction->target.z || target.state_id != prediction->target.state_id || stack->item_id != prediction->held.item_id || stack->metadata != prediction->held.metadata || stack->damage != prediction->held.damage || pressed) {
            prediction->targeting = 1;
            prediction->target = target;
            prediction->held = *stack;
            prediction->started_tick = snapshot->tick;
            prediction->started_at = prediction->diagnostics ? maage_time_monotonic_seconds() : -1;
            prediction->started_sequence = input_sequence;
            prediction->started_revision = maage_world_column_revision(visual, maage_world_chunk_coordinate(target.x), maage_world_chunk_coordinate(target.z));
            prediction->pressed_start = pressed;
            prediction->started_breaks = snapshot->player.blocks_broken;
            prediction->break_sound_heard = 0;
            prediction->progress = 0;
        }
        block = maage_block_registry_block(blocks, target.block_id);
        tool = stack->count ? maage_item_registry_get(items, stack->item_id) : NULL;
        damage = snapshot->player.game_mode == MAAGE_GAME_MODE_CREATIVE ? 1 : maage_item_mining_damage(tool, block, snapshot->player.player.grounded, snapshot->player.submerged);
        matching = snapshot->tick > prediction->started_tick && snapshot->gameplay_command_sequence >= prediction->started_sequence &&
                   snapshot->player.breaking_active && snapshot->player.breaking.x == target.x && snapshot->player.breaking.y == target.y && snapshot->player.breaking.z == target.z && snapshot->player.breaking.state_id == target.state_id &&
                   snapshot->player.breaking_item == (stack->count ? stack->item_id : 0) && snapshot->player.breaking_metadata == (stack->count ? stack->metadata : 0) && snapshot->player.breaking_damage == (stack->count ? stack->damage : 0);
        completed = snapshot->player.blocks_broken > prediction->started_breaks && snapshot->player.last_break_tick > prediction->started_tick &&
                    snapshot->player.last_break.x == target.x && snapshot->player.last_break.y == target.y && snapshot->player.last_break.z == target.z && snapshot->player.last_break.state_id == target.state_id;
        /* A press speculates only the owner's first damage step. Thereafter
         * rebase from consumed input, target and tool, never elapsed frames.
         * A late snapshot permits at most its next owner step, even on stalls.
         * Held retargeting/cooldown cannot run ahead on a second local clock. */
        progress = prediction->pressed_start && snapshot->tick == prediction->started_tick ? damage : 0;
        prediction->progress = progress;
        if (matching && !snapshot->player.break_delay && !snapshot->player.attack_miss_delay) {
            progress = snapshot->player.break_progress;
            phase = snapshot->next_tick_at >= 0 ? (input_time - snapshot->next_tick_at + MAAGE_PLAYER_TICK_SECONDS) / MAAGE_PLAYER_TICK_SECONDS : 0;
            if (phase < 0) phase = 0;
            if (phase > 1) phase = 1;
            prediction->progress = progress + damage * phase;
            if (phase >= 1) progress += damage;
        }
        /* Instant held breaks still obey the owner's published cooldown. */
        if (!matching && !pressed && damage >= 1 && snapshot->tick > prediction->started_tick && snapshot->gameplay_command_sequence >= prediction->started_sequence && !snapshot->player.break_delay && !snapshot->player.attack_miss_delay && snapshot->player.has_target && snapshot->player.target.x == target.x && snapshot->player.target.y == target.y && snapshot->player.target.z == target.z && snapshot->player.target.state_id == target.state_id && snapshot->next_tick_at >= 0 && input_time >= snapshot->next_tick_at) progress = 1;
        if (completed) progress = prediction->progress = 1;
        if (prediction->progress > 1) prediction->progress = 1;
        if (progress >= 1 - 1e-12 && snapshot->tick >= prediction->completed_tick && vacant < MAAGE_CLIENT_PREDICTIONS && prediction->next_id < 4294967295UL) {
            memset(&prediction->entries[vacant], 0, sizeof(prediction->entries[vacant]));
            prediction->entries[vacant].sound_pending = !prediction->break_sound_heard;
            prediction->entries[vacant].sound_echo = prediction->break_sound_heard;
            prediction->entries[vacant].predicted_at = prediction->diagnostics ? maage_time_monotonic_seconds() : -1;
            prediction->entries[vacant].frame = prediction->frame;
            prediction->entries[vacant].tick = completed ? snapshot->player.last_break_tick : snapshot->tick + 1;
            if (prediction->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "action=%lu input_sequence=%lu input_to_prediction_ms=%.3f predicted_at=%.6f tick=%lu start_tick=%lu mining_started_at=%.6f owner_progress=%.6f damage=%.6f next_tick_at=%.6f", prediction->next_id + 1, prediction->started_sequence, (prediction->entries[vacant].predicted_at - prediction->input_at) * 1000, prediction->entries[vacant].predicted_at, snapshot->tick, prediction->started_tick, prediction->started_at, snapshot->player.break_progress, damage, snapshot->next_tick_at));
            prediction->entries[vacant].active = 1;
            prediction->entries[vacant].id = ++prediction->next_id;
            prediction->entries[vacant].target = target;
            prediction->entries[vacant].replacement = ids->state_air;
            command->kind = MAAGE_CMD_BREAK_PREVIEW;
            command->prediction = prediction->next_id;
            command->prediction_revision = prediction->started_revision;
            command->prediction_command_sequence = prediction->started_sequence;
            command->predicted_target = target;
            prediction->progress = 0;
            prediction->targeting = 0;
            prediction->completed_tick = completed ? snapshot->tick : snapshot->tick + 1;
            prediction->entries[vacant].generation = ++prediction->generation;
        }
    }
    /* Use still travels as ordinary INPUT. The preview command merely asks
     * the Server to acknowledge the independently determined destination. */
    if (!entity_target && !command->kind && active && use && prediction->use_delay <= 0 && snapshot->player.health && !snapshot->player.crafting.kind && vacant < MAAGE_CLIENT_PREDICTIONS && prediction->next_id < 4294967295UL) {
        prediction->use_delay = 0.2;
        result = maage_world_raycast(visual, eye, direction, snapshot->player.reach, &target);
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
            stack = &snapshot->player.inventory.slots[hotbar];
            if ((maage_block_registry_block(maage_world_registry(visual), target.block_id)->use != NULL) && (!sneaking || !stack->count)) result = maage_prediction_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
            else result = maage_gameplay_placement(&snapshot->player.player, visual, items, stack, &target, yaw, &destination, &placed);
            if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
                memset(&prediction->entries[vacant], 0, sizeof(prediction->entries[vacant]));
                prediction->entries[vacant].active = 1;
                prediction->entries[vacant].id = ++prediction->next_id;
                prediction->entries[vacant].target = destination;
                prediction->entries[vacant].replacement = placed;
                prediction->entries[vacant].predicted_at = prediction->diagnostics ? maage_time_monotonic_seconds() : -1;
                prediction->entries[vacant].frame = prediction->frame;
                prediction->entries[vacant].tick = snapshot->tick;
                prediction->entries[vacant].generation = ++prediction->generation;
                command->kind = MAAGE_CMD_PLACE_PREVIEW;
                command->prediction = prediction->next_id;
                command->prediction_revision = maage_world_column_revision(visual, maage_world_chunk_coordinate(destination.x), maage_world_chunk_coordinate(destination.z));
                command->prediction_command_sequence = input_sequence;
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
            if (prediction->entries[i].active && prediction->entries[i].frame == prediction->frame) MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "action=%lu prediction_geometry_ms=%.3f frames=0", prediction->entries[i].id, (maage_time_monotonic_seconds() - prediction->entries[i].predicted_at) * 1000));
    return result;
}
maage_status maage_client_prediction_destroy(maage_client_prediction *prediction) {
    maage_status result;
    if (!prediction) return maage_prediction_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    result = maage_render_mesh_destroy(prediction->reveal);
    memset(prediction, 0, sizeof(*prediction));
    return result;
}

maage_status maage_client_prediction_observe_sound(maage_client_prediction *prediction, const maage_block_registry *blocks, const maage_session_event *event) {
    const maage_registry_bindings *ids = maage_block_registry_bindings(blocks);
    unsigned int i;
    if (event->kind != MAAGE_EVENT_SOUND || event->session_epoch != prediction->session || event->world_epoch != prediction->world || (event->sound.action != MAAGE_SOUND_BLOCK_BREAK && event->sound.action != MAAGE_SOUND_BLOCK_DIG)) return maage_prediction_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active && prediction->entries[i].confirmed >= 0 && prediction->entries[i].replacement == ids->state_air &&
            event->sound.position[0] == prediction->entries[i].target.x + 0.5 && event->sound.position[1] == prediction->entries[i].target.y + 0.5 && event->sound.position[2] == prediction->entries[i].target.z + 0.5) {
            if (event->sound.action == MAAGE_SOUND_BLOCK_BREAK) {
                if (prediction->entries[i].sound_echo) continue;
                prediction->entries[i].sound_echo = 1;
            } else if (prediction->entries[i].sound_echo) continue;
            return maage_prediction_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
        }
    if (prediction->targeting && event->sound.action == MAAGE_SOUND_BLOCK_BREAK &&
        event->sound.position[0] == prediction->target.x + 0.5 && event->sound.position[1] == prediction->target.y + 0.5 && event->sound.position[2] == prediction->target.z + 0.5) prediction->break_sound_heard = 1;
    return maage_prediction_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_UPDATE);
}

maage_status maage_client_prediction_take_break_sound(maage_client_prediction *prediction, const maage_block_registry *blocks, unsigned long tick, maage_sound_event *sound) {
    unsigned int i;
    const maage_block_state_definition *state;
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i) {
        if (!prediction->entries[i].sound_pending) continue;
        if (!prediction->entries[i].active || prediction->entries[i].confirmed < 0) {
            prediction->entries[i].sound_pending = 0;
            continue;
        }
        if (prediction->applied_generation < prediction->entries[i].generation) continue;
        state = maage_block_registry_state(blocks, prediction->entries[i].target.state_id);
        if (!state) return maage_prediction_status(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_READ);
        memset(sound, 0, sizeof(*sound));
        sound->action = MAAGE_SOUND_BLOCK_BREAK;
        sound->material = maage_gameplay_block_material(maage_block_registry_block(blocks, state->block_id));
        {
            maage_status selected = maage_sound_select(maage_block_registry_sounds(blocks), sound->action, sound->material, &sound->event_id);
            if (maage_status_failed(selected)) return selected;
        }
        sound->position[0] = prediction->entries[i].target.x + 0.5;
        sound->position[1] = prediction->entries[i].target.y + 0.5;
        sound->position[2] = prediction->entries[i].target.z + 0.5;
        sound->tick = tick;
        sound->intensity = sound->pitch = 1;
        prediction->entries[i].sound_pending = 0;
        return maage_prediction_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_READ);
    }
    return maage_prediction_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_READ);
}
