#include <maage/player.h>
#include "maage/entities.h"

#include <math.h>
#include <string.h>

#define MAAGE_PLAYER_COLLISION_EPSILON 0.0000001

static int maage_player_plane_solid(const maage_world *world, const double minimum[3], const double maximum[3], int axis, int cell, double displacement, double *allowed) {
    int low[3];
    int high[3];
    int index;
    int x;
    int y;
    int z;
    int transverse, coordinate[3], intersects, collided = 0;
    double face, distance;
    maage_block_shape shape;
    for (index = 0; index < 3; ++index) {
        low[index] = (int) floor(minimum[index] + MAAGE_PLAYER_COLLISION_EPSILON);
        high[index] = (int) floor(maximum[index] - MAAGE_PLAYER_COLLISION_EPSILON);
    }
    low[axis] = high[axis] = cell;
    for (z = low[2]; z <= high[2]; ++z) {
        for (y = low[1]; y <= high[1]; ++y) {
            for (x = low[0]; x <= high[0]; ++x) {
                if (!maage_world_collision_shape(world, x, y, z, &shape)) continue;
                coordinate[0] = x;
                coordinate[1] = y;
                coordinate[2] = z;
                intersects = 1;
                for (transverse = 0; transverse < 3; ++transverse) {
                    if (transverse == axis) continue;
                    if (maximum[transverse] <= coordinate[transverse] + shape.minimum[transverse] + MAAGE_PLAYER_COLLISION_EPSILON || minimum[transverse] >= coordinate[transverse] + shape.maximum[transverse] - MAAGE_PLAYER_COLLISION_EPSILON) intersects = 0;
                }
                if (!intersects) continue;
                if (displacement > 0) {
                    face = cell + shape.minimum[axis];
                    distance = face - maximum[axis];
                    if (distance < -MAAGE_PLAYER_COLLISION_EPSILON || distance > *allowed) continue;
                    *allowed = distance < 0 ? 0 : distance;
                } else {
                    face = cell + shape.maximum[axis];
                    distance = face - minimum[axis];
                    if (distance > MAAGE_PLAYER_COLLISION_EPSILON || distance < *allowed) continue;
                    *allowed = distance > 0 ? 0 : distance;
                }
                collided = 1;
            }
        }
    }
    return collided;
}

int maage_world_sweep_body(const maage_world *world, double position[3], double velocity[3], double width, double height, int axis, double displacement) {
    double minimum[3];
    double maximum[3];
    double allowed;
    int cell;
    int last;
    int collided;
    minimum[0] = position[0] - width * 0.5;
    maximum[0] = position[0] + width * 0.5;
    minimum[1] = position[1];
    maximum[1] = position[1] + height;
    minimum[2] = position[2] - width * 0.5;
    maximum[2] = position[2] + width * 0.5;
    allowed = displacement;
    collided = 0;
    if (displacement > 0.0) {
        cell = (int) floor(maximum[axis] - MAAGE_PLAYER_COLLISION_EPSILON);
        last = (int) floor(maximum[axis] + displacement - MAAGE_PLAYER_COLLISION_EPSILON);
        for (; cell <= last; ++cell) {
            if (maage_player_plane_solid(world, minimum, maximum, axis, cell, displacement, &allowed)) {
                collided = 1;
                break;
            }
        }
    } else if (displacement < 0.0) {
        cell = (int) floor(minimum[axis] + MAAGE_PLAYER_COLLISION_EPSILON);
        last = (int) floor(minimum[axis] + displacement + MAAGE_PLAYER_COLLISION_EPSILON);
        for (; cell >= last; --cell) {
            if (maage_player_plane_solid(world, minimum, maximum, axis, cell, displacement, &allowed)) {
                collided = 1;
                break;
            }
        }
    }
    position[axis] += allowed;
    if (collided) velocity[axis] = 0.0;
    return collided;
}

