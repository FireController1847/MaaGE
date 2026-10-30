#include "camera.h"
#include "player_model.h"
#include <math.h>
#include <string.h>

static const maage_block_shape *camera_shape(const maage_world_camera_sample *sample, const maage_block_registry *blocks, const int cell[3], maage_block_shape *unknown) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    int x, y, z, corner, axis;
    if (sample && sample->valid) {
        x = cell[0] - sample->x + 5;
        y = cell[1] - sample->y + 5;
        z = cell[2] - sample->z + 5;
        corner = (maage_world_chunk_coordinate(cell[0]) != maage_world_chunk_coordinate(sample->x - 5) ? 1 : 0) +
                 (maage_world_chunk_coordinate(cell[2]) != maage_world_chunk_coordinate(sample->z - 5) ? 2 : 0);
        if (x >= 0 && x < 11 && y >= 0 && y < 11 && z >= 0 && z < 11 && sample->revisions[corner]) {
            state = maage_block_registry_state(blocks, sample->states[x + 11 * z + 121 * y]);
            block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
            if (block) return block->solid ? &block->collision_shape : NULL;
        }
    }
    for (axis = 0; axis < 3; ++axis) {
        unknown->minimum[axis] = 0;
        unknown->maximum[axis] = 1;
    }
    return unknown;
}
static double camera_ray(const double origin[3], const double direction[3], double distance, const maage_world_camera_sample *sample, const maage_block_registry *blocks) {
    maage_block_shape unknown;
    const maage_block_shape *shape;
    int cell[3], step[3], axis, next, iteration;
    double crossing[3], interval[3], low, high, a, b, swap;
    for (axis = 0; axis < 3; ++axis) {
        cell[axis] = (int) floor(origin[axis]);
        step[axis] = direction[axis] >= 0 ? 1 : -1;
        interval[axis] = fabs(direction[axis]) > 1e-12 ? fabs(1 / direction[axis]) : 1e30;
        crossing[axis] = fabs(direction[axis]) > 1e-12 ? (cell[axis] + (step[axis] > 0 ? 1 : 0) - origin[axis]) / direction[axis] : 1e30;
    }
    /* A four-block ray visits at most 10 cells; ties can add boundary cells. */
    for (iteration = 0; iteration < 32; ++iteration) {
        shape = camera_shape(sample, blocks, cell, &unknown);
        if (shape) {
            low = 0;
            high = distance;
            for (axis = 0; axis < 3 && low <= high; ++axis) {
                a = cell[axis] + shape->minimum[axis] - origin[axis];
                b = cell[axis] + shape->maximum[axis] - origin[axis];
                if (fabs(direction[axis]) <= 1e-12) {
                    if (a > 0 || b < 0) high = -1;
                } else {
                    a /= direction[axis];
                    b /= direction[axis];
                    if (a > b) {
                        swap = a;
                        a = b;
                        b = swap;
                    }
                    if (a > low) low = a;
                    if (b < high) high = b;
                }
            }
            if (low <= high) return low > 0.03 ? low - 0.03 : 0;
        }
        next = crossing[0] < crossing[1] ? 0 : 1;
        if (crossing[2] < crossing[next]) next = 2;
        if (crossing[next] > distance) break;
        cell[next] += step[next];
        crossing[next] += interval[next];
    }
    return distance;
}
double maage_client_camera_position(unsigned int mode, const double eye[3], double yaw, double pitch, int sneaking, double first_person_eye_drop, double aspect, double fov, const maage_world_camera_sample *sample, const maage_block_registry *blocks, double position[3], double *camera_yaw, double *camera_pitch) {
    double direction[3], origin[3], pivot[3], distance = 4, padding, candidate;
    unsigned int axis, ray;
    memcpy(position, eye, 3 * sizeof(double));
    *camera_yaw = yaw;
    *camera_pitch = pitch;
    if (mode == MAAGE_CAMERA_FIRST) {
        position[1] -= first_person_eye_drop;
        return 0;
    }
    memcpy(pivot, eye, sizeof(pivot));
    if (sneaking) pivot[1] -= 0.2;
    direction[0] = sin(yaw) * cos(pitch);
    direction[1] = -sin(pitch);
    direction[2] = -cos(yaw) * cos(pitch);
    if (mode == MAAGE_CAMERA_BACK)
        for (axis = 0; axis < 3; ++axis)
            direction[axis] = -direction[axis];
    else {
        *camera_yaw += 3.141592653589793;
        *camera_pitch = -pitch;
    }
    /* Eight corner rays use at least +/-0.1 clearance, widened when the near plane
     * at a wide aspect/FOV would otherwise extend beyond that clearance. */
    padding = 0.05 * tan(fov * 0.5) * (aspect > 1 ? aspect : 1) + 0.02;
    if (padding < 0.1) padding = 0.1;
    distance = camera_ray(pivot, direction, distance, sample, blocks);
    for (ray = 0; ray < 8; ++ray) {
        for (axis = 0; axis < 3; ++axis)
            origin[axis] = pivot[axis] + ((ray & (1U << axis)) ? padding : -padding);
        candidate = camera_ray(origin, direction, distance, sample, blocks);
        if (candidate < distance) distance = candidate;
    }
    for (axis = 0; axis < 3; ++axis)
        position[axis] = pivot[axis] + direction[axis] * distance;
    return distance;
}

