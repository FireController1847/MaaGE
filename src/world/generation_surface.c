#include "world_internal.h"
#include <stdlib.h>

static int maage_surface_cold(const maage_generation_context *context,
                              maage_biome_id biome, int height) {
    const maage_biome_definition *definition = maage_biome_registry_get(context->biomes, biome);
    return definition && definition->temperature -
                                 (height > 64 ? (height - 64) / 600.0 : 0.0) <
                             0.15;
}
static int maage_cactus_height(const maage_generation_context *context,
                               int x, int ground, int z, unsigned long random) {
    int dx, dz, height;
    maage_biome_id biome;
    if (x % 2 || z % 2 || random % 16UL) return 0;
    for (dz = -1; dz <= 1; ++dz)
        for (dx = -1; dx <= 1; ++dx) {
            if (abs(dx) + abs(dz) != 1) continue;
            if (!maage_generation_surface(context->seed, context->biomes, x + dx, z + dz,
                                          &height, &biome) ||
                height > ground) return 0;
        }
    return 1 + (int) ((random >> 8) % 3UL);
}
maage_status maage_generation_decorate(maage_generation_context *context) {
    maage_block_state_id state;
    maage_biome_id biome;
    unsigned long random;
    int x, z, y, wx, wz, height, result, cactus;
    for (z = 0; z < 16; ++z) {
        if (context->token && maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SURFACE);
        for (x = 0; x < 16; ++x) {
            wx = context->chunk->x * 16 + x;
            wz = context->chunk->z * 16 + z;
            height = context->surface[z * 16 + x];
            biome = context->chunk->biomes[z * 16 + x];
            if (height <= context->policy->sea_level || height >= 252) continue;
            random = maage_generation_feature_hash(context->seed, wx, wz, 13000UL);
            state = MAAGE_STATE_AIR;
            if (biome == MAAGE_BIOME_DESERT) {
                cactus = maage_cactus_height(context, wx, height, wz, random);
                if (cactus) {
                    for (y = 1; y <= cactus; ++y) {
                        result = maage_generation_write(context, wx, height + y, wz,
                                                        MAAGE_STATE_CACTUS, MAAGE_GENERATION_WRITE_AIR);
                        if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SURFACE);
                    }
                    ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_SURFACE];
                } else if (!(random % 43UL))
                    state = MAAGE_STATE_DEAD_BUSH;
            } else if (maage_surface_cold(context, biome, height))
                state = MAAGE_STATE_SNOW;
            else if (biome != MAAGE_BIOME_OCEAN) {
                if (!(random % 97UL))
                    state = (random & 256UL) ? MAAGE_STATE_DANDELION : MAAGE_STATE_POPPY;
                else if (!(random % 4UL))
                    state = MAAGE_STATE_TALL_GRASS;
            }
            if (state != MAAGE_STATE_AIR) {
                result = maage_generation_write(context, wx, height + 1, wz, state, MAAGE_GENERATION_WRITE_AIR);
                if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SURFACE);
                if (result) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_SURFACE];
            }
        }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SURFACE, 0);
}

typedef struct maage_tree_feature {
    unsigned int id;
    maage_block_state_id trunk, leaves;
    int minimum_height, height_variation, radius, conical;
} maage_tree_feature;
static const maage_tree_feature maage_tree_features[] = {
    {1U, MAAGE_STATE_LOG, MAAGE_STATE_LEAVES, 4, 3, 2, 0},
    {2U, MAAGE_STATE_BIRCH_LOG, MAAGE_STATE_BIRCH_LEAVES, 5, 3, 2, 0},
    {3U, MAAGE_STATE_SPRUCE_LOG, MAAGE_STATE_SPRUCE_LEAVES, 6, 4, 3, 1}};