unsigned int maage_player_fluid(const maage_player *player, const maage_world *world) {
    const maage_block_state_definition *state;
    const maage_block_registry *registry;
    int low[3], high[3], x, y, z;
    unsigned int axis, found = MAAGE_FLUID_NONE;
    if (!player || !world) return MAAGE_FLUID_NONE;
    registry = maage_world_registry(world);
    for (axis = 0; axis < 3; ++axis) {
        low[axis] = (int) floor(player->position[axis] - MAAGE_PLAYER_WIDTH * 0.5 + 0.001);
        high[axis] = (int) floor(player->position[axis] + MAAGE_PLAYER_WIDTH * 0.5 - 0.001);
    }
    low[1] = (int) floor(player->position[1] + 0.401);
    high[1] = (int) floor(player->position[1] + MAAGE_PLAYER_HEIGHT - 0.401);
    for (z = low[2]; z <= high[2]; ++z)
        for (x = low[0]; x <= high[0]; ++x)
            if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) return MAAGE_FLUID_NONE;
    for (z = low[2]; z <= high[2]; ++z)
        for (x = low[0]; x <= high[0]; ++x)
            for (y = low[1]; y <= high[1]; ++y) {
                state = maage_block_registry_state(registry, maage_world_get(world, x, y, z));
                if (!state) continue;
                if (state->fluid_kind == MAAGE_FLUID_WATER) return MAAGE_FLUID_WATER;
                if (state->fluid_kind == MAAGE_FLUID_LAVA) found = MAAGE_FLUID_LAVA;
            }
    return found;
}

maage_status maage_player_spawn(maage_player *player, const maage_world *world) {
    if (player == NULL || world == NULL) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
    memset(player, 0, sizeof(*player));
    maage_world_spawn(world, player->position);
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
}

static int maage_player_supported(const maage_player *player, const maage_world *world, double x_offset, double z_offset) {
    maage_block_shape shape;
    double low[3], high[3];
    int first[3], last[3], cell[3], axis, intersects;
    low[0] = player->position[0] + x_offset - MAAGE_PLAYER_WIDTH * 0.5;
    low[1] = player->position[1] - 1.0;
    low[2] = player->position[2] + z_offset - MAAGE_PLAYER_WIDTH * 0.5;
    high[0] = low[0] + MAAGE_PLAYER_WIDTH;
    high[1] = low[1] + MAAGE_PLAYER_HEIGHT;
    high[2] = low[2] + MAAGE_PLAYER_WIDTH;
    for (axis = 0; axis < 3; ++axis) {
        first[axis] = (int) floor(low[axis] + MAAGE_PLAYER_COLLISION_EPSILON);
        last[axis] = (int) floor(high[axis] - MAAGE_PLAYER_COLLISION_EPSILON);
    }
    for (cell[2] = first[2]; cell[2] <= last[2]; ++cell[2])
        for (cell[1] = first[1]; cell[1] <= last[1]; ++cell[1])
            for (cell[0] = first[0]; cell[0] <= last[0]; ++cell[0]) {
                if (!maage_world_collision_shape(world, cell[0], cell[1], cell[2], &shape)) continue;
                intersects = 1;
                for (axis = 0; axis < 3; ++axis)
                    if (high[axis] <= cell[axis] + shape.minimum[axis] + MAAGE_PLAYER_COLLISION_EPSILON || low[axis] >= cell[axis] + shape.maximum[axis] - MAAGE_PLAYER_COLLISION_EPSILON) intersects = 0;
                if (intersects) return 1;
            }
    return 0;
}
static double maage_player_edge_step(double displacement) {
    if (fabs(displacement) <= 0.05) return 0;
    return displacement + (displacement > 0 ? -0.05 : 0.05);
}

