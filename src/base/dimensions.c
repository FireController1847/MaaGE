#include "base_internal.h"
#include "maage/jobs.h"
#include "maage/creative.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* Built-in content policy; the World/Server never interpret these values. */
enum {
    DIM_END_STONE,
    DIM_SOUL_SAND,
    DIM_GLOWSTONE,
    DIM_PORTAL,
    DIM_END_PORTAL,
    DIM_FRAME
};
enum {
    TRAVEL_NETHER = 1,
    TRAVEL_END,
    TRAVEL_RETURN
};
static maage_status dimension_result(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
}
static int dimension_loaded(const maage_world *world, int x, int z) {
    return maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
}
static double dimension_noise(maage_world_seed seed, double x, double y, double z, unsigned long salt) {
    int ix = (int) floor(x), iy = (int) floor(y), iz = (int) floor(z), dx, dy, dz;
    double u = x - ix, v = y - iy, w = z - iz, value = 0, weight;
    unsigned long hash;
    u = u * u * (3 - 2 * u);
    v = v * v * (3 - 2 * v);
    w = w * w * (3 - 2 * w);
    for (dz = 0; dz < 2; ++dz)
        for (dy = 0; dy < 2; ++dy)
            for (dx = 0; dx < 2; ++dx) {
                hash = maage_generation_feature_hash(seed, ix + dx, iz + dz, salt ^ ((unsigned long) (iy + dy) * 2654435761UL));
                weight = (dx ? u : 1 - u) * (dy ? v : 1 - v) * (dz ? w : 1 - w);
                value += weight * ((hash & 65535UL) / 32767.5 - 1);
            }
    return value;
}
static maage_status dimension_set(maage_chunk *chunk, int x, int y, int z, maage_block_state_id state) {
    if (maage_world_chunk_coordinate(x) != maage_chunk_x(chunk) || maage_world_chunk_coordinate(z) != maage_chunk_z(chunk)) return dimension_result(MAAGE_STATUS_NOOP);
    return maage_chunk_set(chunk, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z), state);
}
/* Three deterministic portal rooms around the origin, independently clipped
 * into detached columns. No neighboring generation or seed parity is required. */
static maage_status dimension_strongholds(const maage_base_ids *ids, maage_chunk *chunk, maage_world_seed seed) {
    int i, cx, cz, x, y, z, dx, dz, facing, changed = 0;
    maage_generation_output output;
    double angle = (maage_generation_feature_hash(seed, 0, 0, 71001) % 62832UL) / 10000.0;
    maage_block_state_id state;
    maage_status result;
    for (i = 0; i < 3; ++i) {
        cx = (int) floor(cos(angle + i * 2.094395102393195) * 1024);
        cz = (int) floor(sin(angle + i * 2.094395102393195) * 1024);
        if (abs(maage_chunk_x(chunk) * 16 - cx) > 24 || abs(maage_chunk_z(chunk) * 16 - cz) > 24) continue;
        changed = 1;
        for (z = cz - 5; z <= cz + 5; ++z)
            for (x = cx - 5; x <= cx + 5; ++x)
                for (y = 28; y <= 34; ++y) {
                    dx = x - cx;
                    dz = z - cz;
                    state = y == 28 || y == 34 || abs(dx) == 5 || abs(dz) == 5 ? ids->state_cobblestone : ids->state_air;
                    if (y == 28 && abs(dx) <= 1 && abs(dz) <= 1) state = ids->state_lava;
                    if (y == 29 && ((abs(dx) == 2 && abs(dz) <= 1) || (abs(dz) == 2 && abs(dx) <= 1))) {
                        facing = dz == -2 ? 2 : (dx == 2 ? 3 : (dz == 2 ? 0 : 1));
                        state = ids->frame_states[0][facing];
                    }
                    result = dimension_set(chunk, x, y, z, state);
                    if (maage_status_failed(result)) return result;
                }
    }
    if (changed) {
        result = maage_chunk_generation_output(chunk, &output);
        if (maage_status_failed(result)) return result;
        for (z = 0; z < 16; ++z)
            for (x = 0; x < 16; ++x) {
                y = 256;
                while (y > 0 && maage_chunk_get(chunk, x, y - 1, z) == ids->state_air)
                    --y;
                output.heightmap[z * 16 + x] = (unsigned short) y;
            }
    }
    return dimension_result(MAAGE_STATUS_SUCCESS);
}
static maage_status dimension_overworld_generate(const void *context, maage_chunk *chunk, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, const maage_job_token *token, maage_status_trace *trace) {
    maage_status result = maage_base_generate(chunk, seed, blocks, biomes, policy, token, trace);
    if (maage_status_failed(result)) return result;
    return dimension_strongholds((const maage_base_ids *) context, chunk, seed);
}
static maage_status dimension_overworld_spawn(const void *context, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, double position[3]) {
    (void) context;
    return maage_base_spawn(seed, blocks, biomes, policy, position);
}
typedef struct dimension_nether_profile {
    int floor, ceiling, bridge;
} dimension_nether_profile;
/* The cavern's lower and upper envelopes always remain attached to their
 * bedrock slabs. A shared pillar field joins them, rather than thresholding
 * isolated 3D noise lobes into unsupported netherrack islands. */
static dimension_nether_profile dimension_nether_column(maage_world_seed seed, int x, int z) {
    dimension_nether_profile profile;
    double pillar;
    int region_x, region_z, fortress_x, fortress_z;
    pillar = dimension_noise(seed, x / 32.0, 0, z / 32.0, 72003) + 0.3 * dimension_noise(seed, x / 11.0, 0, z / 11.0, 72005) - 0.15;
    if (pillar < 0) pillar = 0;
    profile.floor = 26 + (int) (dimension_noise(seed, x / 90.0, 0, z / 90.0, 72001) * 28 + dimension_noise(seed, x / 23.0, 0, z / 23.0, 72006) * 8 + pillar * 130);
    profile.ceiling = 96 + (int) (dimension_noise(seed, x / 70.0, 0, z / 70.0, 72002) * 20 + dimension_noise(seed, x / 19.0, 0, z / 19.0, 72007) * 6 - pillar * 104);
    if (profile.floor < 6) profile.floor = 6;
    if (profile.floor > 120) profile.floor = 120;
    if (profile.ceiling < 8) profile.ceiling = 8;
    if (profile.ceiling > 122) profile.ceiling = 122;
    region_x = maage_generation_region(x, 512);
    region_z = maage_generation_region(z, 512);
    fortress_x = region_x * 512 + 128 + (int) (maage_generation_feature_hash(seed, region_x, region_z, 72010) % 256UL);
    fortress_z = region_z * 512 + 128 + (int) (maage_generation_feature_hash(seed, region_x, region_z, 72011) % 256UL);
    profile.bridge = (abs(x - fortress_x) <= 3 && abs(z - fortress_z) < 60) || (abs(z - fortress_z) <= 3 && abs(x - fortress_x) < 60);
    return profile;
}
static int dimension_nether_air(const dimension_nether_profile *profile, int y) {
    if (profile->bridge && y >= 60 && y <= 65) return y >= 62;
    return y >= 32 && y > profile->floor && y < profile->ceiling;
}
/* Replay bounded features from neighboring origin columns. Their RNG and
 * geometry never depend on resident neighbors or generation job order. */
