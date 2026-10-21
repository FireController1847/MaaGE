#include "world_internal.h"
#include <math.h>

#define MAAGE_FEATURE_WORD_MASK 4294967295UL
#define MAAGE_FEATURE_PI 3.14159265358979323846

unsigned long maage_generation_feature_hash(maage_world_seed seed, int x, int z,
    unsigned long salt)
{
    unsigned long value = (seed.low ^ salt ^ 0x471c7a3dUL) & MAAGE_FEATURE_WORD_MASK;
    value ^= (seed.high * 1664525UL) & MAAGE_FEATURE_WORD_MASK;
    value ^= ((unsigned long)x * 374761393UL) & MAAGE_FEATURE_WORD_MASK;
    value ^= ((unsigned long)z * 668265263UL) & MAAGE_FEATURE_WORD_MASK;
    value ^= value >> 16; value = (value * 2146121005UL) & MAAGE_FEATURE_WORD_MASK;
    value ^= value >> 15; value = (value * 2221713035UL) & MAAGE_FEATURE_WORD_MASK;
    value ^= value >> 16;
    return value ? value : 0x6d2b79f5UL;
}
unsigned long maage_generation_feature_next(unsigned long *random)
{
    unsigned long value = *random;
    value ^= (value << 13) & MAAGE_FEATURE_WORD_MASK;
    value ^= value >> 17;
    value ^= (value << 5) & MAAGE_FEATURE_WORD_MASK;
    *random = value & MAAGE_FEATURE_WORD_MASK; return *random;
}
int maage_generation_region(int coordinate, int edge)
{
    int result = coordinate / edge;
    return coordinate % edge < 0 ? result - 1 : result;
}
int maage_generation_write(maage_generation_context *context, int x, int y, int z,
    maage_block_state_id replacement, maage_generation_write_rule rule)
{
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    maage_block_state_id current;
    x -= context->chunk->x * 16; z -= context->chunk->z * 16;
    if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 1 || y >= 256) return 0;
    current = maage_chunk_get(context->chunk, x, y, z);
    if (current == replacement) return 0;
    state = maage_block_registry_state(context->blocks, current);
    block = state ? maage_block_registry_block(context->blocks, state->block_id) : NULL;
    if (!block || block->id == MAAGE_BLOCK_BEDROCK) return 0;
    if (rule == MAAGE_GENERATION_WRITE_STONE && block->id != MAAGE_BLOCK_STONE) return 0;
    if (rule == MAAGE_GENERATION_WRITE_NATURAL && block->id != MAAGE_BLOCK_STONE &&
        block->id != MAAGE_BLOCK_DIRT && block->id != MAAGE_BLOCK_GRASS &&
        block->id != MAAGE_BLOCK_GRAVEL && block->id != MAAGE_BLOCK_SANDSTONE &&
        block->id != MAAGE_BLOCK_SAND && block->id != MAAGE_BLOCK_COAL_ORE &&
        block->id != MAAGE_BLOCK_IRON_ORE && block->id != MAAGE_BLOCK_GOLD_ORE &&
        block->id != MAAGE_BLOCK_REDSTONE_ORE && block->id != MAAGE_BLOCK_LAPIS_ORE &&
        block->id != MAAGE_BLOCK_DIAMOND_ORE && block->id != MAAGE_BLOCK_EMERALD_ORE) return 0;
    if (rule == MAAGE_GENERATION_WRITE_AIR && block->id != MAAGE_BLOCK_AIR) return 0;
    if (rule == MAAGE_GENERATION_WRITE_FOLIAGE && block->id != MAAGE_BLOCK_AIR &&
        block->id != MAAGE_BLOCK_LEAVES && block->id != MAAGE_BLOCK_TALL_GRASS &&
        block->id != MAAGE_BLOCK_DANDELION && block->id != MAAGE_BLOCK_POPPY &&
        block->id != MAAGE_BLOCK_SNOW) return 0;
    context->failure = maage_chunk_set(context->chunk, x, y, z, replacement);
    if (maage_status_failed(context->failure)) {
        MAAGE_STATUS_TRACE(context->trace, context->failure, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET);
        return -1;
    }
    return maage_status_code(context->failure) == MAAGE_STATUS_NOOP ? 0 : 1;
}
int maage_generation_features_valid(const maage_block_registry *blocks)
{
    static const maage_block_state_id required[] = {
        MAAGE_STATE_COAL_ORE, MAAGE_STATE_IRON_ORE, MAAGE_STATE_GOLD_ORE,
        MAAGE_STATE_REDSTONE_ORE, MAAGE_STATE_LAPIS_ORE, MAAGE_STATE_DIAMOND_ORE,
        MAAGE_STATE_EMERALD_ORE, MAAGE_STATE_LAVA, MAAGE_STATE_LOG, MAAGE_STATE_LEAVES,
        MAAGE_STATE_SPRUCE_LOG, MAAGE_STATE_BIRCH_LOG, MAAGE_STATE_SPRUCE_LEAVES,
        MAAGE_STATE_BIRCH_LEAVES, MAAGE_STATE_TALL_GRASS, MAAGE_STATE_DEAD_BUSH,
        MAAGE_STATE_DANDELION, MAAGE_STATE_POPPY, MAAGE_STATE_BROWN_MUSHROOM,
        MAAGE_STATE_RED_MUSHROOM, MAAGE_STATE_SNOW, MAAGE_STATE_CACTUS,
        MAAGE_STATE_COBBLESTONE, MAAGE_STATE_MOSSY_COBBLESTONE
    };
    size_t index;
    for (index = 0; index < sizeof(required) / sizeof(required[0]); ++index)
        if (!maage_block_registry_state(blocks, required[index])) return 0;
    return 1;
}