maage_status maage_player_tick(maage_player *player, const maage_world *world, const maage_player_input *input) {
    double forward;
    double strafe;
    double magnitude;
    double acceleration;
    double drag;
    double sine;
    double cosine;
    double vertical;
    double horizontal[2];
    int hit_vertical;
    unsigned int fluid;
    if (player == NULL || world == NULL || input == NULL) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    forward = input->forward;
    strafe = input->strafe;
    if (forward > 1.0) forward = 1.0;
    if (forward < -1.0) forward = -1.0;
    if (strafe > 1.0) strafe = 1.0;
    if (strafe < -1.0) strafe = -1.0;
    if (input->descend) {
        forward *= MAAGE_PLAYER_SNEAK_INPUT;
        strafe *= MAAGE_PLAYER_SNEAK_INPUT;
    }
    forward *= MAAGE_PLAYER_INPUT_DAMPING;
    strafe *= MAAGE_PLAYER_INPUT_DAMPING;
    magnitude = sqrt(forward * forward + strafe * strafe);
    if (magnitude > 1.0) {
        forward /= magnitude;
        strafe /= magnitude;
    }
    acceleration = player->grounded ? MAAGE_PLAYER_GROUND_ACCELERATION : MAAGE_PLAYER_AIR_ACCELERATION;
    /* Contact at the start of the step determines retained momentum, including
     * the takeoff and landing steps. The sweep publishes next step's contact. */
    drag = player->grounded ? MAAGE_PLAYER_GROUND_DRAG : MAAGE_PLAYER_AIR_DRAG;
    fluid = input->flying ? MAAGE_FLUID_NONE : maage_player_fluid(player, world);
    if (input->flying) {
        acceleration = 0.2;
        drag = 0.6;
    } else if (player->sprinting && fluid == MAAGE_FLUID_NONE) acceleration *= MAAGE_PLAYER_SPRINT_SPEED;
    sine = sin(input->yaw);
    cosine = cos(input->yaw);
    player->velocity[0] += acceleration * (forward * sine + strafe * cosine);
    player->velocity[2] += acceleration * (-forward * cosine + strafe * sine);
    if (input->jump && player->grounded && !input->flying) {
        player->velocity[1] = MAAGE_PLAYER_JUMP_PER_TICK;
        if (player->sprinting && fluid == MAAGE_FLUID_NONE) {
            player->velocity[0] += sine * MAAGE_PLAYER_SPRINT_JUMP;
            player->velocity[2] -= cosine * MAAGE_PLAYER_SPRINT_JUMP;
        }
    }
    if (input->flying) player->velocity[1] = ((input->jump ? 1 : 0) - (input->descend ? 1 : 0)) * 0.3;
    vertical = player->velocity[1];
    horizontal[0] = player->velocity[0];
    horizontal[1] = player->velocity[2];
    if (player->grounded && input->descend && !input->flying && vertical <= 0) {
        /* Clip the requested walk against the projected body, independently
         * along each axis and then at the combined corner. This permits body
         * overhang, retains real collision responses and never changes gravity
         * or an upward jump/knockback impulse. Unknown terrain stays solid. */
        while (horizontal[0] != 0 && !maage_player_supported(player, world, horizontal[0], 0))
            horizontal[0] = maage_player_edge_step(horizontal[0]);
        while (horizontal[1] != 0 && !maage_player_supported(player, world, 0, horizontal[1]))
            horizontal[1] = maage_player_edge_step(horizontal[1]);
        while (horizontal[0] != 0 && horizontal[1] != 0 && !maage_player_supported(player, world, horizontal[0], horizontal[1])) {
            horizontal[0] = maage_player_edge_step(horizontal[0]);
            horizontal[1] = maage_player_edge_step(horizontal[1]);
        }
    }
    hit_vertical = maage_world_sweep_body(world, player->position, player->velocity, MAAGE_PLAYER_WIDTH, MAAGE_PLAYER_HEIGHT, 1, vertical);
    player->grounded = hit_vertical && vertical < 0.0;
    player->collided_horizontally = maage_world_sweep_body(world, player->position, player->velocity, MAAGE_PLAYER_WIDTH, MAAGE_PLAYER_HEIGHT, 0, horizontal[0]);
    if (maage_world_sweep_body(world, player->position, player->velocity, MAAGE_PLAYER_WIDTH, MAAGE_PLAYER_HEIGHT, 2, horizontal[1])) player->collided_horizontally = 1;
    if (player->collided_horizontally) player->sprinting = 0;
    player->velocity[1] = (player->velocity[1] - MAAGE_PLAYER_GRAVITY_PER_TICK) * MAAGE_PLAYER_VERTICAL_DRAG;
    if (input->flying) player->velocity[1] = 0;
    player->velocity[0] *= drag;
    player->velocity[2] *= drag;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
}