static maage_status dimension_nether_quartz(const maage_base_ids *ids, maage_chunk *chunk, maage_world_seed seed, const maage_job_token *token) {
    static const int directions[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    int cx, cz, attempt, step, x, y, z, direction;
    unsigned long random;
    maage_status result;
    for (cz = maage_chunk_z(chunk) - 1; cz <= maage_chunk_z(chunk) + 1; ++cz)
        for (cx = maage_chunk_x(chunk) - 1; cx <= maage_chunk_x(chunk) + 1; ++cx) {
            if (maage_job_cancelled(token)) return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, MAAGE_DETAIL_CANCELLED);
            random = maage_generation_feature_hash(seed, cx, cz, 72004);
            for (attempt = 0; attempt < 16; ++attempt) {
                x = cx * 16 + (int) (maage_generation_feature_next(&random) % 16UL);
                y = 10 + (int) (maage_generation_feature_next(&random) % 108UL);
                z = cz * 16 + (int) (maage_generation_feature_next(&random) % 16UL);
                for (step = 0; step < 13; ++step) {
                    if (y >= 10 && y <= 117 && maage_world_chunk_coordinate(x) == maage_chunk_x(chunk) && maage_world_chunk_coordinate(z) == maage_chunk_z(chunk) && maage_chunk_get(chunk, maage_world_local_coordinate(x), y, maage_world_local_coordinate(z)) == ids->state_netherrack) {
                        result = dimension_set(chunk, x, y, z, ids->state_quartz_ore);
                        if (maage_status_failed(result)) return result;
                    }
                    direction = (int) (maage_generation_feature_next(&random) % 6UL);
                    x += directions[direction][0];
                    y += directions[direction][1];
                    z += directions[direction][2];
                }
            }
        }
    return dimension_result(MAAGE_STATUS_SUCCESS);
}
static maage_status dimension_nether_glowstone(const maage_base_ids *ids, maage_chunk *chunk, maage_world_seed seed, const maage_job_token *token) {
    dimension_nether_profile profiles[15][15], anchor;
    unsigned char occupied[13][15][15];
    int points[72][3];
    int cx, cz, attempt, x, y, z, root_x, root_y, root_z, i, count, target, parent, direction, dx, dy, dz, tries;
    unsigned long random;
    maage_status result;
    for (cz = maage_chunk_z(chunk) - 1; cz <= maage_chunk_z(chunk) + 1; ++cz)
        for (cx = maage_chunk_x(chunk) - 1; cx <= maage_chunk_x(chunk) + 1; ++cx) {
            if (maage_job_cancelled(token)) return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, MAAGE_DETAIL_CANCELLED);
            for (attempt = 0; attempt < 10; ++attempt) {
                random = maage_generation_feature_hash(seed, cx, cz, 72100UL + (unsigned long) attempt);
                root_x = cx * 16 + (int) (maage_generation_feature_next(&random) % 16UL);
                y = 40 + (int) (maage_generation_feature_next(&random) % 80UL);
                root_z = cz * 16 + (int) (maage_generation_feature_next(&random) % 16UL);
                anchor = dimension_nether_column(seed, root_x, root_z);
                root_y = anchor.ceiling - 1;
                if (y < root_y - 7 || y > root_y || !dimension_nether_air(&anchor, root_y) || dimension_nether_air(&anchor, root_y + 1) || (anchor.bridge && root_y + 1 >= 60 && root_y + 1 <= 65)) continue;
                for (z = 0; z < 15; ++z)
                    for (x = 0; x < 15; ++x)
                        profiles[z][x] = dimension_nether_column(seed, root_x + x - 7, root_z + z - 7);
                memset(occupied, 0, sizeof(occupied));
                occupied[0][7][7] = 1;
                points[0][0] = points[0][2] = 7;
                points[0][1] = 0;
                count = 1;
                target = 24 + (int) (maage_generation_feature_next(&random) % 49UL);
                /* Each accepted child shares a face with an existing cell;
                 * the root hangs immediately below the natural roof. */
                for (tries = 0; tries < target * 24 && count < target; ++tries) {
                    parent = (int) (maage_generation_feature_next(&random) % (unsigned long) count);
                    dx = points[parent][0];
                    dy = points[parent][1];
                    dz = points[parent][2];
                    direction = (int) (maage_generation_feature_next(&random) % 6UL);
                    if (direction == 0) --dx;
                    else if (direction == 1) ++dx;
                    else if (direction == 2) --dz;
                    else if (direction == 3) ++dz;
                    else ++dy;
                    if (dx < 0 || dx >= 15 || dz < 0 || dz >= 15 || dy >= 13 || occupied[dy][dz][dx] || !dimension_nether_air(&profiles[dz][dx], root_y - dy)) continue;
                    occupied[dy][dz][dx] = 1;
                    points[count][0] = dx;
                    points[count][1] = dy;
                    points[count][2] = dz;
                    ++count;
                }
                for (i = 0; i < count; ++i) {
                    x = root_x + points[i][0] - 7;
                    y = root_y - points[i][1];
                    z = root_z + points[i][2] - 7;
                    result = dimension_set(chunk, x, y, z, ids->dimension_states[DIM_GLOWSTONE]);
                    if (maage_status_failed(result)) return result;
                }
            }
        }
    return dimension_result(MAAGE_STATUS_SUCCESS);
}
static maage_status dimension_nether_generate(const void *context, maage_chunk *chunk, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, const maage_job_token *token, maage_status_trace *trace) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    maage_generation_output output;
    dimension_nether_profile profile;
    int x, y, z, wx, wz, soul;
    unsigned long hash;
    maage_block_state_id state;
    maage_status result = maage_chunk_generation_output(chunk, &output);
    (void) blocks;
    (void) biomes;
    (void) policy;
    (void) trace;
    if (maage_status_failed(result)) return result;
    for (z = 0; z < 16; ++z) {
        if (maage_job_cancelled(token)) return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, MAAGE_DETAIL_CANCELLED);
        for (x = 0; x < 16; ++x) {
            wx = output.x * 16 + x;
            wz = output.z * 16 + z;
            hash = maage_generation_feature_hash(seed, wx, wz, 72000);
            profile = dimension_nether_column(seed, wx, wz);
            soul = profile.floor >= 30 && profile.floor <= 35 && dimension_noise(seed, wx / 12.0, 0, wz / 12.0, 72008) > 0.1;
            for (y = 0; y < 128; ++y) {
                state = y <= profile.floor || y >= profile.ceiling ? ids->state_netherrack : (y < 32 ? ids->state_lava : ids->state_air);
                if (y <= (int) (hash % 5) || y >= 127 - (int) ((hash >> 8) % 5)) state = ids->state_bedrock;
                if (soul && y <= profile.floor && y >= profile.floor - 3 && y < profile.ceiling) state = ids->dimension_states[DIM_SOUL_SAND];
                if (profile.bridge && y >= 60 && y <= 65) state = y <= 61 ? ids->state_nether_brick : ids->state_air;
                if (state != ids->state_air) {
                    result = maage_chunk_set(chunk, x, y, z, state);
                    if (maage_status_failed(result)) return result;
                }
            }
            output.biomes[z * 16 + x] = ids->biome_hell;
            output.heightmap[z * 16 + x] = 128;
        }
    }
    result = dimension_nether_quartz(ids, chunk, seed, token);
    if (!maage_status_failed(result)) result = dimension_nether_glowstone(ids, chunk, seed, token);
    if (maage_status_failed(result)) return result;
    *output.stages = 255;
    return dimension_result(MAAGE_STATUS_SUCCESS);
}
static maage_status dimension_end_generate(const void *context, maage_chunk *chunk, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, const maage_job_token *token, maage_status_trace *trace) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    maage_generation_output output;
    int x, y, z, wx, wz, top, bottom, pillar, px, pz, height;
    double radius, shape, angle;
    maage_block_state_id state;
    maage_status result = maage_chunk_generation_output(chunk, &output);
    (void) blocks;
    (void) biomes;
    (void) policy;
    (void) trace;
    if (maage_status_failed(result)) return result;
    for (z = 0; z < 16; ++z) {
        if (maage_job_cancelled(token)) return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, MAAGE_DETAIL_CANCELLED);
        for (x = 0; x < 16; ++x) {
            wx = output.x * 16 + x;
            wz = output.z * 16 + z;
            radius = sqrt((double) wx * wx + (double) wz * wz);
            shape = 1 - radius / 110.0 + dimension_noise(seed, wx / 30.0, 0, wz / 30.0, 73001) * 0.22;
            top = 0;
            if (shape > 0) {
                top = 56 + (int) (shape * 12);
                bottom = 52 - (int) (shape * 24);
                for (y = bottom; y < top; ++y) {
                    result = maage_chunk_set(chunk, x, y, z, ids->dimension_states[DIM_END_STONE]);
                    if (maage_status_failed(result)) return result;
                }
            }
            for (pillar = 0; pillar < 10; ++pillar) {
                angle = pillar * 0.6283185307179586;
                px = (int) floor(cos(angle) * 42);
                pz = (int) floor(sin(angle) * 42);
                if ((wx - px) * (double) (wx - px) + (wz - pz) * (double) (wz - pz) > 9) continue;
                height = 76 + (int) (maage_generation_feature_hash(seed, px, pz, 73002) % 30UL);
                for (y = top; y <= height; ++y) {
                    result = maage_chunk_set(chunk, x, y, z, ids->state_obsidian);
                    if (maage_status_failed(result)) return result;
                }
                top = height + 1;
            }
            /* A permanent exit fountain keeps travel usable independently of
             * future boss progression. It is ordinary persistent World content. */
            if (abs(wx) <= 3 && abs(wz) <= 3) {
                for (y = 63; y <= 68; ++y) {
                    state = ids->state_air;
                    if (y == 63 || ((abs(wx) == 3 || abs(wz) == 3) && y == 64) || (wx == 0 && wz == 0 && y <= 67)) state = ids->state_bedrock;
                    else if (y == 64) state = ids->dimension_states[DIM_END_PORTAL];
                    result = maage_chunk_set(chunk, x, y, z, state);
                    if (maage_status_failed(result)) return result;
                }
                top = wx == 0 && wz == 0 ? 68 : 65;
            }
            output.biomes[z * 16 + x] = ids->biome_sky;
            output.heightmap[z * 16 + x] = (unsigned short) top;
        }
    }
    *output.stages = 255;
    return dimension_result(MAAGE_STATUS_SUCCESS);
}
static maage_status dimension_fixed_spawn(const void *context, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, double position[3]) {
    (void) context;
    (void) seed;
    (void) blocks;
    (void) biomes;
    (void) policy;
    position[0] = 100.5;
    position[1] = 50;
    position[2] = 0.5;
    return dimension_result(MAAGE_STATUS_SUCCESS);
}
/* Only loaded cells participate; unavailable frame faces defer the operation.
 * Corner blocks are optional. The interior supports widths/heights 2..21. */