static int maage_tree_radius(const maage_tree_feature *feature, int height, int dy) {
    int radius;
    if (!feature->conical) return dy >= height - 1 ? 1 : feature->radius;
    radius = (height - dy + 1) / 2;
    if (radius > feature->radius) radius = feature->radius;
    if (dy < height - 1 && !(dy & 1) && radius > 1) --radius;
    return radius;
}
static int maage_tree_leaf(const maage_tree_feature *feature,
                           int height, int dy, int dx, int dz, unsigned long random) {
    int radius = maage_tree_radius(feature, height, dy);
    if (abs(dx) > radius || abs(dz) > radius) return 0;
    return !(radius > 0 && abs(dx) == radius && abs(dz) == radius &&
             (feature->conical || dy == height || ((random + dx * dx + dz * dz + dy) & 1UL)));
}
static int maage_tree_place(maage_generation_context *context,
                            const maage_tree_feature *feature, int x, int ground, int z, unsigned long random) {
    int height, dy, dx, dz, radius, result, changed = 0;
    height = feature->minimum_height + (int) (random % (unsigned long) feature->height_variation);
    if (ground + height + 2 >= 256) return 0;
    for (dy = feature->conical ? 2 : height - 3; dy <= height; ++dy) {
        radius = maage_tree_radius(feature, height, dy);
        for (dz = -radius; dz <= radius; ++dz)
            for (dx = -radius; dx <= radius; ++dx) {
                if (!maage_tree_leaf(feature, height, dy, dx, dz, random)) continue;
                result = maage_generation_write(context, x + dx, ground + 1 + dy, z + dz,
                                                feature->leaves, MAAGE_GENERATION_WRITE_FOLIAGE);
                if (result < 0) return -1;
                changed |= result;
            }
    }
    for (dy = 1; dy <= height; ++dy) {
        result = maage_generation_write(context, x, ground + dy, z, feature->trunk, MAAGE_GENERATION_WRITE_FOLIAGE);
        if (result < 0) return -1;
        changed |= result;
    }
    result = maage_generation_write(context, x, ground, z, MAAGE_STATE_DIRT, MAAGE_GENERATION_WRITE_ANY);
    return result < 0 ? -1 : changed;
}
static int maage_tree_candidate(const maage_generation_context *context, int gx, int gz,
                                int *x, int *height, int *z, const maage_tree_feature **tree, unsigned long *random) {
    maage_biome_id biome, sample_biome;
    int dx, dz, sample_height, chance;
    *random = maage_generation_feature_hash(context->seed, gx, gz, 14000UL);
    *x = gx * 8 + (int) (maage_generation_feature_next(random) % 8UL);
    *z = gz * 8 + (int) (maage_generation_feature_next(random) % 8UL);
    if (!maage_generation_surface(context->seed, context->biomes, *x, *z, height, &biome) ||
        *height <= context->policy->sea_level || *height >= 240) return 0;
    chance = biome == MAAGE_BIOME_FOREST ? 80 : (biome == MAAGE_BIOME_TAIGA ? 70 : (biome == MAAGE_BIOME_PLAINS ? 4 : (biome == MAAGE_BIOME_HILLS ? 9 : 0)));
    if ((int) (maage_generation_feature_next(random) % 100UL) >= chance) return 0;
    for (dz = -1; dz <= 1; ++dz)
        for (dx = -1; dx <= 1; ++dx)
            if (!maage_generation_surface(context->seed, context->biomes, *x + dx, *z + dz,
                                          &sample_height, &sample_biome) ||
                abs(sample_height - *height) > 1) return 0;
    *tree = &maage_tree_features[biome == MAAGE_BIOME_TAIGA ? 2 : (biome == MAAGE_BIOME_FOREST && *random % 5UL == 0 ? 1 : 0)];
    maage_generation_feature_next(random);
    return 1;
}
int maage_generation_spawn_clear(maage_world_seed seed, const maage_biome_registry *biomes,
                                 const maage_generation_policy *policy, int x, int z) {
    maage_generation_context context;
    const maage_tree_feature *tree;
    unsigned long random;
    int gx, gz, tx, tz, height;
    context.seed = seed;
    context.biomes = biomes;
    context.policy = policy;
    random = maage_generation_feature_hash(seed, x, z, 13000UL);
    if (!(x % 2) && !(z % 2) && !(random % 16UL)) return 0;
    for (gz = maage_generation_region(z, 8) - 1; gz <= maage_generation_region(z, 8) + 1; ++gz)
        for (gx = maage_generation_region(x, 8) - 1; gx <= maage_generation_region(x, 8) + 1; ++gx)
            if (maage_tree_candidate(&context, gx, gz, &tx, &height, &tz, &tree, &random) &&
                abs(tx - x) <= tree->radius && abs(tz - z) <= tree->radius) return 0;
    return 1;
}
int maage_generation_spawn_floor(maage_world_seed seed, const maage_biome_registry *biomes,
                                 const maage_generation_policy *policy, int x, int z, double *feet_y) {
    maage_generation_context context;
    const maage_tree_feature *tree;
    unsigned long random;
    maage_biome_id biome;
    int gx, gz, tx, tz, ground, tree_ground, height, dy, dx, dz, top, cactus;
    context.seed = seed;
    context.biomes = biomes;
    context.policy = policy;
    if (!feet_y || !maage_generation_surface(seed, biomes, x, z, &ground, &biome)) return 0;
    top = ground;
    if (biome == MAAGE_BIOME_DESERT && ground > policy->sea_level && ground < 252) {
        random = maage_generation_feature_hash(seed, x, z, 13000UL);
        cactus = maage_cactus_height(&context, x, ground, z, random);
        if (cactus) top = ground + cactus;
    }
    for (gz = maage_generation_region(z, 8) - 1; gz <= maage_generation_region(z, 8) + 1; ++gz)
        for (gx = maage_generation_region(x, 8) - 1; gx <= maage_generation_region(x, 8) + 1; ++gx) {
            if (!maage_tree_candidate(&context, gx, gz, &tx, &tree_ground, &tz, &tree, &random)) continue;
            height = tree->minimum_height + (int) (random % (unsigned long) tree->height_variation);
            dx = x - tx;
            dz = z - tz;
            if (!dx && !dz && tree_ground + height > top) top = tree_ground + height;
            for (dy = tree->conical ? 2 : height - 3; dy <= height; ++dy) {
                if (!maage_tree_leaf(tree, height, dy, dx, dz, random)) continue;
                if (tree_ground + 1 + dy > top) top = tree_ground + 1 + dy;
            }
        }
    *feet_y = top + 1.0;
    if (ground > policy->sea_level && maage_surface_cold(&context, biome, ground)) *feet_y += 0.125;
    return 1;
}
maage_status maage_generation_vegetation(maage_generation_context *context) {
    const maage_tree_feature *tree;
    unsigned long random;
    maage_biome_id biome;
    int gx, gz, x, z, height, result, top, local_x, local_z;
    for (gz = context->chunk->z * 2 - 1; gz <= context->chunk->z * 2 + 2; ++gz)
        for (gx = context->chunk->x * 2 - 1; gx <= context->chunk->x * 2 + 2; ++gx) {
            if (context->token && maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_VEGETATION);
            if (!maage_tree_candidate(context, gx, gz, &x, &height, &z, &tree, &random)) continue;
            result = maage_tree_place(context, tree, x, height, z, random);
            if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_VEGETATION);
            if (result) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_VEGETATION];
        }
    /* Snow rests on the final canopy; occasional forest-floor mushrooms use
     * the actual private column after tree placement rather than assumed shade. */
    for (local_z = 0; local_z < 16; ++local_z)
        for (local_x = 0; local_x < 16; ++local_x) {
            x = context->chunk->x * 16 + local_x;
            z = context->chunk->z * 16 + local_z;
            height = context->surface[local_z * 16 + local_x];
            biome = context->chunk->biomes[local_z * 16 + local_x];
            top = 254;
            while (top > height + 1 && maage_chunk_get(context->chunk, local_x, top, local_z) == MAAGE_STATE_AIR)
                --top;
            random = maage_generation_feature_hash(context->seed, x, z, 14001UL);
            if (height > context->policy->sea_level && maage_surface_cold(context, biome, height) && top > height + 1 && top < 255) {
                result = maage_generation_write(context, x, top + 1, z, MAAGE_STATE_SNOW, MAAGE_GENERATION_WRITE_AIR);
                if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_VEGETATION);
            } else if (biome == MAAGE_BIOME_FOREST && !(random % 73UL)) {
                for (top = height + 2; top < height + 12 && top < 255; ++top) {
                    if (maage_chunk_get(context->chunk, local_x, top, local_z) == MAAGE_STATE_LEAVES ||
                        maage_chunk_get(context->chunk, local_x, top, local_z) == MAAGE_STATE_BIRCH_LEAVES) {
                        result = maage_generation_write(context, x, height + 1, z,
                                                        (random & 256UL) ? MAAGE_STATE_BROWN_MUSHROOM : MAAGE_STATE_RED_MUSHROOM,
                                                        MAAGE_GENERATION_WRITE_AIR);
                        if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_VEGETATION);
                        break;
                    }
                }
            }
        }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_VEGETATION, 0);
}