maage_status maage_client_camera_observe(maage_client_camera_effects *effects, unsigned long session, unsigned long world, unsigned long tick, const maage_session_player_motion *motion) {
    maage_client_camera_step *step, old;
    unsigned long count = 1;
    double speed, pitch;
    int reset;
    if (!effects || !motion || !motion->life) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
    reset = !effects->initialized || effects->session != session || effects->world != world || effects->life != motion->life;
    if (!reset && tick <= effects->tick) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    memset(&old, 0, sizeof(old));
    if (reset) {
        memset(effects, 0, sizeof(*effects));
        effects->session = session;
        effects->world = world;
        effects->life = motion->life;
        effects->initialized = 1;
        effects->hurt_age = effects->death_age = -1;
        old.distance = motion->walk_distance;
    } else {
        old = effects->steps[effects->tick % MAAGE_CLIENT_CAMERA_SAMPLES];
        count = tick - effects->tick;
    }
    speed = sqrt(motion->velocity[0] * motion->velocity[0] + motion->velocity[2] * motion->velocity[2]);
    if (speed > 0.1) speed = 0.1;
    if (!motion->grounded || !motion->alive) speed = 0;
    pitch = motion->grounded || !motion->alive ? 0 : atan(-motion->velocity[1] * 0.2) * 15;
    step = &effects->steps[tick % MAAGE_CLIENT_CAMERA_SAMPLES];
    step->valid = 1;
    step->tick = tick;
    step->distance = motion->walk_distance;
    step->stride = reset ? 0 : (motion->walk_distance - old.distance) / count;
    if (step->stride < 0) step->stride = 0;
    /* Only a missing packet uses the closed-form constant-target rebase. Normal
     * FIFO observations advance exactly once per simulation step, not per frame. */
    step->previous_amount = speed + (old.amount - speed) * pow(0.6, (double) count - 1);
    step->amount = speed + (old.amount - speed) * pow(0.6, (double) count);
    step->previous_pitch = pitch + (old.pitch - pitch) * pow(0.2, (double) count - 1);
    step->pitch = pitch + (old.pitch - pitch) * pow(0.2, (double) count);
    step->previous_eye_drop = old.eye_drop;
    step->eye_drop = motion->sneaking ? 0.08 : old.eye_drop * pow(0.4, (double) count);
    effects->tick = tick;
    return maage_status_make(count > 1 ? MAAGE_STATUS_PARTIAL : MAAGE_STATUS_SUCCESS, count > 1 ? MAAGE_CONTEXT_CLIENT : 0, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_client_camera_effects_update(maage_client_camera_effects *effects, const maage_session_snapshot *snapshot, double interpolation, int paused, int bobbing) {
    maage_client_camera_step *step, fallback;
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
    double remainder, roll, bearing;
    const double pi = 3.141592653589793;
    if (!effects || !snapshot || snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
    if (!effects->initialized || effects->session != snapshot->session_epoch || effects->world != snapshot->world_epoch || effects->life != snapshot->player.motion.life || effects->tick < snapshot->tick) {
        result = maage_client_camera_observe(effects, snapshot->session_epoch, snapshot->world_epoch, snapshot->tick, &snapshot->player.motion);
        if (maage_status_failed(result)) return result;
    }
    step = &effects->steps[snapshot->tick % MAAGE_CLIENT_CAMERA_SAMPLES];
    if (!step->valid || step->tick != snapshot->tick) {
        /* Never rewind observation state when a dropped sample or a stalled
         * reader leaves a gap behind already received future steps. Retain that
         * history and hold the nearest available filter endpoint for this frame. */
        fallback = effects->steps[effects->tick % MAAGE_CLIENT_CAMERA_SAMPLES];
        if (!fallback.valid || fallback.tick != effects->tick) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
        fallback.tick = snapshot->tick;
        fallback.distance = snapshot->player.motion.walk_distance;
        fallback.stride = 0;
        fallback.amount = fallback.previous_amount;
        fallback.pitch = fallback.previous_pitch;
        fallback.eye_drop = fallback.previous_eye_drop;
        if (effects->tick - snapshot->tick < MAAGE_CLIENT_CAMERA_SAMPLES) *step = fallback;
        else step = &fallback;
        result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
    }
    if (interpolation < 0) interpolation = 0;
    if (interpolation > 1) interpolation = 1;
    if (!paused || !effects->rendered) {
        /* The distance phase intentionally leads the completed step, while the
         * amplitude and vertical response interpolate the two tick endpoints. */
        effects->phase = -fmod(fmod(step->distance, 2.0) + step->stride * interpolation, 2.0) * pi;
        effects->amount = step->previous_amount + (step->amount - step->previous_amount) * interpolation;
        effects->pitch = step->previous_pitch + (step->pitch - step->previous_pitch) * interpolation;
        effects->eye_drop = step->previous_eye_drop + (step->eye_drop - step->previous_eye_drop) * interpolation;
        effects->hurt_age = snapshot->player.damage_sequence ? (double) snapshot->tick - snapshot->player.damage_tick + interpolation : -1;
        effects->death_age = snapshot->player.health ? -1 : (double) snapshot->tick - snapshot->player.death_tick + interpolation;
        effects->rendered = 1;
    }
    maage_player_matrix_identity(effects->matrix);
    effects->fov_scale = snapshot->player.submerged ? 60.0 / 70.0 : 1;
    if (effects->death_age >= 0) {
        roll = 40 * effects->death_age / (effects->death_age + 200);
        maage_player_matrix_rotate(effects->matrix, 2, roll * pi / 180);
        effects->fov_scale /= 1 + 2 * effects->death_age / (effects->death_age + 500);
    }
    if (effects->hurt_age >= 0 && effects->hurt_age <= 10) {
        remainder = 1 - effects->hurt_age * 0.1;
        roll = -14 * sin(pi * remainder * remainder * remainder * remainder);
        bearing = snapshot->player.damage_yaw;
        maage_player_matrix_rotate(effects->matrix, 1, -bearing);
        maage_player_matrix_rotate(effects->matrix, 2, roll * pi / 180);
        maage_player_matrix_rotate(effects->matrix, 1, bearing);
    }
    if (bobbing) {
        maage_player_matrix_translate(effects->matrix, sin(effects->phase) * effects->amount * 0.5, -fabs(cos(effects->phase) * effects->amount), 0);
        maage_player_matrix_rotate(effects->matrix, 2, sin(effects->phase) * effects->amount * 3 * pi / 180);
        maage_player_matrix_rotate(effects->matrix, 0, (fabs(cos(effects->phase - 0.2) * effects->amount) * 5 + effects->pitch) * pi / 180);
    }
    return result;
}