static maage_status dimension_nether_frame(const maage_base_ids *ids, const maage_world *world, int x, int y, int z, int axis, maage_block_plan *plan, int *width, int *height) {
    int dx = axis == 0, dz = axis == 1, i, j, left = 0, bottom = y;
    maage_block_state_id state;
    while (bottom > 1 && y - bottom < 21 && maage_world_get(world, x, bottom - 1, z) != ids->state_obsidian)
        --bottom;
    if (maage_world_get(world, x, bottom - 1, z) != ids->state_obsidian) return dimension_result(MAAGE_STATUS_NOT_FOUND);
    for (i = 1; i <= 22; ++i) {
        if (!dimension_loaded(world, x - i * dx, z - i * dz)) return dimension_result(MAAGE_STATUS_NOOP);
        state = maage_world_get(world, x - i * dx, bottom, z - i * dz);
        if (state == ids->state_obsidian) {
            left = i - 1;
            break;
        }
        if (state != ids->state_air && state != ids->portal_states[axis]) return dimension_result(MAAGE_STATUS_NOT_FOUND);
    }
    if (i > 22) return dimension_result(MAAGE_STATUS_NOT_FOUND);
    x -= left * dx;
    z -= left * dz;
    for (i = 0; i <= 21; ++i) {
        if (!dimension_loaded(world, x + i * dx, z + i * dz)) return dimension_result(MAAGE_STATUS_NOOP);
        state = maage_world_get(world, x + i * dx, bottom, z + i * dz);
        if (state == ids->state_obsidian) break;
        if (state != ids->state_air && state != ids->portal_states[axis]) return dimension_result(MAAGE_STATUS_NOT_FOUND);
    }
    if (i < 2 || i > 21) return dimension_result(MAAGE_STATUS_NOT_FOUND);
    *width = i;
    for (j = 0; j <= 21 && bottom + j < 255; ++j) {
        int roof = 1;
        for (i = 0; i < *width; ++i)
            if (maage_world_get(world, x + i * dx, bottom + j, z + i * dz) != ids->state_obsidian) roof = 0;
        if (roof) break;
        if (maage_world_get(world, x - dx, bottom + j, z - dz) != ids->state_obsidian || maage_world_get(world, x + *width * dx, bottom + j, z + *width * dz) != ids->state_obsidian) return dimension_result(MAAGE_STATUS_NOT_FOUND);
        for (i = 0; i < *width; ++i) {
            state = maage_world_get(world, x + i * dx, bottom + j, z + i * dz);
            if (state != ids->state_air && state != ids->portal_states[axis]) return dimension_result(MAAGE_STATUS_NOT_FOUND);
            if (!j && maage_world_get(world, x + i * dx, bottom - 1, z + i * dz) != ids->state_obsidian) return dimension_result(MAAGE_STATUS_NOT_FOUND);
        }
    }
    if (j < 3 || j > 21) return dimension_result(MAAGE_STATUS_NOT_FOUND);
    *height = j;
    plan->x = x;
    plan->y = bottom;
    plan->z = z;
    plan->edge = *width;
    plan->height = *height;
    return dimension_result(MAAGE_STATUS_SUCCESS);
}
static maage_status dimension_ignite(const maage_item_definition *item, maage_world *world, const maage_world_target *target, int *consume_use) {
    static const int offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    maage_block_state_id cells[21 * 21 * 21];
    maage_block_plan plan;
    maage_status result;
    int x, y, z, axis, width, height, i, j;
    (void) consume_use;
    if (maage_world_dimension(world)->id == ids->dimension_end || target->face >= 6) return dimension_result(MAAGE_STATUS_NOOP);
    x = target->x + offsets[target->face][0];
    y = target->y + offsets[target->face][1];
    z = target->z + offsets[target->face][2];
    for (axis = 0; axis < 2; ++axis) {
        result = dimension_nether_frame(ids, world, x, y, z, axis, &plan, &width, &height);
        if (maage_status_code(result) == MAAGE_STATUS_NOOP) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) continue;
        for (i = 0; i < plan.edge * plan.edge * plan.height; ++i)
            cells[i] = MAAGE_CONTENT_ID_INVALID;
        for (j = 0; j < height; ++j)
            for (i = 0; i < width; ++i)
                cells[(j * width + (axis ? i : 0)) * width + (axis ? 0 : i)] = ids->portal_states[axis];
        plan.cells = cells;
        return maage_world_apply_plan(world, &plan);
    }
    return dimension_result(MAAGE_STATUS_NOOP);
}
static maage_status dimension_portal_neighbor(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, const maage_block_neighbor_change *change, int *deferred, size_t *continuation) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    maage_block_plan plan;
    int width, height, axis = state->id == ids->portal_states[1];
    maage_status result = dimension_nether_frame(ids, world, x, y, z, axis, &plan, &width, &height);
    (void) change;
    (void) continuation;
    if (maage_status_code(result) == MAAGE_STATUS_NOOP) {
        *deferred = 1;
        return result;
    }
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) return result;
    if (maage_status_code(result) != MAAGE_STATUS_NOT_FOUND) return result;
    return maage_world_set(world, x, y, z, state->id, ids->state_air);
}
static int dimension_ring_frame(int dx, int dz) {
    return (abs(dx) == 2 && abs(dz) <= 1) || (abs(dz) == 2 && abs(dx) <= 1);
}
static maage_status dimension_end_neighbor(maage_world *world, int x, int y, int z, const maage_block_state_definition *state, const maage_block_neighbor_change *change, int *deferred, size_t *continuation) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    int cx, cz, dx, dz, facing, complete, missing = 0;
    (void) change;
    (void) continuation;
    if (maage_world_dimension(world)->id == ids->dimension_end) return dimension_result(MAAGE_STATUS_SUCCESS);
    for (cz = z - 1; cz <= z + 1; ++cz)
        for (cx = x - 1; cx <= x + 1; ++cx) {
            complete = 1;
            for (dz = -2; dz <= 2 && complete; ++dz)
                for (dx = -2; dx <= 2; ++dx) {
                    if (!dimension_ring_frame(dx, dz)) continue;
                    if (!dimension_loaded(world, cx + dx, cz + dz)) {
                        missing = 1;
                        complete = 0;
                        break;
                    }
                    facing = dz == -2 ? 2 : (dx == 2 ? 3 : (dz == 2 ? 0 : 1));
                    if (maage_world_get(world, cx + dx, y, cz + dz) != ids->frame_states[1][facing]) {
                        complete = 0;
                        break;
                    }
                }
            if (complete) return dimension_result(MAAGE_STATUS_SUCCESS);
        }
    if (missing) {
        *deferred = 1;
        return dimension_result(MAAGE_STATUS_NOOP);
    }
    return maage_world_set(world, x, y, z, state->id, ids->state_air);
}
static maage_status dimension_eye_use(const maage_item_definition *item, maage_world *world, const maage_world_target *target, int *consume_use) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    const maage_block_state_definition *state = maage_block_registry_state(maage_world_registry(world), target->state_id);
    maage_block_state_id cells[25], filled;
    maage_block_plan plan;
    maage_property_value facing, eye;
    int cx, cz, dx, dz, expected, complete, restore;
    double position[3];
    maage_status result;
    if (!state || state->block_id != ids->dimension_blocks[DIM_FRAME]) return dimension_result(MAAGE_STATUS_NOOP);
    facing = maage_base_state_value(maage_world_registry(world), state, ids->property_facing);
    eye = maage_base_state_value(maage_world_registry(world), state, ids->property_eye);
    if (facing > 3) return dimension_result(MAAGE_STATUS_NOOP);
    *consume_use = !eye;
    for (dz = -6; dz <= 6; dz += 12)
        for (dx = -6; dx <= 6; dx += 12)
            if (!dimension_loaded(world, target->x + dx, target->z + dz)) return dimension_result(MAAGE_STATUS_NOOP);
    filled = ids->frame_states[1][facing];
    /* The clicked frame can lie at any ring position. A filled frame can
     * restore a complete ring's missing surface without inserting another Eye.
     * The last new Eye and all nine surface cells still commit in one plan. */
    for (cz = target->z - 2; cz <= target->z + 2; ++cz)
        for (cx = target->x - 2; cx <= target->x + 2; ++cx) {
            if (!dimension_ring_frame(target->x - cx, target->z - cz)) continue;
            complete = 1;
            for (dz = -2; dz <= 2 && complete; ++dz)
                for (dx = -2; dx <= 2; ++dx) {
                    if (!dimension_ring_frame(dx, dz)) continue;
                    if (!dimension_loaded(world, cx + dx, cz + dz)) {
                        complete = 0;
                        break;
                    }
                    expected = dz == -2 ? 2 : (dx == 2 ? 3 : (dz == 2 ? 0 : 1));
                    if (cx + dx == target->x && cz + dz == target->z) {
                        if (filled != ids->frame_states[1][expected]) complete = 0;
                    } else if (maage_world_get(world, cx + dx, target->y, cz + dz) != ids->frame_states[1][expected]) complete = 0;
                }
            if (!complete) continue;
            restore = !eye;
            for (dz = -2; dz <= 2; ++dz)
                for (dx = -2; dx <= 2; ++dx) {
                    cells[(dz + 2) * 5 + dx + 2] = abs(dx) <= 1 && abs(dz) <= 1 ? ids->dimension_states[DIM_END_PORTAL] : MAAGE_CONTENT_ID_INVALID;
                    if (abs(dx) <= 1 && abs(dz) <= 1 && maage_world_get(world, cx + dx, target->y, cz + dz) != ids->dimension_states[DIM_END_PORTAL]) restore = 1;
                }
            if (!restore) return dimension_result(MAAGE_STATUS_NOOP);
            cells[(target->z - cz + 2) * 5 + target->x - cx + 2] = filled;
            plan.x = cx - 2;
            plan.y = target->y;
            plan.z = cz - 2;
            plan.edge = 5;
            plan.height = 1;
            plan.cells = cells;
            result = maage_world_apply_plan(world, &plan);
            if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
                position[0] = cx + 0.5;
                position[1] = target->y + 0.75;
                position[2] = cz + 0.5;
                (void) maage_world_emit_sound(world, ids->end_portal_open_sound, position, 1, 1);
            }
            return result;
        }
    return eye ? dimension_result(MAAGE_STATUS_NOOP) : maage_world_set(world, target->x, target->y, target->z, target->state_id, filled);
}
static maage_status dimension_frame_placement(const maage_item_definition *item, maage_item_variant_id variant, maage_block_face face, double yaw, maage_block_state_id *state) {
    const maage_base_ids *ids = (const maage_base_ids *) item->behavior_context;
    int facing = ((int) floor(yaw / 1.570796326794897 + 0.5) % 4 + 6) % 4;
    (void) face;
    (void) variant;
    *state = ids->frame_states[0][facing];
    return dimension_result(MAAGE_STATUS_SUCCESS);
}
static maage_status dimension_portal_geometry(const void *context, const maage_block_geometry_query *query, const maage_block_geometry_sink *sink) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    maage_block_geometry_quad quad;
    int i, axis = query->state->id == ids->portal_states[1];
    memset(&quad, 0, sizeof(quad));
    quad.double_sided = 1;
    quad.material = MAAGE_BLOCK_GEOMETRY_INHERIT_MATERIAL;
    quad.face = query->block->id == ids->dimension_blocks[DIM_END_PORTAL] ? MAAGE_FACE_POSITIVE_Y : (axis ? MAAGE_FACE_POSITIVE_X : MAAGE_FACE_POSITIVE_Z);
    for (i = 0; i < 4; ++i) {
        quad.uv[i][0] = i == 1 || i == 2 ? 1 : 0;
        quad.uv[i][1] = i >= 2 ? 1 : 0;
        if (quad.face == MAAGE_FACE_POSITIVE_Y) {
            quad.positions[i][0] = quad.uv[i][0];
            quad.positions[i][1] = 0.75f;
            quad.positions[i][2] = quad.uv[i][1];
        } else {
            quad.positions[i][axis ? 2 : 0] = quad.uv[i][0];
            quad.positions[i][1] = 1 - quad.uv[i][1];
            quad.positions[i][axis ? 0 : 2] = 0.5f;
        }
    }
    return sink->quad(sink->context, &quad);
}
typedef struct dimension_frame_emitter {
    const maage_block_geometry_sink *sink;
    unsigned int facing;
    int eye;
} dimension_frame_emitter;
static maage_status dimension_frame_quad(void *context, const maage_block_geometry_quad *source) {
    dimension_frame_emitter *emitter = (dimension_frame_emitter *) context;
    maage_block_geometry_quad quad = *source;
    unsigned int corner, turn;
    float u;
    for (corner = 0; corner < 4; ++corner) {
        if (quad.face == MAAGE_FACE_POSITIVE_Y)
            for (turn = 0; turn < emitter->facing; ++turn) {
                u = quad.uv[corner][0];
                quad.uv[corner][0] = 1 - quad.uv[corner][1];
                quad.uv[corner][1] = u;
            }
        if (emitter->eye) {
            quad.uv[corner][0] = 0.25f + quad.uv[corner][0] * 0.5f;
            quad.uv[corner][1] = quad.face == MAAGE_FACE_POSITIVE_Y ? 0.25f + quad.uv[corner][1] * 0.5f : quad.uv[corner][1] * 3 / 16;
        } else if (quad.face / 2 != 1) quad.uv[corner][1] = 3.0f / 16 + quad.uv[corner][1] * 13 / 16;
    }
    return emitter->sink->quad(emitter->sink->context, &quad);
}
static maage_status dimension_frame_geometry(const void *context, const maage_block_geometry_query *query, const maage_block_geometry_sink *sink) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    dimension_frame_emitter emitter;
    maage_block_geometry_sink mapped;
    maage_block_shape bounds;
    maage_status result;
    emitter.sink = sink;
    emitter.facing = (unsigned int) maage_base_state_value(query->blocks, query->state, ids->property_facing);
    emitter.eye = 0;
    mapped.context = &emitter;
    mapped.quad = dimension_frame_quad;
    memset(&bounds, 0, sizeof(bounds));
    bounds.maximum[0] = bounds.maximum[2] = 1;
    bounds.maximum[1] = 13.0 / 16;
    result = maage_block_geometry_cuboid(&mapped, &bounds, query->state->faces, 63, query->block->mesh_material, 1);
    if (maage_status_failed(result) || !maage_base_state_value(query->blocks, query->state, ids->property_eye)) return result;
    bounds.minimum[0] = bounds.minimum[2] = 4.0 / 16;
    bounds.minimum[1] = bounds.maximum[1];
    bounds.maximum[0] = bounds.maximum[2] = 12.0 / 16;
    bounds.maximum[1] = 1;
    emitter.eye = 1;
    return maage_block_geometry_cuboid(&mapped, &bounds, query->geometry->appearances, 63U & ~(1U << MAAGE_FACE_NEGATIVE_Y), query->block->mesh_material, 0);
}
static maage_status dimension_contact(const void *context, maage_world *world, const double position[3], int creative, maage_dimension_travel *travel) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    const maage_dimension_definition *source = maage_world_dimension(world);
    maage_block_state_id state;
    int x, y, z, nether = 0, end = 0;
    for (z = (int) floor(position[2] - 0.29); z <= (int) floor(position[2] + 0.29); ++z)
        for (x = (int) floor(position[0] - 0.29); x <= (int) floor(position[0] + 0.29); ++x)
            for (y = (int) floor(position[1] - 0.05); y <= (int) floor(position[1] + 1.79); ++y) {
                if (!dimension_loaded(world, x, z)) continue;
                state = maage_world_get(world, x, y, z);
                if ((state == ids->portal_states[0] && fabs(position[2] - z - 0.5) < 0.3) || (state == ids->portal_states[1] && fabs(position[0] - x - 0.5) < 0.3)) nether = 1;
                if (state == ids->dimension_states[DIM_END_PORTAL] && position[1] <= y + 0.75 && position[1] + 1.8 >= y + 0.75) end = 1;
            }
    if (!nether && !end) return dimension_result(MAAGE_STATUS_NOOP);
    memcpy(travel->position, position, sizeof(travel->position));
    if (end) {
        travel->destination = source->id == ids->dimension_end ? ids->dimension_overworld : ids->dimension_end;
        travel->kind = source->id == ids->dimension_end ? TRAVEL_RETURN : TRAVEL_END;
        travel->delay = 1;
        travel->presentation = ids->end_presentation;
        travel->credits = source->id == ids->dimension_end;
        if (travel->kind == TRAVEL_END) {
            travel->position[0] = 100.5;
            travel->position[1] = 50;
            travel->position[2] = 0.5;
        } else travel->at_spawn = 1;
    } else {
        if (source->id == ids->dimension_end) return dimension_result(MAAGE_STATUS_NOOP);
        travel->destination = source->id == ids->dimension_nether ? ids->dimension_overworld : ids->dimension_nether;
        travel->kind = TRAVEL_NETHER;
        travel->arrival_radius = 10; /* Lookup radius plus frame and plan margins. */
        travel->delay = creative ? 1 : 80;
        travel->cooldown = 300;
        travel->presentation = ids->nether_presentation;
        travel->position[0] *= source->id == ids->dimension_nether ? 8 : 0.125;
        travel->position[2] *= source->id == ids->dimension_nether ? 8 : 0.125;
        if (travel->position[1] > 118) travel->position[1] = 118;
        if (travel->position[1] < 35) travel->position[1] = 35;
    }
    if (travel->position[0] < -MAAGE_WORLD_LIMIT + 256) travel->position[0] = -MAAGE_WORLD_LIMIT + 256;
    if (travel->position[0] > MAAGE_WORLD_LIMIT - 256) travel->position[0] = MAAGE_WORLD_LIMIT - 256;
    if (travel->position[2] < -MAAGE_WORLD_LIMIT + 256) travel->position[2] = -MAAGE_WORLD_LIMIT + 256;
    if (travel->position[2] > MAAGE_WORLD_LIMIT - 256) travel->position[2] = MAAGE_WORLD_LIMIT - 256;
    return dimension_result(MAAGE_STATUS_SUCCESS);
}
/* A five-by-five platform and its dry perimeter must fit in loaded terrain.
 * Surface candidates use the topmost cell, never a cave below the heightmap. */