/* Each region owns finite tunnels; every intersecting column replays the same
 * complete paths and clips writes. Region halo 2 covers all 88-block paths. */
static int maage_feature_carve_ball(maage_generation_context *context,
    double cx, double cy, double cz, double rx, double ry, double rz)
{
    double dx, dy, dz;
    int x, y, z, low_x, high_x, low_z, high_z, low_y, high_y, result, changed = 0;
    int origin_x = context->chunk->x * 16, origin_z = context->chunk->z * 16;
    low_x = (int)floor(cx - rx); high_x = (int)floor(cx + rx);
    low_z = (int)floor(cz - rz); high_z = (int)floor(cz + rz);
    if (high_x < origin_x || low_x >= origin_x + 16 || high_z < origin_z || low_z >= origin_z + 16) return 0;
    if (low_x < origin_x) low_x = origin_x;
    if (high_x >= origin_x + 16) high_x = origin_x + 15;
    if (low_z < origin_z) low_z = origin_z;
    if (high_z >= origin_z + 16) high_z = origin_z + 15;
    low_y = (int)floor(cy - ry); high_y = (int)floor(cy + ry);
    if (low_y < 5) low_y = 5;
    if (high_y > 248) high_y = 248;
    for (z = low_z; z <= high_z; ++z) for (x = low_x; x <= high_x; ++x) {
        dx = (x + 0.5 - cx) / rx; dz = (z + 0.5 - cz) / rz;
        if (dx * dx + dz * dz >= 1.0) continue;
        for (y = low_y; y <= high_y; ++y) {
            if (y > context->surface[(z - origin_z) * 16 + x - origin_x] - 3) continue;
            dy = (y + 0.5 - cy) / ry;
            if (dx * dx + dy * dy + dz * dz >= 1.0) continue;
            result = maage_generation_write(context, x, y, z,
                y < 10 ? MAAGE_STATE_LAVA : MAAGE_STATE_AIR, MAAGE_GENERATION_WRITE_NATURAL);
            if (result < 0) return -1;
            changed |= result;
        }
    }
    return changed;
}
static int maage_feature_tunnel(maage_generation_context *context, unsigned long random,
    double x, double y, double z, double angle, double pitch, int length, double radius)
{
    int step, result, changed = 0;
    double shape;
    for (step = 0; step < length; ++step) {
        if (context->token && !(step & 7) && maage_job_cancelled(context->token)) return -1;
        shape = radius * (0.7 + 0.3 * sin(MAAGE_FEATURE_PI * step / length));
        result = maage_feature_carve_ball(context, x, y, z, shape, shape * 0.72, shape);
        if (result < 0) return -1;
        changed |= result;
        x += sin(angle) * cos(pitch) * 1.25;
        z += cos(angle) * cos(pitch) * 1.25;
        y += sin(pitch) * 1.25;
        angle += ((int)(maage_generation_feature_next(&random) % 201UL) - 100) * 0.0018;
        pitch = pitch * 0.78 + ((int)(maage_generation_feature_next(&random) % 201UL) - 100) * 0.0007;
    }
    return changed;
}
maage_status maage_generation_carve(maage_generation_context *context)
{
    int rx, rz, center_x, center_z, count, branch, result, changed;
    double x, y, z, angle;
    unsigned long random, branch_random;
    center_x = maage_generation_region(context->chunk->x * 16, 64);
    center_z = maage_generation_region(context->chunk->z * 16, 64);
    for (rz = center_z - 2; rz <= center_z + 2; ++rz) for (rx = center_x - 2; rx <= center_x + 2; ++rx) {
        if (context->token && maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CAVES);
        random = maage_generation_feature_hash(context->seed, rx, rz, 10001UL);
        if (maage_generation_feature_next(&random) % 3UL) continue;
        x = rx * 64 + (int)(maage_generation_feature_next(&random) % 64UL);
        z = rz * 64 + (int)(maage_generation_feature_next(&random) % 64UL);
        y = 12 + (int)(maage_generation_feature_next(&random) % 54UL);
        count = 2 + (int)(maage_generation_feature_next(&random) % 3UL);
        changed = 0;
        result = maage_feature_carve_ball(context, x, y, z, 5.0, 2.8, 5.0);
        if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CAVES);
        changed |= result;
        for (branch = 0; branch < count; ++branch) {
            angle = (maage_generation_feature_next(&random) % 65536UL) * (2.0 * MAAGE_FEATURE_PI / 65536.0);
            branch_random = maage_generation_feature_next(&random);
            result = maage_feature_tunnel(context, branch_random, x, y, z, angle,
                ((int)(branch_random % 101UL) - 50) * 0.004, 40 + (int)(branch_random % 25UL),
                1.6 + (branch_random % 100UL) * 0.014);
            if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CAVES);
            changed |= result;
        }
        if (changed) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_CAVES];
        /* A separate rare narrow, vertically stretched path forms a ravine. */
        random = maage_generation_feature_hash(context->seed, rx, rz, 10002UL);
        if (!(random % 7UL)) {
            angle = (maage_generation_feature_next(&random) % 65536UL) * (2.0 * MAAGE_FEATURE_PI / 65536.0);
            changed = 0;
            for (branch = 0; branch < 60; ++branch) {
                if (context->token && !(branch & 7) && maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CAVES);
                result = maage_feature_carve_ball(context, x + sin(angle) * (branch - 30),
                    24 + (random % 24UL), z + cos(angle) * (branch - 30),
                    2.0 + sin(branch * MAAGE_FEATURE_PI / 60.0), 10.0, 2.6);
                if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CAVES);
                changed |= result;
            }
            if (changed) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_CAVES];
        }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CAVES, 0);
}

