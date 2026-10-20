#include <maage/player.h>

#include <math.h>
#include <string.h>

#define MAAGE_PLAYER_COLLISION_EPSILON 0.0000001

static int maage_player_plane_solid(const maage_world *world,
    const double minimum[3], const double maximum[3], int axis, int cell,
    double displacement, double *allowed)
{
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
        low[index] = (int)floor(minimum[index] + MAAGE_PLAYER_COLLISION_EPSILON);
        high[index] = (int)floor(maximum[index] - MAAGE_PLAYER_COLLISION_EPSILON);
    }
    low[axis] = high[axis] = cell;
    for (z = low[2]; z <= high[2]; ++z) {
        for (y = low[1]; y <= high[1]; ++y) {
            for (x = low[0]; x <= high[0]; ++x) {
                if (!maage_world_collision_shape(world, x, y, z, &shape)) continue;
                coordinate[0] = x; coordinate[1] = y; coordinate[2] = z;
                intersects = 1;
                for (transverse = 0; transverse < 3; ++transverse) {
                    if (transverse == axis) continue;
                    if (maximum[transverse] <= coordinate[transverse] + shape.minimum[transverse] + MAAGE_PLAYER_COLLISION_EPSILON ||
                        minimum[transverse] >= coordinate[transverse] + shape.maximum[transverse] - MAAGE_PLAYER_COLLISION_EPSILON)
                        intersects = 0;
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

static int maage_player_sweep(maage_player *player, const maage_world *world,
    int axis, double displacement)
{
    double minimum[3];
    double maximum[3];
    double allowed;
    int cell;
    int last;
    int collided;
    minimum[0] = player->position[0] - MAAGE_PLAYER_WIDTH * 0.5;
    maximum[0] = player->position[0] + MAAGE_PLAYER_WIDTH * 0.5;
    minimum[1] = player->position[1];
    maximum[1] = player->position[1] + MAAGE_PLAYER_HEIGHT;
    minimum[2] = player->position[2] - MAAGE_PLAYER_WIDTH * 0.5;
    maximum[2] = player->position[2] + MAAGE_PLAYER_WIDTH * 0.5;
    allowed = displacement;
    collided = 0;
    if (displacement > 0.0) {
        cell = (int)floor(maximum[axis] - MAAGE_PLAYER_COLLISION_EPSILON);
        last = (int)floor(maximum[axis] + displacement -
            MAAGE_PLAYER_COLLISION_EPSILON);
        for (; cell <= last; ++cell) {
            if (maage_player_plane_solid(world, minimum, maximum, axis, cell,
                displacement, &allowed)) {
                collided = 1;
                break;
            }
        }
    } else if (displacement < 0.0) {
        cell = (int)floor(minimum[axis] + MAAGE_PLAYER_COLLISION_EPSILON);
        last = (int)floor(minimum[axis] + displacement +
            MAAGE_PLAYER_COLLISION_EPSILON);
        for (; cell >= last; --cell) {
            if (maage_player_plane_solid(world, minimum, maximum, axis, cell,
                displacement, &allowed)) {
                collided = 1;
                break;
            }
        }
    }
    player->position[axis] += allowed;
    if (collided) player->velocity[axis] = 0.0;
    return collided;
}

void maage_player_spawn(maage_player *player, const maage_world *world)
{
    if (player == NULL) return;
    memset(player, 0, sizeof(*player));
    maage_world_spawn(world, player->position);
}

void maage_player_tick(maage_player *player, const maage_world *world,
    const maage_player_input *input)
{
    double forward;
    double strafe;
    double magnitude;
    double acceleration;
    double drag;
    double sine;
    double cosine;
    double vertical;
    int hit_vertical;
    if (player == NULL || world == NULL || input == NULL) return;
    forward = input->forward;
    strafe = input->strafe;
    if (forward > 1.0) forward = 1.0;
    if (forward < -1.0) forward = -1.0;
    if (strafe > 1.0) strafe = 1.0;
    if (strafe < -1.0) strafe = -1.0;
    magnitude = sqrt(forward * forward + strafe * strafe);
    if (magnitude > 1.0) {
        forward /= magnitude;
        strafe /= magnitude;
    }
    forward *= MAAGE_PLAYER_INPUT_DAMPING;
    strafe *= MAAGE_PLAYER_INPUT_DAMPING;
    acceleration = player->grounded ? MAAGE_PLAYER_GROUND_ACCELERATION :
        MAAGE_PLAYER_AIR_ACCELERATION;
    if (input->flying) acceleration = 0.2;
    sine = sin(input->yaw);
    cosine = cos(input->yaw);
    player->velocity[0] += acceleration * (forward * sine + strafe * cosine);
    player->velocity[2] += acceleration * (-forward * cosine + strafe * sine);
    if (input->jump && player->grounded)
        player->velocity[1] = MAAGE_PLAYER_JUMP_PER_TICK;
    if (input->flying) player->velocity[1] = ((input->jump ? 1 : 0) -
        (input->descend ? 1 : 0)) * 0.3;
    vertical = player->velocity[1];
    hit_vertical = maage_player_sweep(player, world, 1, vertical);
    player->grounded = hit_vertical && vertical < 0.0;
    maage_player_sweep(player, world, 0, player->velocity[0]);
    maage_player_sweep(player, world, 2, player->velocity[2]);
    player->velocity[1] = (player->velocity[1] - MAAGE_PLAYER_GRAVITY_PER_TICK) *
        MAAGE_PLAYER_VERTICAL_DRAG;
    if (input->flying) player->velocity[1] = 0;
    drag = player->grounded ? MAAGE_PLAYER_GROUND_DRAG : MAAGE_PLAYER_AIR_DRAG;
    if (input->flying) drag = 0.6;
    player->velocity[0] *= drag;
    player->velocity[2] *= drag;
}
