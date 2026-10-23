#include "maage/world.h"
#include <math.h>
#include <float.h>
#include <string.h>

/* Slab intersection keeps the traversal independent of full-cube assumptions. */
static int maage_ray_shape(const double origin[3], const double direction[3],
    const int cell[3], const maage_block_shape *shape, double range,
    double *distance, maage_block_face *face)
{
    double near_value, far_value, first, last, minimum, maximum;
    maage_block_face entering;
    int axis;
    near_value = -1.0e100; far_value = range;
    entering = MAAGE_FACE_NEGATIVE_X;
    for (axis = 0; axis < 3; ++axis) {
        minimum = cell[axis] + shape->minimum[axis];
        maximum = cell[axis] + shape->maximum[axis];
        if (direction[axis] == 0.0) {
            if (origin[axis] < minimum || origin[axis] > maximum) return 0;
            continue;
        }
        first = (minimum - origin[axis]) / direction[axis];
        last = (maximum - origin[axis]) / direction[axis];
        if (direction[axis] < 0.0) {
            minimum = first; first = last; last = minimum;
        }
        if (first > near_value) {
            near_value = first;
            entering = (maage_block_face)(axis * 2 + (direction[axis] < 0.0));
        }
        if (last < far_value) far_value = last;
        if (near_value > far_value) return 0;
    }
    if (far_value < 0.0 || near_value > range) return 0;
    *distance = near_value > 0.0 ? near_value : 0.0;
    *face = entering;
    return 1;
}

maage_status maage_world_raycast(const maage_world *world, const double origin[3],
    const double direction[3], double range, maage_world_target *target)
{
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    const maage_block_shape *shape;
    double ray[3], next[3], delta[3], magnitude, scale, travelled, distance;
    int cell[3], step[3], axis, chosen;
    maage_block_state_id state_id;
    maage_block_face face;
    if (target) memset(target, 0, sizeof(*target));
    if (!world || !origin || !direction || !target ||
        !(range > 0.0 && range <= MAAGE_WORLD_LIMIT)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RAYCAST, 0);
    scale = 0.0;
    for (axis = 0; axis < 3; ++axis) {
        if (!(origin[axis] > -MAAGE_WORLD_LIMIT && origin[axis] < MAAGE_WORLD_LIMIT) ||
            !(direction[axis] >= -DBL_MAX && direction[axis] <= DBL_MAX)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RAYCAST, 0);
        if (fabs(direction[axis]) > scale) scale = fabs(direction[axis]);
    }
    if (!(scale > 0.0)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RAYCAST, 0);
    magnitude = 0.0;
    for (axis = 0; axis < 3; ++axis) {
        ray[axis] = direction[axis] / scale;
        magnitude += ray[axis] * ray[axis];
    }
    magnitude = sqrt(magnitude);
    for (axis = 0; axis < 3; ++axis) {
        ray[axis] /= magnitude;
        cell[axis] = (int)floor(origin[axis]);
        step[axis] = ray[axis] > 0.0 ? 1 : -1;
        if (ray[axis] == 0.0) next[axis] = delta[axis] = 1.0e100;
        else {
            delta[axis] = fabs(1.0 / ray[axis]);
            next[axis] = ((ray[axis] > 0.0 ? cell[axis] + 1.0 : cell[axis]) -
                origin[axis]) / ray[axis];
        }
    }
    travelled = 0.0;
    while (travelled <= range) {
        if (cell[0] <= -MAAGE_WORLD_LIMIT || cell[0] >= MAAGE_WORLD_LIMIT ||
            cell[2] <= -MAAGE_WORLD_LIMIT || cell[2] >= MAAGE_WORLD_LIMIT) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RAYCAST, 0);
        if (cell[1] >= 0 && cell[1] < MAAGE_WORLD_HEIGHT) {
            if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate(cell[0]),
                maage_world_chunk_coordinate(cell[2]))) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RAYCAST, 0);
            state_id = maage_world_get(world, cell[0], cell[1], cell[2]);
            state = maage_block_registry_state(maage_world_registry(world), state_id);
            block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
            if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RAYCAST, 0);
            shape = state->selection_shape.maximum[0] != 0.0 ?
                &state->selection_shape : &block->selection_shape;
            if (block->selectable && maage_ray_shape(origin, ray, cell,
                shape, range, &distance, &face)) {
                target->x = cell[0]; target->y = cell[1]; target->z = cell[2];
                target->state_id = state_id; target->block_id = state->block_id;
                target->face = face; target->distance = distance;
                target->bounds = *shape;
                return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RAYCAST, 0);
            }
        }
        chosen = 0;
        for (axis = 1; axis < 3; ++axis)
            if (next[axis] < next[chosen]) chosen = axis;
        travelled = next[chosen]; next[chosen] += delta[chosen];
        cell[chosen] += step[chosen];
    }
    return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RAYCAST, 0);
}