typedef struct maage_ore_feature {
    maage_block_state_id state;
    unsigned int attempts, size, minimum_y, maximum_y;
    maage_biome_id biome;
    int biome_only, triangular;
} maage_ore_feature;
/* Classic pre-1.18 height bands and approximate 1.7.10 attempt/vein sizes. */
static const maage_ore_feature maage_ore_features[] = {
    {MAAGE_STATE_DIRT,20,32,0,256,0,0,0}, {MAAGE_STATE_GRAVEL,10,32,0,256,0,0,0},
    {MAAGE_STATE_COAL_ORE,20,17,0,128,0,0,0}, {MAAGE_STATE_IRON_ORE,20,9,0,64,0,0,0},
    {MAAGE_STATE_GOLD_ORE,2,9,0,32,0,0,0}, {MAAGE_STATE_REDSTONE_ORE,8,8,0,16,0,0,0},
    {MAAGE_STATE_DIAMOND_ORE,1,8,0,16,0,0,0}, {MAAGE_STATE_LAPIS_ORE,1,7,0,32,0,0,1},
    {MAAGE_STATE_EMERALD_ORE,5,1,4,32,MAAGE_BIOME_HILLS,1,0}
};
static int maage_feature_vein(maage_generation_context *context,
    const maage_ore_feature *feature, int cx, int cy, int cz, unsigned long random)
{
    double angle, x1, x2, z1, z2, y1, y2, fraction, x, y, z, radius, dx, dy, dz;
    int step, bx, by, bz, min_x, max_x, min_z, max_z, min_y, max_y, result, changed = 0;
    int origin_x = context->chunk->x * 16, origin_z = context->chunk->z * 16;
    if (feature->size == 1)
        return maage_generation_write(context, cx, cy, cz, feature->state, MAAGE_GENERATION_WRITE_STONE);
    angle = (maage_generation_feature_next(&random) % 65536UL) * MAAGE_FEATURE_PI / 65536.0;
    x1 = cx + sin(angle) * feature->size / 8.0; x2 = cx - sin(angle) * feature->size / 8.0;
    z1 = cz + cos(angle) * feature->size / 8.0; z2 = cz - cos(angle) * feature->size / 8.0;
    y1 = cy - (int)(maage_generation_feature_next(&random) % 3UL);
    y2 = cy - (int)(maage_generation_feature_next(&random) % 3UL);
    for (step = 0; step <= (int)feature->size; ++step) {
        fraction = step / (double)feature->size;
        x = x1 + (x2 - x1) * fraction; z = z1 + (z2 - z1) * fraction; y = y1 + (y2 - y1) * fraction;
        radius = (1.0 + (sin(MAAGE_FEATURE_PI * fraction) + 1.0) *
            (maage_generation_feature_next(&random) % 1000UL) * feature->size / 16000.0) * 0.5;
        min_x = (int)floor(x - radius); max_x = (int)floor(x + radius);
        min_z = (int)floor(z - radius); max_z = (int)floor(z + radius);
        min_y = (int)floor(y - radius); max_y = (int)floor(y + radius);
        if (min_x < origin_x) min_x = origin_x;
        if (max_x > origin_x + 15) max_x = origin_x + 15;
        if (min_z < origin_z) min_z = origin_z;
        if (max_z > origin_z + 15) max_z = origin_z + 15;
        if (min_y < 1) min_y = 1;
        if (max_y > 255) max_y = 255;
        for (bz = min_z; bz <= max_z; ++bz) for (bx = min_x; bx <= max_x; ++bx) for (by = min_y; by <= max_y; ++by) {
            dx = (bx + 0.5 - x) / radius; dy = (by + 0.5 - y) / radius; dz = (bz + 0.5 - z) / radius;
            if (dx * dx + dy * dy + dz * dz >= 1.0) continue;
            result = maage_generation_write(context, bx, by, bz, feature->state, MAAGE_GENERATION_WRITE_STONE);
            if (result < 0) return -1;
            changed |= result;
        }
    }
    return changed;
}
maage_status maage_generation_underground(maage_generation_context *context)
{
    const maage_ore_feature *feature;
    unsigned long random;
    size_t index;
    maage_biome_id biome;
    int ox, oz, attempt, x, y, z, height, result, local_x, local_z, changed, dx, dy, dz, lava;
    double distance;
    for (oz = context->chunk->z - 1; oz <= context->chunk->z + 1; ++oz)
        for (ox = context->chunk->x - 1; ox <= context->chunk->x + 1; ++ox) {
            if (context->token && maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_ORES);
            for (index = 0; index < sizeof(maage_ore_features) / sizeof(maage_ore_features[0]); ++index) {
                if (context->token && maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_ORES);
                feature = &maage_ore_features[index];
                random = maage_generation_feature_hash(context->seed, ox, oz, 11000UL + (unsigned long)index);
                for (attempt = 0; attempt < (int)feature->attempts; ++attempt) {
                    x = ox * 16 + (int)(maage_generation_feature_next(&random) % 16UL);
                    z = oz * 16 + (int)(maage_generation_feature_next(&random) % 16UL);
                    y = feature->minimum_y + (int)(maage_generation_feature_next(&random) %
                        (feature->maximum_y - feature->minimum_y));
                    if (feature->triangular) y = (y + (int)(maage_generation_feature_next(&random) % 32UL)) / 2;
                    if (feature->biome_only && (!maage_generation_surface(context->seed, context->biomes, x, z, &height, &biome) ||
                        biome != feature->biome)) continue;
                    result = maage_feature_vein(context, feature, x, y, z, maage_generation_feature_next(&random));
                    if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_ORES);
                    if (result) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_UNDERGROUND];
                }
            }
            random = maage_generation_feature_hash(context->seed, ox, oz, 12000UL);
            if (random % 4UL) continue;
            x = ox * 16 + (int)(maage_generation_feature_next(&random) % 16UL);
            z = oz * 16 + (int)(maage_generation_feature_next(&random) % 16UL);
            if (!maage_generation_surface(context->seed, context->biomes, x, z, &height, &biome) ||
                biome == MAAGE_BIOME_OCEAN || height <= context->policy->sea_level) continue;
            lava = (random & 32UL) != 0;
            y = lava ? 8 + (int)(maage_generation_feature_next(&random) % 24UL) : height - 2;
            changed = 0;
            for (dz = -4; dz <= 4; ++dz) for (dx = -4; dx <= 4; ++dx) for (dy = -2; dy <= 2; ++dy) {
                distance = dx * dx / 16.0 + dz * dz / 12.0 + dy * dy / 4.0;
                if (distance >= 1.0) continue;
                local_x = x + dx - context->chunk->x * 16; local_z = z + dz - context->chunk->z * 16;
                if (local_x < 0 || local_x >= 16 || local_z < 0 || local_z >= 16 || y + dy < 5 ||
                    y + dy >= context->surface[local_z * 16 + local_x] - 1) continue;
                result = maage_generation_write(context, x + dx, y + dy, z + dz,
                    dy >= 0 ? MAAGE_STATE_AIR : (lava ? MAAGE_STATE_LAVA : MAAGE_STATE_WATER),
                    MAAGE_GENERATION_WRITE_NATURAL);
                if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_ORES);
                changed |= result;
            }
            if (changed) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_UNDERGROUND];
        }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ORES, 0);
}