static const maage_block_definition *dimension_site_block(const maage_world *world, int x, int y, int z, const maage_block_state_definition **state) {
    const maage_block_registry *blocks = maage_world_registry(world);
    *state = maage_block_registry_state(blocks, maage_world_get(world, x, y, z));
    return *state ? maage_block_registry_block(blocks, (*state)->block_id) : NULL;
}
static int dimension_site_floor(const maage_base_ids *ids, const maage_world *world, int x, int y, int z) {
    const maage_block_state_definition *state;
    const maage_block_definition *block = dimension_site_block(world, x, y, z, &state);
    maage_block_shape shape;
    return block && block->solid && !block->contact_damage && !block->entity && !block->foliage && state->block_id != ids->block_bedrock && !state->fluid_kind && maage_block_collision_shape(block, state, &shape) && shape.minimum[0] <= 0 && shape.minimum[2] <= 0 && shape.maximum[0] >= 1 && shape.maximum[1] >= 1 && shape.maximum[2] >= 1;
}
static int dimension_site_clear(const maage_base_ids *ids, const maage_world *world, int x, int y, int z, int carve, int water) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    int dx, dy, dz, edge;
    if (y < 1 || y > MAAGE_WORLD_HEIGHT - 5) return 0;
    for (dz = -3; dz <= 3; ++dz)
        for (dx = -3; dx <= 3; ++dx) {
            edge = abs(dx) == 3 || abs(dz) == 3;
            if (!dimension_loaded(world, x + dx, z + dz)) return 0;
            for (dy = 0; dy < 5; ++dy) {
                block = dimension_site_block(world, x + dx, y + dy, z + dz, &state);
                if (!block) return 0;
                if (dy == 0 && water && state->fluid_kind == ids->fluid_water) continue;
                if (state->fluid_kind || block->contact_damage) return 0;
                if (edge) continue;
                if (block->entity || state->block_id == ids->block_bedrock || block->portal_presentation) return 0;
                if (dy && !carve && state->id != ids->state_air && !block->replaceable) return 0;
            }
            if (carve && y + 5 < MAAGE_WORLD_HEIGHT) {
                block = dimension_site_block(world, x + dx, y + 5, z + dz, &state);
                if (!block || state->fluid_kind || block->contact_damage) return 0;
            }
        }
    return 1;
}
static int dimension_surface_site(const maage_base_ids *ids, const maage_world *world, int x, int z, int sea, int water_only, int *floor_y) {
    maage_world_observation observation;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    int y, water, dx, dz;
    if (!maage_world_observe(world, x, sea, z, &observation)) return 0;
    y = observation.precipitation_height - 1;
    if (y < 1 || y > MAAGE_WORLD_HEIGHT - 5) return 0;
    block = dimension_site_block(world, x, y, z, &state);
    if (!block) return 0;
    water = y == sea && state->fluid_kind == ids->fluid_water;
    if (water_only && !water) return 0;
    if (!water) {
        if (y < sea || !dimension_site_floor(ids, world, x, y, z)) return 0;
        for (dz = -1; dz <= 1; ++dz)
            for (dx = -1; dx <= 1; ++dx)
                if (!dimension_site_floor(ids, world, x + dx, y, z + dz)) return 0;
    }
    if (!dimension_site_clear(ids, world, x, y, z, 0, water)) return 0;
    *floor_y = y;
    return 1;
}
static void dimension_ring_offset(int radius, int index, int *dx, int *dz) {
    if (!radius) { *dx = *dz = 0; return; }
    if (index < radius * 2) { *dx = -radius + index; *dz = -radius; }
    else if (index < radius * 4) { *dx = radius; *dz = -radius + index - radius * 2; }
    else if (index < radius * 6) { *dx = radius - (index - radius * 4); *dz = radius; }
    else { *dx = -radius; *dz = radius - (index - radius * 6); }
}
static int dimension_find_surface(const maage_base_ids *ids, const maage_world *world, int x, int z, int sea, int water_only, int *out_x, int *out_y, int *out_z) {
    int radius, index, dx, dz;
    for (radius = 0; radius <= 112; ++radius)
        for (index = 0; index < (radius ? radius * 8 : 1); ++index) {
            dimension_ring_offset(radius, index, &dx, &dz);
            if (dimension_surface_site(ids, world, x + dx, z + dz, sea, water_only, out_y)) {
                *out_x = x + dx;
                *out_z = z + dz;
                return 1;
            }
        }
    return 0;
}
static int dimension_find_cavity(const maage_base_ids *ids, const maage_world *world, int x, int y, int z, int nether, int carve, int *out_x, int *out_y, int *out_z) {
    int radius, index, dx, dz, offset, direction, height, top = nether ? 118 : MAAGE_WORLD_HEIGHT - 6;
    for (radius = 0; radius <= 32; ++radius)
        for (index = 0; index < (radius ? radius * 8 : 1); ++index) {
            dimension_ring_offset(radius, index, &dx, &dz);
            for (offset = 0; offset <= top; ++offset)
                for (direction = 1; direction >= -1; direction -= 2) {
                    if (!offset && direction < 0) continue;
                    height = y + offset * direction;
                    if (height < 5 || height > top || !dimension_site_floor(ids, world, x + dx, height, z + dz)) continue;
                    if (!carve && maage_world_get(world, x + dx, height + 1, z + dz) != ids->state_air) continue;
                    if (!dimension_site_clear(ids, world, x + dx, height, z + dz, carve, 0)) continue;
                    *out_x = x + dx;
                    *out_y = height;
                    *out_z = z + dz;
                    return 1;
                }
        }
    return 0;
}
static maage_status dimension_arrive(const void *context, maage_world *world, maage_dimension_travel *travel) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    maage_block_state_id cells[5 * 5 * 5];
    maage_block_plan plan;
    maage_block_position portal;
    maage_status result;
    const maage_generation_policy *policy = maage_world_generation_policy(world);
    const maage_block_state_definition *surface;
    maage_world_observation observation;
    int x, y, z, i, j, k, origin_x, origin_z, origin_y, found, ocean, nether = maage_world_dimension(world)->id == ids->dimension_nether;
    if (travel->kind == TRAVEL_NETHER) {
        result = maage_world_find_states(world, ids->portal_states, 2, travel->position, 128, &portal);
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
            int width, portal_height;
            maage_block_plan frame;
            result = dimension_nether_frame(ids, world, portal.x, portal.y, portal.z, maage_world_get(world, portal.x, portal.y, portal.z) == ids->portal_states[1], &frame, &width, &portal_height);
            if (maage_status_code(result) == MAAGE_STATUS_NOOP) return result;
        }
        if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
            while (portal.y > 1 && (maage_world_get(world, portal.x, portal.y - 1, portal.z) == ids->portal_states[0] || maage_world_get(world, portal.x, portal.y - 1, portal.z) == ids->portal_states[1]))
                --portal.y;
            /* Reuse a valid frame only if at least one side has dry,
             * two-block headroom and a solid standing surface. */
            k = maage_world_get(world, portal.x, portal.y, portal.z) == ids->portal_states[1];
            for (i = -1; i <= 1; i += 2) {
                x = portal.x + (k ? i : 0);
                z = portal.z + (k ? 0 : i);
                if (!dimension_loaded(world, x, z)) return dimension_result(MAAGE_STATUS_NOOP);
                if (dimension_site_floor(ids, world, x, portal.y - 1, z) && maage_world_get(world, x, portal.y, z) == ids->state_air && maage_world_get(world, x, portal.y + 1, z) == ids->state_air) {
                    travel->position[0] = portal.x + 0.5;
                    travel->position[1] = portal.y;
                    travel->position[2] = portal.z + 0.5;
                    return result;
                }
            }
            result = dimension_result(MAAGE_STATUS_NOT_FOUND);
        }
        if (maage_status_code(result) != MAAGE_STATUS_NOT_FOUND) return result;
    }
    x = (int) floor(travel->position[0]);
    z = (int) floor(travel->position[2]);
    y = travel->kind == TRAVEL_END ? 49 : (int) floor(travel->position[1]) - 1;
    if (travel->kind == TRAVEL_NETHER) {
        origin_x = x;
        origin_y = y;
        origin_z = z;
        found = 0;
        ocean = 0;
        if (!nether) {
            surface = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, x, policy->sea_level, z));
            ocean = surface && surface->fluid_kind == ids->fluid_water;
            if (maage_world_observe(world, x, policy->sea_level, z, &observation) && observation.biome_id == ids->biome_ocean) ocean = 1;
            if (ocean) found = dimension_find_surface(ids, world, origin_x, origin_z, policy->sea_level, 1, &x, &y, &z);
            if (!found) found = dimension_find_surface(ids, world, origin_x, origin_z, policy->sea_level, 0, &x, &y, &z);
        }
        if (!found && !ocean) found = dimension_find_cavity(ids, world, origin_x, origin_y, origin_z, nether, 0, &x, &y, &z);
        if (!found && !ocean) found = dimension_find_cavity(ids, world, origin_x, origin_y, origin_z, nether, 1, &x, &y, &z);
        if (!found) return dimension_result(MAAGE_STATUS_NOT_FOUND);
    }
    for (k = -2; k <= 2; ++k)
        for (i = -2; i <= 2; ++i)
            if (!dimension_loaded(world, x + i, z + k)) return dimension_result(MAAGE_STATUS_NOOP);
    for (j = 0; j < 5; ++j)
        for (k = 0; k < 5; ++k)
            for (i = 0; i < 5; ++i) {
                maage_block_state_id state = j == 0 ? ids->state_obsidian : ids->state_air;
                if (travel->kind == TRAVEL_NETHER && k == 2 && i >= 1 && i <= 4 && j >= 1) state = i == 1 || i == 4 || j == 4 ? ids->state_obsidian : ids->portal_states[0];
                cells[(j * 5 + k) * 5 + i] = state;
            }
    plan.x = x - 2;
    plan.y = y;
    plan.z = z - 2;
    plan.edge = 5;
    plan.height = 5;
    plan.cells = cells;
    result = maage_world_apply_plan(world, &plan);
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
        travel->position[0] = x + 0.5;
        travel->position[1] = y + 1;
        travel->position[2] = z + 0.5;
    }
    return result;
}
static maage_status dimension_register_block(maage_content_context *context, maage_base_ids *ids, unsigned int index, const char *name, const char *label, int texture, int solid, double hardness) {
    maage_block_definition block;
    maage_status result;
    memset(&block, 0, sizeof(block));
    block.behavior_context = ids;
    block.geometry_id = ids->geometry_cube;
    block.solid = block.material_solid = block.selectable = solid;
    block.opaque = solid;
    block.light_opacity = solid ? 15 : 0;
    block.hardness = hardness;
    block.material = ids->material_stone;
    if (index == DIM_END_STONE) {
        block.required_tool = MAAGE_TOOL_PICKAXE;
        block.harvest_requires_tool = 1;
    }
    if (index == DIM_SOUL_SAND) {
        block.required_tool = MAAGE_TOOL_SHOVEL;
        block.material = ids->material_sand;
    }
    block.item_drag = 0.588;
    block.selection_shape.maximum[0] = block.selection_shape.maximum[1] = block.selection_shape.maximum[2] = 1;
    if (solid) block.collision_shape = block.selection_shape;
    if (index == DIM_PORTAL || index == DIM_END_PORTAL) {
        block.geometry_id = ids->portal_geometry;
        block.portal_presentation = index == DIM_PORTAL ? ids->nether_presentation : ids->end_presentation;
        block.emitted_light = index == DIM_PORTAL ? 11 : 15;
        block.mesh_material = index == DIM_END_PORTAL ? MAAGE_MESH_MATERIAL_STARFIELD : MAAGE_MESH_MATERIAL_TRANSLUCENT;
    }
    if (index == DIM_PORTAL) block.neighbor_changed = dimension_portal_neighbor;
    if (index == DIM_END_PORTAL) block.neighbor_changed = dimension_end_neighbor;
    if (index == DIM_GLOWSTONE) block.emitted_light = 15;
    if (index == DIM_SOUL_SAND) {
        block.item_drag = 0.4;
        block.collision_shape.maximum[1] = 0.875;
    }
    if (index == DIM_FRAME) {
        block.geometry_id = ids->frame_geometry;
        block.opaque = 0;
        /* Recessed body/eye faces must sample exposed neighbor light rather
         * than the fully light-blocking cell occupied by the frame itself. */
        block.use_neighbor_brightness = 1;
        block.collision_shape.maximum[1] = 0.8125;
        block.selection_shape.maximum[1] = 0.8125;
    }
    result = maage_base_register_block(context->blocks, name, &block);
    if (maage_status_failed(result)) return result;
    ids->dimension_blocks[index] = block.id;
    if (index == DIM_PORTAL || index == DIM_FRAME) return result;
    result = maage_content_register_state(context->blocks, ids, context->config, &ids->dimension_states[index], block.id, NULL, 0, texture);
    if (maage_status_failed(result)) return result;
    if (solid) {
        result = base_block_item(context, name, label, block.id, ids->dimension_states[index], 0, index == DIM_FRAME ? dimension_frame_placement : NULL, 0, &ids->dimension_items[index]);
        if (maage_status_failed(result)) return result;
        if (hardness >= 0) {
            block.drop_item_id = ids->dimension_items[index];
            block.drop_count = 1;
        }
        result = maage_block_registry_update_block(context->blocks, &block);
    }
    return result;
}
maage_status maage_base_dimensions(maage_content_context *context, maage_base_ids *ids) {
    static const char *const paths[9] = {"minecraft/textures/blocks/end_stone.png", "minecraft/textures/blocks/soul_sand.png", "minecraft/textures/blocks/glowstone.png", NULL, "minecraft/textures/blocks/endframe_top.png", "minecraft/textures/blocks/endframe_eye.png", "minecraft/textures/blocks/endframe_side.png", "minecraft/textures/blocks/obsidian.png", "minecraft/textures/entity/end_portal.png"};
    maage_texture_definition texture;
    maage_block_geometry_definition geometry;
    maage_block_face_definition appearances[MAAGE_FACE_COUNT];
    maage_block_state_definition state;
    maage_biome_definition biome;
    maage_dimension_definition dimension;
    maage_property_definition property;
    maage_property_assignment properties[2];
    maage_item_definition item;
    maage_status result;
    unsigned int i, eye, facing, face, channel;
    memset(&geometry, 0, sizeof(geometry));
    geometry.primitive = MAAGE_BLOCK_GEOMETRY_CUSTOM;
    geometry.emit = dimension_portal_geometry;
    geometry.context = ids;
    result = maage_block_geometry_register(context->geometry, "minecraft:portal", &geometry, &ids->portal_geometry);
    if (maage_status_failed(result)) return result;
    for (i = 0; i < 9; ++i) {
        if (!paths[i]) continue; /* Portal texture is shared with the screen effect. */
        memset(&texture, 0, sizeof(texture));
        texture.fallback = MAAGE_CONTENT_ID_INVALID;
        result = maage_presentation_source_register(context->presentation, paths[i], &texture.source);
        if (!maage_status_failed(result)) result = maage_presentation_texture_register(context->presentation, &texture, &ids->textures[MAAGE_BASE_TEXTURE_DIMENSIONS + i]);
        if (maage_status_failed(result)) return result;
    }
    memset(appearances, 0, sizeof(appearances));
    for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
        appearances[face].texture_id = ids->textures[MAAGE_BASE_TEXTURE_DIMENSIONS + 5];
        for (channel = 0; channel < 4; ++channel)
            appearances[face].tint[channel] = 1;
        appearances[face].uv[2] = appearances[face].uv[3] = 1;
    }
    geometry.emit = dimension_frame_geometry;
    geometry.appearances = appearances;
    geometry.appearance_count = MAAGE_FACE_COUNT;
    result = maage_block_geometry_register(context->geometry, "minecraft:end_portal_frame", &geometry, &ids->frame_geometry);
    if (maage_status_failed(result)) return result;
    result = dimension_register_block(context, ids, DIM_END_STONE, "minecraft:end_stone", "End Stone", MAAGE_BASE_TEXTURE_DIMENSIONS, 1, 3);
    if (!maage_status_failed(result)) result = dimension_register_block(context, ids, DIM_SOUL_SAND, "minecraft:soul_sand", "Soul Sand", MAAGE_BASE_TEXTURE_DIMENSIONS + 1, 1, 0.5);
    if (!maage_status_failed(result)) result = dimension_register_block(context, ids, DIM_GLOWSTONE, "minecraft:glowstone", "Glowstone", MAAGE_BASE_TEXTURE_DIMENSIONS + 2, 1, 0.3);
    if (!maage_status_failed(result)) result = dimension_register_block(context, ids, DIM_PORTAL, "minecraft:portal", "Nether Portal", MAAGE_BASE_TEXTURE_DIMENSIONS + 3, 0, -1);
    if (!maage_status_failed(result)) result = dimension_register_block(context, ids, DIM_END_PORTAL, "minecraft:end_portal", "End Portal", MAAGE_BASE_TEXTURE_DIMENSIONS + 8, 0, -1);
    if (!maage_status_failed(result)) result = dimension_register_block(context, ids, DIM_FRAME, "minecraft:end_portal_frame", "End Portal Frame", MAAGE_BASE_TEXTURE_DIMENSIONS + 4, 1, -1);
    if (maage_status_failed(result)) return result;
    memset(&property, 0, sizeof(property));
    property.kind = MAAGE_PROPERTY_BOOLEAN;
    property.maximum = 1;
    result = maage_property_register(context->traits, "minecraft:portal_axis", &property);
    if (maage_status_failed(result)) return result;
    properties[0].property = property.id;
    properties[0].value = 0;
    result = maage_content_register_state(context->blocks, ids, context->config, &ids->portal_states[0], ids->dimension_blocks[DIM_PORTAL], properties, 1, MAAGE_BASE_TEXTURE_DIMENSIONS + 3);
    if (maage_status_failed(result)) return result;
    ids->dimension_states[DIM_PORTAL] = ids->portal_states[0];
    properties[0].value = 1;
    result = maage_content_register_state(context->blocks, ids, context->config, &ids->portal_states[1], ids->dimension_blocks[DIM_PORTAL], properties, 1, MAAGE_BASE_TEXTURE_DIMENSIONS + 3);
    if (maage_status_failed(result)) return result;
    result = maage_property_register(context->traits, "minecraft:eye", &property);
    if (maage_status_failed(result)) return result;
    ids->property_eye = property.id;
    for (eye = 0; eye < 2; ++eye)
        for (facing = 0; facing < 4; ++facing) {
            properties[0].property = ids->property_eye;
            properties[0].value = eye;
            properties[1].property = ids->property_facing;
            properties[1].value = facing;
            result = maage_content_register_state(context->blocks, ids, context->config, &ids->frame_states[eye][facing], ids->dimension_blocks[DIM_FRAME], properties, 2, MAAGE_BASE_TEXTURE_DIMENSIONS + 6);
            if (maage_status_failed(result)) return result;
            state = *maage_block_registry_state(context->blocks, ids->frame_states[eye][facing]);
            state.faces[MAAGE_FACE_POSITIVE_Y].texture_id = ids->textures[MAAGE_BASE_TEXTURE_DIMENSIONS + 4];
            state.faces[MAAGE_FACE_NEGATIVE_Y].texture_id = ids->textures[MAAGE_BASE_TEXTURE_DIMENSIONS];
            if (eye) state.selection_shape.maximum[0] = state.selection_shape.maximum[1] = state.selection_shape.maximum[2] = 1;
            result = maage_block_registry_update_state(context->blocks, &state);
            if (maage_status_failed(result)) return result;
        }
    ids->dimension_states[DIM_FRAME] = ids->frame_states[0][0];
    result = base_block_item(context, "minecraft:end_portal_frame", "End Portal Frame", ids->dimension_blocks[DIM_FRAME], ids->dimension_states[DIM_FRAME], 0, dimension_frame_placement, 0, &ids->dimension_items[DIM_FRAME]);
    if (maage_status_failed(result)) return result;
    memset(&item, 0, sizeof(item));
    item.behavior_context = ids;
    item.mining_speed = 1;
    item.max_stack = 1;
    item.durability = 64;
    item.use_durability = 1;
    item.model = MAAGE_ITEM_MODEL_SPRITE;
    item.display_name = "Flint and Steel";
    item.use = dimension_ignite;
    memset(&texture, 0, sizeof(texture));
    texture.fallback = MAAGE_CONTENT_ID_INVALID;
    result = maage_presentation_source_register(context->presentation, "minecraft/textures/items/flint_and_steel.png", &texture.source);
    if (!maage_status_failed(result)) result = maage_presentation_texture_register(context->presentation, &texture, &item.texture_id);
    if (!maage_status_failed(result)) result = maage_item_registry_register(context->items, "minecraft:flint_and_steel", &item);
    if (maage_status_failed(result)) return result;
    ids->item_flint_steel = item.id;
    item.max_stack = 64;
    item.durability = 0;
    item.use_durability = 0;
    item.display_name = "Eye of Ender";
    item.use = dimension_eye_use;
    result = maage_presentation_source_register(context->presentation, "minecraft/textures/items/ender_eye.png", &texture.source);
    if (!maage_status_failed(result)) result = maage_presentation_texture_register(context->presentation, &texture, &item.texture_id);
    if (!maage_status_failed(result)) result = maage_item_registry_register(context->items, "minecraft:ender_eye", &item);
    if (maage_status_failed(result)) return result;
    ids->item_ender_eye = item.id;
    memset(&biome, 0, sizeof(biome));
    biome.base_height = 32;
    biome.temperature = 2;
    biome.display_name = "Hell";
    biome.surface = biome.filler = ids->state_netherrack;
    for (i = 0; i < 3; ++i)
        biome.grass_color[i] = biome.foliage_color[i] = 1;
    result = maage_biome_registry_register(context->biomes, "minecraft:hell", &biome);
    if (maage_status_failed(result)) return result;
    ids->biome_hell = biome.id;
    biome.display_name = "The End";
    biome.base_height = 64;
    biome.temperature = 0.5;
    biome.surface = biome.filler = ids->dimension_states[DIM_END_STONE];
    result = maage_biome_registry_register(context->biomes, "minecraft:sky", &biome);
    if (maage_status_failed(result)) return result;
    ids->biome_sky = biome.id;
    memset(&dimension, 0, sizeof(dimension));
    dimension.context = ids;
    dimension.primary = dimension.skylight = dimension.weather = dimension.celestial = 1;
    dimension.coordinate_scale = 1;
    dimension.display_name = "Overworld";
    dimension.generate = dimension_overworld_generate;
    dimension.spawn = dimension_overworld_spawn;
    dimension.contact = dimension_contact;
    dimension.arrive = dimension_arrive;
    result = maage_dimension_register(context->dimensions, "minecraft:overworld", &dimension);
    if (maage_status_failed(result)) return result;
    ids->dimension_overworld = dimension.id;
    dimension.respawn = ids->dimension_overworld;
    dimension.primary = dimension.skylight = dimension.weather = dimension.celestial = 0;
    dimension.coordinate_scale = 8;
    dimension.ambient_light = 0.1;
    dimension.water_evaporates = 1;
    dimension.fog_distance = 96;
    dimension.fog[0] = 0.2f;
    dimension.fog[1] = 0.03f;
    dimension.fog[2] = 0.03f;
    dimension.display_name = "Nether";
    dimension.generate = dimension_nether_generate;
    dimension.spawn = dimension_fixed_spawn;
    result = maage_dimension_register(context->dimensions, "minecraft:the_nether", &dimension);
    if (maage_status_failed(result)) return result;
    ids->dimension_nether = dimension.id;
    dimension.coordinate_scale = 1;
    dimension.ambient_light = 0.1;
    dimension.water_evaporates = 0;
    dimension.fog_distance = 0;
    dimension.fog[0] = 0.075f;
    dimension.fog[1] = 0.065f;
    dimension.fog[2] = 0.095f;
    dimension.display_name = "The End";
    dimension.generate = dimension_end_generate;
    result = maage_dimension_register(context->dimensions, "minecraft:the_end", &dimension);
    if (!maage_status_failed(result)) ids->dimension_end = dimension.id;
    return result;
}
maage_status maage_base_dimension_creative(maage_content_context *context, const maage_base_ids *ids) {
    maage_creative_entry entry;
    maage_creative_tab_id tab;
    maage_status result = maage_creative_tab_resolve(context->creative, "minecraft:building_blocks", &tab);
    unsigned int i;
    if (maage_status_failed(result)) return result;
    for (i = 0; i < 8; ++i) {
        if (i == DIM_PORTAL || i == DIM_END_PORTAL) continue;
        memset(&entry, 0, sizeof(entry));
        entry.tab = tab;
        entry.stack.item_id = i < 6 ? ids->dimension_items[i] : (i == 6 ? ids->item_flint_steel : ids->item_ender_eye);
        entry.stack.count = 1;
        entry.order = entry.search_order = 1900 + i;
        entry.searchable = 1;
        result = maage_creative_entry_register(context->creative, &entry);
        if (maage_status_failed(result)) return result;
    }
    return result;
}
