#include "prediction.h"
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
        if (prediction->entries[i].active && prediction->entries[i].sample.x == x && prediction->entries[i].sample.y == y && prediction->entries[i].sample.z == z) return 1;
    return 0;
}
static int maage_prediction_target(const maage_client_prediction *prediction, const maage_session_snapshot *snapshot, const maage_block_registry *blocks, const maage_world_neighborhood *sample, const double eye[3], double yaw, double pitch, maage_world_target *target) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    double direction[3], nearest = snapshot->player.reach, low, high, a, b, temporary;
    int x, y, z, i, axis, side, hit_side = 0, found = 0;
    if (!sample->valid) return 0;
    direction[0] = sin(yaw) * cos(pitch);
    direction[1] = -sin(pitch);
    direction[2] = -cos(yaw) * cos(pitch);
    for (y = -1; y <= 1; ++y)
        for (z = -1; z <= 1; ++z)
            for (x = -1; x <= 1; ++x) {
                if (maage_client_prediction_hides(prediction, sample->x + x, sample->y + y, sample->z + z)) continue;
                i = x + 1 + 3 * (z + 1) + 9 * (y + 1);
                state = maage_block_registry_state(blocks, sample->states[i]);
                block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
                if (!block || !block->selectable) continue;
                low = 0;
                high = nearest;
                for (axis = 0; axis < 3; ++axis) {
                    double coordinate = axis == 0 ? sample->x + x : (axis == 1 ? sample->y + y : sample->z + z);
                    const maage_block_shape *shape = state->selection_shape.maximum[0] > state->selection_shape.minimum[0] ? &state->selection_shape : &block->selection_shape;
                    a = coordinate + shape->minimum[axis] - eye[axis];
                    b = coordinate + shape->maximum[axis] - eye[axis];
                    if (fabs(direction[axis]) < 1e-12) {
                        if (a > 0 || b < 0) break;
                        continue;
                    }
                    a /= direction[axis];
                    b /= direction[axis];
                    side = axis * 2;
                    if (a > b) {
                        temporary = a;
                        a = b;
                        b = temporary;
                        ++side;
                    }
                    if (a > low) {
                        low = a;
                        hit_side = side;
                    }
                    if (b < high) high = b;
                    if (low > high) break;
                }
                if (axis != 3 || low > nearest || high < 0) continue;
                target->x = sample->x + x;
                target->y = sample->y + y;
                target->z = sample->z + z;
                target->state_id = state->id;
                target->block_id = state->block_id;
                target->bounds = state->selection_shape.maximum[0] > state->selection_shape.minimum[0] ? state->selection_shape : block->selection_shape;
                target->face = (maage_block_face) hit_side;
                target->distance = low;
                nearest = low;
                found = 1;
            }
    return found;
}
void maage_client_prediction_confirm(maage_client_prediction *prediction, const maage_session_event *event) {
    unsigned int i;
    if (!prediction || !event || event->kind != MAAGE_EVENT_BLOCK_ACTION || event->session_epoch != prediction->session || event->world_epoch != prediction->world) return;
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active && prediction->entries[i].id == event->prediction) {
            if (event->block.x != prediction->entries[i].sample.x || event->block.y != prediction->entries[i].sample.y || event->block.z != prediction->entries[i].sample.z) return;
            if (!event->accepted) {
                prediction->entries[i].active = 0;
                prediction->rollback_requested = 1;
            } else {
                prediction->entries[i].confirmed = 1;
                memcpy(prediction->entries[i].revisions, event->column_revisions, sizeof(event->column_revisions));
            }
            ++prediction->generation;
            return;
        }
}
static maage_status maage_prediction_rebuild(maage_client_prediction *prediction, maage_renderer *renderer, maage_render_texture *atlas, const maage_block_registry *blocks, const maage_biome_registry *biomes, maage_client_chunk_mesh *meshes, size_t capacity, maage_status_trace *trace) {
    maage_block_position removed[MAAGE_CLIENT_PREDICTIONS];
    maage_mesh_data data, patch;
    maage_render_mesh *replacement = NULL;
    unsigned int *indices;
    unsigned int i, j, count = 0;
    size_t column, faces = 0, index;
    maage_status result;
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active) {
            removed[count].x = prediction->entries[i].sample.x;
            removed[count].y = prediction->entries[i].sample.y;
            removed[count++].z = prediction->entries[i].sample.z;
        }
    if (prediction->dirty || prediction->applied_generation != prediction->generation) {
        for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
            if (prediction->entries[i].active) {
                result = maage_world_mesh_reveal(blocks, biomes, &prediction->entries[i].sample, removed, count, prediction->entries[i].supplied_faces, prediction->leaves, prediction->vertices + faces * 4, prediction->indices + faces * 6, &patch);
                if (maage_status_failed(result)) return result;
                for (index = 0; index < patch.index_count; ++index)
                    prediction->indices[faces * 6 + index] += (unsigned int) (faces * 4);
                faces += patch.index_count / 6;
            }
        if (faces) {
            data.vertices = prediction->vertices;
            data.vertex_count = faces * 4;
            data.indices = prediction->indices;
            data.index_count = faces * 6;
            result = maage_renderer_upload_geometry(renderer, &data, atlas, &replacement, trace);
            if (maage_status_failed(result)) return result;
        }
        maage_render_mesh_destroy(prediction->reveal);
        prediction->reveal = replacement;
    }
    for (column = 0; column < capacity; ++column)
        if (meshes[column].used) {
            j = 0;
            for (i = 0; i < count; ++i)
                if (maage_world_chunk_coordinate(removed[i].x) == meshes[column].x && maage_world_chunk_coordinate(removed[i].z) == meshes[column].z) ++j;
            if (!j) {
                maage_render_mesh_destroy(meshes[column].prediction);
                meshes[column].prediction = NULL;
                meshes[column].filtered = 0;
                continue;
            }
            if (!meshes[column].source || (meshes[column].filtered &&
                                           meshes[column].filtered_revision == meshes[column].visual_revision &&
                                           meshes[column].filtered_generation == prediction->generation)) continue;
            result = maage_world_mesh_filter(meshes[column].source, removed, count, &data, &indices);
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
    prediction->dirty = 0;
    return maage_prediction_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
}
maage_status maage_client_prediction_update(maage_client_prediction *prediction, maage_renderer *renderer, maage_render_texture *atlas, const maage_item_registry *items, const maage_block_registry *blocks, const maage_biome_registry *biomes, maage_world *visual, const maage_session_snapshot *snapshot, maage_client_chunk_mesh *meshes, size_t capacity, const double eye[3], double yaw, double pitch, double elapsed, maage_leaves_mode leaves, int active, int held, int pressed, maage_session_command *command, maage_status_trace *trace) {
    maage_world_neighborhood mining;
    maage_world_target local_target;
    double direction_vector[3];
    maage_block_position positions[MAAGE_CLIENT_PREDICTIONS];
    maage_block_state_id expected[MAAGE_CLIENT_PREDICTIONS];
    size_t prediction_count = 0;
    const maage_item_stack *stack;
    const maage_item_definition *tool;
    const maage_block_definition *block;
    maage_world_target target;
    maage_client_chunk_mesh *column;
    maage_status result;
    double damage;
    static const int neighbors[6][2] = {{-1, 0}, {1, 0}, {0, 0}, {0, 0}, {0, -1}, {0, 1}};
    unsigned int i, index, direction, supplied, vacant = MAAGE_CLIENT_PREDICTIONS;
    int dx, dz, ready, predictable, pending = 0;
    const maage_block_state_definition *neighbor;
    memset(command, 0, sizeof(*command));
    memset(&mining, 0, sizeof(mining));
    direction_vector[0] = sin(yaw) * cos(pitch);
    direction_vector[1] = -sin(pitch);
    direction_vector[2] = -cos(yaw) * cos(pitch);
    result = maage_world_raycast(visual, eye, direction_vector, snapshot->player.reach, &local_target);
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) maage_world_sample_neighborhood(visual, local_target.x, local_target.y, local_target.z, &mining);
    if (prediction->session != snapshot->session_epoch || prediction->world != snapshot->world_epoch) {
        maage_client_prediction_destroy(prediction);
        prediction->session = snapshot->session_epoch;
        prediction->world = snapshot->world_epoch;
        ++prediction->generation;
    }
    if (prediction->leaves != leaves) {
        prediction->leaves = leaves;
        ++prediction->generation;
    }
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i) {
        if (prediction->entries[i].active) {
            supplied = 0;
            for (direction = 0; direction < 6; ++direction) {
                dx = neighbors[direction][0];
                dz = neighbors[direction][1];
                column = maage_prediction_column(meshes, capacity, maage_world_chunk_coordinate(prediction->entries[i].sample.x + dx), maage_world_chunk_coordinate(prediction->entries[i].sample.z + dz));
                if (column && column->presentation_generation >= prediction->entries[i].generation) supplied |= 1U << direction;
            }
            if (supplied != prediction->entries[i].supplied_faces) {
                prediction->entries[i].supplied_faces = supplied;
                prediction->dirty = 1;
            }
        }
        if (prediction->entries[i].active && prediction->entries[i].confirmed) {
            supplied = prediction->entries[i].supplied_faces;
            for (direction = 0; direction < 6; ++direction) {
                dx = neighbors[direction][0];
                dz = neighbors[direction][1];
                column = maage_prediction_column(meshes, capacity, maage_world_chunk_coordinate(prediction->entries[i].sample.x + dx), maage_world_chunk_coordinate(prediction->entries[i].sample.z + dz));
                if (column && column->revision >= prediction->entries[i].revisions[dx + 1 + 3 * (dz + 1)]) supplied |= 1U << direction;
            }
            if (supplied != prediction->entries[i].supplied_faces) {
                prediction->entries[i].supplied_faces = supplied;
                ++prediction->generation;
            }
            ready = 1;
            for (dz = -1; dz <= 1; ++dz)
                for (dx = -1; dx <= 1; ++dx) {
                    index = (unsigned int) (dx + 1 + 3 * (dz + 1));
                    column = maage_prediction_column(meshes, capacity, maage_world_chunk_coordinate(prediction->entries[i].sample.x + dx), maage_world_chunk_coordinate(prediction->entries[i].sample.z + dz));
                    if (column && column->revision < prediction->entries[i].revisions[index]) ready = 0;
                }
            if (ready) {
                prediction->entries[i].active = 0;
                ++prediction->generation;
            }
        }
        if (!prediction->entries[i].active) vacant = i;
        else
            pending = 1;
    }
    if (prediction->suspended) {
        if (pending || !pressed) goto reconcile;
        prediction->suspended = 0;
    }
    if (snapshot->tick != prediction->observed_tick) {
        prediction->observed_tick = snapshot->tick;
        if (!pressed && prediction->delay <= 0 && !prediction->targeting) prediction->delay = (snapshot->player.break_delay > snapshot->player.attack_miss_delay ? snapshot->player.break_delay : snapshot->player.attack_miss_delay) * 0.05;
    }
    if (elapsed > 0.25) elapsed = 0.25;
    if (prediction->delay > 0) prediction->delay -= elapsed;
    if (pressed) prediction->delay = 0;
    if (!active || !held || !snapshot->player.health || !maage_prediction_target(prediction, snapshot, blocks, &mining, eye, yaw, pitch, &target)) {
        prediction->targeting = 0;
        prediction->progress = 0;
    } else {
        stack = maage_inventory_selected(&snapshot->player.inventory);
        if (!prediction->targeting || target.x != prediction->target.x || target.y != prediction->target.y ||
            target.z != prediction->target.z || target.state_id != prediction->target.state_id ||
            stack->item_id != prediction->held.item_id || stack->metadata != prediction->held.metadata ||
            stack->damage != prediction->held.damage || pressed) {
            prediction->targeting = 1;
            prediction->target = target;
            prediction->held = *stack;
            prediction->progress = 0;
        }
        block = maage_block_registry_block(blocks, target.block_id);
        tool = stack->count ? maage_item_registry_get(items, stack->item_id) : NULL;
        damage = snapshot->player.game_mode == MAAGE_GAME_MODE_CREATIVE ? 1 : maage_item_mining_damage(tool, block, snapshot->player.player.grounded, snapshot->player.submerged);
        if (prediction->delay <= 0) {
            if (snapshot->player.breaking_active && snapshot->player.breaking.x == target.x &&
                snapshot->player.breaking.y == target.y && snapshot->player.breaking.z == target.z &&
                snapshot->player.break_progress > prediction->progress) prediction->progress = snapshot->player.break_progress;
            prediction->progress += damage * elapsed / 0.05;
            if (damage >= 1) prediction->progress = 1;
            if (prediction->progress >= 1 && vacant < MAAGE_CLIENT_PREDICTIONS && prediction->next_id < 4294967295UL) {
                column = maage_prediction_column(meshes, capacity, maage_world_chunk_coordinate(target.x), maage_world_chunk_coordinate(target.z));
                predictable = target.x == mining.x && target.y == mining.y && target.z == mining.z;
                if (!block->opaque) predictable = 1; /* No newly occluded faces to reconstruct. */
                if (block->id == MAAGE_BLOCK_LEAVES || block->id == MAAGE_BLOCK_ICE) predictable = 0;
                if (block->opaque)
                    for (index = 0; index < 27; ++index) {
                        neighbor = maage_block_registry_state(blocks, mining.states[index]);
                        if (neighbor && neighbor->fluid_kind) predictable = 0;
                    }
                if (predictable && target.x == mining.x && target.y == mining.y && target.z == mining.z)
                    for (dz = -1; dz <= 1; ++dz)
                        for (dx = -1; dx <= 1; ++dx) {
                            maage_client_chunk_mesh *neighbor_column = maage_prediction_column(meshes, capacity, maage_world_chunk_coordinate(target.x + dx), maage_world_chunk_coordinate(target.z + dz));
                            if (!neighbor_column || neighbor_column->revision != mining.revisions[dx + 1 + 3 * (dz + 1)]) predictable = 0;
                        }
                if (column && column->source && predictable) {
                    prediction->entries[vacant].active = 1;
                    prediction->entries[vacant].confirmed = 0;
                    prediction->entries[vacant].supplied_faces = 0;
                    prediction->entries[vacant].id = ++prediction->next_id;
                    if (target.x == mining.x && target.y == mining.y && target.z == mining.z) prediction->entries[vacant].sample = mining;
                    else {
                        memset(&prediction->entries[vacant].sample, 0, sizeof(prediction->entries[vacant].sample));
                        prediction->entries[vacant].sample.valid = 1;
                        prediction->entries[vacant].sample.x = target.x;
                        prediction->entries[vacant].sample.y = target.y;
                        prediction->entries[vacant].sample.z = target.z;
                        prediction->entries[vacant].sample.states[13] = target.state_id;
                    }
                    command->kind = MAAGE_CMD_BREAK_PREVIEW;
                    command->prediction = prediction->next_id;
                    command->predicted_target = target;
                    prediction->progress = 0;
                    prediction->targeting = 0;
                    prediction->delay = snapshot->player.game_mode == MAAGE_GAME_MODE_CREATIVE || damage < 1 ? 0.25 : 0.05;
                    ++prediction->generation;
                    prediction->entries[vacant].generation = prediction->generation;
                }
            }
        }
    }