typedef struct maage_structure_feature {
    unsigned int id;
    int region_edge, chance, half_width, half_depth, height, minimum_y, maximum_y;
    maage_block_state_id wall, floor;
} maage_structure_feature;
static const maage_structure_feature maage_dungeon_feature = {
    MAAGE_STRUCTURE_DUNGEON, 64, 3, 4, 4, 5, 16, 48,
    MAAGE_STATE_COBBLESTONE, MAAGE_STATE_MOSSY_COBBLESTONE};
static int maage_structure_box(maage_generation_context *context,
                               const maage_structure_feature *feature, int cx, int cy, int cz, unsigned long random) {
    maage_block_state_id state;
    int x, y, z, result, changed = 0, doorway;
    doorway = (int) (random % 4UL);
    for (z = -feature->half_depth; z <= feature->half_depth; ++z)
        for (x = -feature->half_width; x <= feature->half_width; ++x)
            for (y = 0; y <= feature->height; ++y) {
                state = MAAGE_STATE_AIR;
                if (!y)
                    state = maage_generation_feature_hash(context->seed, cx + x, cz + z,
                                                          (unsigned long) cy + 15001UL) %
                                    4UL
                                ? feature->floor
                                : feature->wall;
                else if (y == feature->height || abs(x) == feature->half_width || abs(z) == feature->half_depth)
                    state = feature->wall;
                if (y >= 1 && y <= 2 && ((doorway < 2 && abs(x) <= 1 && z == (doorway ? feature->half_depth : -feature->half_depth)) || (doorway >= 2 && abs(z) <= 1 && x == (doorway == 2 ? -feature->half_width : feature->half_width))))
                    state = MAAGE_STATE_AIR;
                result = maage_generation_write(context, cx + x, cy + y, cz + z, state, MAAGE_GENERATION_WRITE_ANY);
                if (result < 0) return -1;
                changed |= result;
            }
    return changed;
}
maage_status maage_generation_structures(maage_generation_context *context) {
    const maage_structure_feature *feature = &maage_dungeon_feature;
    unsigned long random;
    maage_biome_id biome;
    int rx, rz, center_x, center_z, x, y, z, height, result, dx, dz, fit;
    center_x = maage_generation_region(context->chunk->x * 16, feature->region_edge);
    center_z = maage_generation_region(context->chunk->z * 16, feature->region_edge);
    for (rz = center_z - 1; rz <= center_z + 1; ++rz)
        for (rx = center_x - 1; rx <= center_x + 1; ++rx) {
            if (context->token && maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_STRUCTURES);
            random = maage_generation_feature_hash(context->seed, rx, rz, 15000UL);
            if (random % (unsigned long) feature->chance) continue;
            x = rx * feature->region_edge + (int) (maage_generation_feature_next(&random) % (unsigned long) feature->region_edge);
            z = rz * feature->region_edge + (int) (maage_generation_feature_next(&random) % (unsigned long) feature->region_edge);
            y = feature->minimum_y + (int) (maage_generation_feature_next(&random) %
                                            (unsigned long) (feature->maximum_y - feature->minimum_y));
            fit = 1;
            for (dz = -feature->half_depth; dz <= feature->half_depth; dz += feature->half_depth)
                for (dx = -feature->half_width; dx <= feature->half_width; dx += feature->half_width) {
                    if (!maage_generation_surface(context->seed, context->biomes, x + dx, z + dz,
                                                  &height, &biome) ||
                        height <= y + feature->height + 4) fit = 0;
                }
            if (!fit) continue;
            result = maage_structure_box(context, feature, x, y, z, random);
            if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_STRUCTURES);
            if (result) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_STRUCTURES];
        }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_STRUCTURES, 0);
}
