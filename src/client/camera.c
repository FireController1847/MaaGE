#include "camera.h"
#include <math.h>
#include <string.h>

static const maage_block_shape *camera_shape(const maage_world_camera_sample *sample,
    const maage_block_registry *blocks, const int cell[3], maage_block_shape *unknown)
{
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    int x, y, z, corner, axis;
    if (sample && sample->valid) {
        x = cell[0]-sample->x+5; y = cell[1]-sample->y+5; z = cell[2]-sample->z+5;
        corner = (maage_world_chunk_coordinate(cell[0]) != maage_world_chunk_coordinate(sample->x-5) ? 1 : 0) +
            (maage_world_chunk_coordinate(cell[2]) != maage_world_chunk_coordinate(sample->z-5) ? 2 : 0);
        if (x >= 0 && x < 11 && y >= 0 && y < 11 && z >= 0 && z < 11 && sample->revisions[corner]) {
            state = maage_block_registry_state(blocks, sample->states[x+11*z+121*y]);
            block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
            if (block) return block->solid ? &block->collision_shape : NULL;
        }
    }
    for (axis = 0; axis < 3; ++axis) { unknown->minimum[axis] = 0; unknown->maximum[axis] = 1; }
    return unknown;
}
static double camera_ray(const double origin[3], const double direction[3], double distance,
    const maage_world_camera_sample *sample, const maage_block_registry *blocks)
{
    maage_block_shape unknown;
    const maage_block_shape *shape;
    int cell[3], step[3], axis, next, iteration;
    double crossing[3], interval[3], low, high, a, b, swap;
    for (axis = 0; axis < 3; ++axis) {
        cell[axis] = (int)floor(origin[axis]); step[axis] = direction[axis] >= 0 ? 1 : -1;
        interval[axis] = fabs(direction[axis]) > 1e-12 ? fabs(1/direction[axis]) : 1e30;
        crossing[axis] = fabs(direction[axis]) > 1e-12 ?
            (cell[axis]+(step[axis]>0 ? 1 : 0)-origin[axis])/direction[axis] : 1e30;
    }
    /* A four-block ray visits at most 10 cells; ties can add boundary cells. */
    for (iteration = 0; iteration < 32; ++iteration) {
        shape = camera_shape(sample, blocks, cell, &unknown);
        if (shape) {
            low = 0; high = distance;
            for (axis = 0; axis < 3 && low <= high; ++axis) {
                a = cell[axis]+shape->minimum[axis]-origin[axis];
                b = cell[axis]+shape->maximum[axis]-origin[axis];
                if (fabs(direction[axis]) <= 1e-12) {
                    if (a > 0 || b < 0) high = -1;
                } else {
                    a /= direction[axis]; b /= direction[axis];
                    if (a > b) { swap = a; a = b; b = swap; }
                    if (a > low) low = a;
                    if (b < high) high = b;
                }
            }
            if (low <= high) return low > 0.03 ? low-0.03 : 0;
        }
        next = crossing[0] < crossing[1] ? 0 : 1;
        if (crossing[2] < crossing[next]) next = 2;
        if (crossing[next] > distance) break;
        cell[next] += step[next]; crossing[next] += interval[next];
    }
    return distance;
}
double maage_client_camera_position(unsigned int mode, const double eye[3],
    double yaw, double pitch, int sneaking, double aspect, double fov,
    const maage_world_camera_sample *sample, const maage_block_registry *blocks,
    double position[3], double *camera_yaw, double *camera_pitch)
{
    double direction[3], origin[3], pivot[3], distance = 4, padding, candidate;
    unsigned int axis, ray;
    memcpy(position, eye, 3*sizeof(double));
    *camera_yaw = yaw; *camera_pitch = pitch;
    if (mode == MAAGE_CAMERA_FIRST) return 0;
    memcpy(pivot, eye, sizeof(pivot));
    if (sneaking) pivot[1] -= 0.2;
    direction[0] = sin(yaw)*cos(pitch); direction[1] = -sin(pitch); direction[2] = -cos(yaw)*cos(pitch);
    if (mode == MAAGE_CAMERA_BACK) for (axis = 0; axis < 3; ++axis) direction[axis] = -direction[axis];
    else { *camera_yaw += 3.141592653589793; *camera_pitch = -pitch; }
    /* The reference's eight +/-0.1 rays, widened when the actual near plane
     * at a wide aspect/FOV would otherwise extend beyond that clearance. */
    padding = 0.05*tan(fov*0.5)*(aspect > 1 ? aspect : 1)+0.02;
    if (padding < 0.1) padding = 0.1;
    distance = camera_ray(pivot, direction, distance, sample, blocks);
    for (ray = 0; ray < 8; ++ray) {
        for (axis = 0; axis < 3; ++axis) origin[axis] = pivot[axis]+((ray & (1U<<axis)) ? padding : -padding);
        candidate = camera_ray(origin, direction, distance, sample, blocks);
        if (candidate < distance) distance = candidate;
    }
    for (axis = 0; axis < 3; ++axis) position[axis] = pivot[axis]+direction[axis]*distance;
    return distance;
}