reconcile:
    for (i = 0; i < MAAGE_CLIENT_PREDICTIONS; ++i)
        if (prediction->entries[i].active && !prediction->suspended) {
            positions[prediction_count].x = prediction->entries[i].sample.x;
            positions[prediction_count].y = prediction->entries[i].sample.y;
            positions[prediction_count].z = prediction->entries[i].sample.z;
            expected[prediction_count++] = prediction->entries[i].sample.states[13];
        }
    result = maage_world_present_predictions(visual, positions, expected, prediction_count, prediction->generation);
    if (maage_status_failed(result)) return result;
    if (prediction->rollback_requested) {
        size_t column_index;
        for (column_index = 0; column_index < capacity; ++column_index)
            if (meshes[column_index].rollback && meshes[column_index].rollback_revision == maage_world_column_revision(visual, meshes[column_index].x, meshes[column_index].z)) {
                maage_client_chunk_mesh *cached = &meshes[column_index];
                maage_render_mesh_destroy(cached->mesh);
                maage_world_mesh_destroy(cached->source);
                cached->mesh = cached->rollback;
                cached->source = cached->rollback_source;
                cached->rollback = NULL;
                cached->rollback_source = NULL;
                cached->revision = cached->rollback_revision;
                cached->visual_revision = cached->rollback_visual_revision;
                cached->presentation_generation = 0;
                if (cached->source) {
                    const maage_mesh_data *data = maage_world_mesh_data(cached->source);
                    cached->vertices = data->vertex_count;
                    cached->indices = data->index_count;
                }
            }
        prediction->rollback_requested = 0;
        prediction->dirty = 1;
    }
    if (!prediction_count) {
        size_t column_index;
        for (column_index = 0; column_index < capacity; ++column_index) {
            maage_render_mesh_destroy(meshes[column_index].rollback);
            maage_world_mesh_destroy(meshes[column_index].rollback_source);
            meshes[column_index].rollback = NULL;
            meshes[column_index].rollback_source = NULL;
        }
    }
    if (prediction->dirty || prediction->generation != prediction->applied_generation) {
        result = maage_prediction_rebuild(prediction, renderer, atlas, blocks, biomes, meshes, capacity, trace);
        if (maage_status_failed(result)) return result;
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
