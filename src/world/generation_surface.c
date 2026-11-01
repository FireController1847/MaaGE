#include "world_internal.h"
#include <stdlib.h>
#include <string.h>

static double maage_surface_unit(unsigned long random) {
    return (random & 16777215UL) / 16777216.0;
}
static const maage_generation_sample *maage_surface_sample(const maage_generation_context *context, int x, int z, maage_generation_sample *sample) {
    int local_x, local_z;
    if (context->chunk && context->columns) {
        local_x = x - context->chunk->x * 16;
        local_z = z - context->chunk->z * 16;
        if (local_x >= 0 && local_x < 16 && local_z >= 0 && local_z < 16) return &context->columns[local_z * 16 + local_x];
    }
    return maage_generation_sample_at(context->seed, context->biomes, context->policy, x, z, sample, context->regions) ? sample : NULL;
}
static int maage_cactus_height(const maage_generation_context *context, int x, int ground, int z, unsigned long random) {
    const maage_builtin_ids *ids = maage_block_registry_builtin_ids(context->blocks);
    maage_generation_sample sample, adjacent;
    const maage_generation_sample *column = maage_surface_sample(context, x, z, &sample), *neighbor;
    const maage_block_state_definition *floor, *neighbor_floor;
    unsigned long other;
    int dx, dz;
    if (!column || column->height != ground || ground <= context->policy->sea_level || ground >= 252 || (column->rainfall > 0 && maage_generation_cold(column, ground + 1)) || maage_surface_unit(random) >= column->decoration.cacti) return 0;
    floor = maage_block_registry_state(context->blocks, column->surface);
    if (!floor || floor->block_id != ids->block_sand) return 0;
    /* Coordinate priority gives irregular spacing without touching a neighbor
     * chunk. Every candidate rejects an adjacent higher-priority candidate. */
    for (dz = -1; dz <= 1; ++dz)
        for (dx = -1; dx <= 1; ++dx) {
            if (abs(dx) + abs(dz) != 1) continue;
            neighbor = maage_surface_sample(context, x + dx, z + dz, &adjacent);
            if (!neighbor || neighbor->height > ground) return 0;
            neighbor_floor = maage_block_registry_state(context->blocks, neighbor->surface);
            if (!neighbor_floor) return 0;
            other = maage_generation_feature_hash(context->seed, x + dx, z + dz, 13000UL);
            if (neighbor->height == ground && neighbor_floor->block_id == ids->block_sand && !(neighbor->rainfall > 0 && maage_generation_cold(neighbor, ground + 1)) && maage_surface_unit(other) < neighbor->decoration.cacti && (other < random || (other == random && (dx < 0 || dz < 0)))) return 0;
        }
    return 1 + (int) ((random >> 24) % 3UL);
}
maage_status maage_generation_decorate(maage_generation_context *context) {
    const maage_builtin_ids *ids = maage_block_registry_builtin_ids(context->blocks);
    const maage_generation_sample *column;
    const maage_block_state_definition *floor;
    maage_block_state_id state;
    unsigned long random;
    int x, z, y, wx, wz, height, result, cactus;
    for (z = 0; z < 16; ++z) {
        if (context->token && maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SURFACE);
        for (x = 0; x < 16; ++x) {
            wx = context->chunk->x * 16 + x;
            wz = context->chunk->z * 16 + z;
            column = &context->columns[z * 16 + x];
            height = column->height;
            if (height < context->policy->sea_level && maage_chunk_get(context->chunk, x, context->policy->sea_level, z) == ids->state_water && maage_generation_cold(column, context->policy->sea_level)) {
                result = maage_generation_write(context, wx, context->policy->sea_level, wz, ids->state_ice, MAAGE_GENERATION_WRITE_ANY);
                if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SURFACE);
                if (result) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_SURFACE];
            }
            if (height <= context->policy->sea_level || height >= 252 || (column->rainfall > 0 && maage_generation_cold(column, height + 1))) continue;
            floor = maage_block_registry_state(context->blocks, maage_chunk_get(context->chunk, x, height, z));
            if (!floor) return maage_generation_outcome(context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_SURFACE);
            random = maage_generation_feature_hash(context->seed, wx, wz, 13000UL);
            state = ids->state_air;
            cactus = floor->block_id == ids->block_sand ? maage_cactus_height(context, wx, height, wz, random) : 0;
            if (cactus) {
                for (y = 1; y <= cactus; ++y) {
                    result = maage_generation_write(context, wx, height + y, wz, ids->state_cactus, MAAGE_GENERATION_WRITE_AIR);
                    if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_SURFACE);
                }
                ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_SURFACE];
            } else if ((floor->block_id == ids->block_dirt || floor->block_id == ids->block_sand) && maage_surface_unit(maage_generation_feature_next(&random)) < column->decoration.dead_bushes) state = ids->state_dead_bush;
            else if (floor->block_id == ids->block_grass || floor->block_id == ids->block_dirt) {
                if (maage_surface_unit(maage_generation_feature_next(&random)) < column->decoration.flowers) state = (random & 16777216UL) ? ids->state_dandelion : ids->state_poppy;
                else if (maage_surface_unit(maage_generation_feature_next(&random)) < column->decoration.grass) state = ids->state_tall_grass;
            }
            if (state != ids->state_air) {
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
    size_t trunk, leaves;
    int minimum_height, height_variation, radius, conical, width, branches;
} maage_tree_feature;
static const maage_tree_feature maage_tree_features[] = {
    {1U, offsetof(maage_builtin_ids, state_log), offsetof(maage_builtin_ids, state_leaves), 4, 3, 2, 0, 1, 0},
    {2U, offsetof(maage_builtin_ids, state_birch_log), offsetof(maage_builtin_ids, state_birch_leaves), 5, 3, 2, 0, 1, 0},
    {3U, offsetof(maage_builtin_ids, state_spruce_log), offsetof(maage_builtin_ids, state_spruce_leaves), 6, 4, 3, 1, 1, 0},
    {4U, offsetof(maage_builtin_ids, state_log), offsetof(maage_builtin_ids, state_leaves), 5, 12, 5, 0, 1, 1},
    {5U, offsetof(maage_builtin_ids, state_spruce_log), offsetof(maage_builtin_ids, state_spruce_leaves), 13, 15, 4, 1, 2, 0}};
typedef int (*maage_tree_writer)(void *context, int x, int y, int z, maage_block_state_id state, maage_generation_write_rule rule);
typedef struct maage_tree_generation {
    maage_generation_context *context;
    unsigned short top[256];
    unsigned char snow[256];
    int snowfall;
} maage_tree_generation;
static int maage_tree_height(const maage_tree_feature *feature, unsigned long random) {
    int height = feature->minimum_height + (int) (random % (unsigned long) feature->height_variation);
    if (feature->width == 2) height += (int) ((random >> 8) % 3UL);
    return height;
}
static int maage_tree_radius(const maage_tree_feature *feature, int height, int dy) {
    int radius;
    if (!feature->conical) return dy >= height - 1 ? 1 : (feature->branches ? 2 : feature->radius);
    radius = (height - dy + 1) / 2;
    if (radius > feature->radius) radius = feature->radius;
    if (dy < height - 1 && !(dy & 1) && radius > 1) --radius;
    return radius;
}
static int maage_tree_leaf(const maage_tree_feature *feature, int height, int dy, int dx, int dz, unsigned long random) {
    int radius = maage_tree_radius(feature, height, dy);
    int ax = dx < 0 ? -dx : (dx >= feature->width ? dx - feature->width + 1 : 0);
    int az = dz < 0 ? -dz : (dz >= feature->width ? dz - feature->width + 1 : 0);
    if (ax > radius || az > radius) return 0;
    return !(radius > 0 && ax == radius && az == radius && (feature->conical || dy == height || ((random + ax * ax + az * az + dy) & 1UL)));
}
/* One feature emitter serves detached generation columns and staged live
 * growth. Writers own clipping/capacity; shape selection has no World access. */
static int maage_tree_emit(const maage_builtin_ids *ids, const maage_tree_feature *feature, int height, int x, int ground, int z, unsigned long random, maage_tree_writer write, void *context) {
    static const int directions[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    int dy, dx, dz, radius, result, changed = 0, first, crown, branch, cx, cy, cz, step, steps, bx, by, bz;
    first = feature->conical ? 2 : height - 3;
    if (feature->width == 2) {
        crown = (random & 65536UL ? 13 : 3) + (int) ((random >> 12) % 5UL);
        first = height - crown;
        if (first < 2) first = 2;
    }
    for (dy = first; dy <= height; ++dy) {
        radius = maage_tree_radius(feature, height, dy);
        for (dz = -radius; dz < radius + feature->width; ++dz)
            for (dx = -radius; dx < radius + feature->width; ++dx) {
                if (!maage_tree_leaf(feature, height, dy, dx, dz, random)) continue;
                result = write(context, x + dx, ground + 1 + dy, z + dz, MAAGE_CONTENT_ID(ids, feature->leaves), MAAGE_GENERATION_WRITE_FOLIAGE);
                if (result < 0) return -1;
                changed |= result;
            }
    }
    if (feature->branches)
        for (branch = 0; branch < 8; ++branch) {
            cx = x + directions[branch][0] * 3;
            cz = z + directions[branch][1] * 3;
            cy = ground + height - 2 + (int) ((random >> branch) & 1UL);
            for (dy = -2; dy <= 1; ++dy) {
                radius = dy == 1 ? 1 : 2;
                for (dz = -radius; dz <= radius; ++dz)
                    for (dx = -radius; dx <= radius; ++dx) {
                        if (abs(dx) == radius && abs(dz) == radius) continue;
                        result = write(context, cx + dx, cy + dy, cz + dz, MAAGE_CONTENT_ID(ids, feature->leaves), MAAGE_GENERATION_WRITE_FOLIAGE);
                        if (result < 0) return -1;
                        changed |= result;
                    }
            }
        }
    for (dy = 1; dy <= height; ++dy)
        for (dz = 0; dz < feature->width; ++dz)
            for (dx = 0; dx < feature->width; ++dx) {
                if (feature->width == 2 && dy == height && (dx || dz)) continue;
                result = write(context, x + dx, ground + dy, z + dz, MAAGE_CONTENT_ID(ids, feature->trunk), MAAGE_GENERATION_WRITE_FOLIAGE);
                if (result < 0) return -1;
                changed |= result;
            }
    if (feature->branches)
        for (branch = 0; branch < 8; ++branch) {
            cy = height - 2 + (int) ((random >> branch) & 1UL);
            steps = cy - height / 2;
            if (steps < 3) steps = 3;
            for (step = 1; step <= steps; ++step) {
                bx = directions[branch][0] * 3 * step / steps;
                bz = directions[branch][1] * 3 * step / steps;
                by = height / 2 + (cy - height / 2) * step / steps;
                if (!bx && !bz) continue;
                result = write(context, x + bx, ground + by, z + bz, directions[branch][0] ? ids->logs[4] : ids->logs[8], MAAGE_GENERATION_WRITE_FOLIAGE);
                if (result < 0) return -1;
                changed |= result;
            }
        }
    for (dz = 0; dz < feature->width; ++dz)
        for (dx = 0; dx < feature->width; ++dx) {
            result = write(context, x + dx, ground, z + dz, ids->state_dirt, MAAGE_GENERATION_WRITE_ANY);
            if (result < 0) return -1;
            changed |= result;
        }
    return changed;
}
static int maage_tree_generation_write(void *context, int x, int y, int z, maage_block_state_id state, maage_generation_write_rule rule) {
    maage_tree_generation *trees = (maage_tree_generation *) context;
    int index, result = maage_generation_write(trees->context, x, y, z, state, rule);
    if (result > 0 && rule == MAAGE_GENERATION_WRITE_FOLIAGE) {
        index = (z - trees->context->chunk->z * 16) * 16 + x - trees->context->chunk->x * 16;
        /* Retain the owner of the highest emitted cell. Equal-state writes
         * leave that owner intact, just as they leave the block intact. */
        if (y >= trees->top[index]) {
            trees->top[index] = (unsigned short) y;
            trees->snow[index] = (unsigned char) trees->snowfall;
        }
    }
    return result;
}
static int maage_tree_place(maage_tree_generation *trees, const maage_tree_feature *feature, int x, int ground, int z, unsigned long random, int snowfall) {
    const maage_builtin_ids *ids = maage_block_registry_builtin_ids(trees->context->blocks);
    int height = maage_tree_height(feature, random);
    if (ground + height + 2 >= 256) return 0;
    trees->snowfall = snowfall;
    return maage_tree_emit(ids, feature, height, x, ground, z, random, maage_tree_generation_write, trees);
}

static int maage_tree_plan_write(void *context, int x, int y, int z, maage_block_state_id state, maage_generation_write_rule rule) {
    maage_tree_plan *plan = (maage_tree_plan *) context;
    (void) rule;
    x -= plan->x;
    y -= plan->y;
    z -= plan->z;
    if (x < 0 || x >= plan->edge || z < 0 || z >= plan->edge || y < 0 || y >= plan->height) return -1;
    plan->cells[((size_t) y * plan->edge + z) * plan->edge + x] = state;
    return 1;
}
static int maage_tree_replaceable(const maage_block_state_definition *state) {
    const maage_builtin_ids *ids = state ? state->builtins : NULL;
    return state && (state->block_id == ids->block_air || state->block_id == ids->block_leaves || state->block_id == ids->block_sapling || state->block_id == ids->block_tall_grass || state->block_id == ids->block_dandelion || state->block_id == ids->block_poppy || state->block_id == ids->block_snow);
}
maage_status maage_generation_grow_tree(maage_world *world, int x, int y, int z, unsigned int species, int large, unsigned long random, int *attempted) {
    const maage_builtin_ids *ids = maage_block_registry_builtin_ids(maage_world_registry(world));
    const maage_tree_feature *feature;
    const maage_block_registry *blocks = maage_world_registry(world);
    const maage_block_state_definition *state;
    maage_tree_plan plan;
    maage_block_state_id replacement;
    maage_status result = maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    size_t count;
    int height, radius, dx, dy, dz, cx, cz;
    if (attempted) *attempted = 0;
    if (!world || !attempted || species > 2 || (large != 0 && large != 1) || !random) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    feature = &maage_tree_features[species == 0 ? (large ? 3 : 0) : (species == 1 ? (large ? 4 : 2) : 1)];
    height = maage_tree_height(feature, random);
    if (x - feature->radius - 2 <= -MAAGE_WORLD_LIMIT || z - feature->radius - 2 <= -MAAGE_WORLD_LIMIT || x + feature->radius + feature->width + 1 >= MAAGE_WORLD_LIMIT || z + feature->radius + feature->width + 1 >= MAAGE_WORLD_LIMIT) return result;
    if (y < 1 || y + height + 1 >= MAAGE_WORLD_HEIGHT) {
        *attempted = 1;
        return result;
    }
    memset(&plan, 0, sizeof(plan));
    plan.x = x - feature->radius;
    plan.y = y - 1;
    plan.z = z - feature->radius;
    plan.edge = feature->radius * 2 + feature->width;
    plan.height = height + 2;
    for (cz = maage_world_chunk_coordinate(plan.z - 2); cz <= maage_world_chunk_coordinate(plan.z + plan.edge + 1); ++cz)
        for (cx = maage_world_chunk_coordinate(plan.x - 2); cx <= maage_world_chunk_coordinate(plan.x + plan.edge + 1); ++cx)
            if (!maage_world_chunk_loaded(world, cx, cz)) return result;
    *attempted = 1;
    for (dz = 0; dz < feature->width; ++dz)
        for (dx = 0; dx < feature->width; ++dx) {
            state = maage_block_registry_state(blocks, maage_world_get(world, x + dx, y - 1, z + dz));
            if (!state) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
            if (state->block_id != ids->block_dirt && state->block_id != ids->block_grass) return result;
            state = maage_block_registry_state(blocks, maage_world_get(world, x + dx, y, z + dz));
            if (!state) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
            if (state->block_id != ids->block_sapling || (state->metadata & 7U) != species) return result;
        }
    /* Keep a clear trunk corridor and headroom as well as every emitted cell;
     * a hollow canopy must not permit solid obstacles through its interior. */
    for (dy = 0; dy <= height + 1; ++dy) {
        radius = !dy ? 0 : (dy >= height - 1 ? 2 : 1);
        if (feature->conical && dy >= 2 && dy <= height) radius = maage_tree_radius(feature, height, dy);
        for (dz = -radius; dz < radius + feature->width; ++dz)
            for (dx = -radius; dx < radius + feature->width; ++dx) {
                state = maage_block_registry_state(blocks, maage_world_get(world, x + dx, y + dy, z + dz));
                if (!state) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
                if (!maage_tree_replaceable(state)) return result;
            }
    }
    count = (size_t) plan.edge * plan.edge * plan.height;
    if (count > (size_t) - 1 / sizeof(*plan.cells)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    plan.cells = (maage_block_state_id *) calloc(count, sizeof(*plan.cells));
    if (!plan.cells) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    if (maage_tree_emit(ids, feature, height, x, y - 1, z, random, maage_tree_plan_write, &plan) < 0) {
        result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
        goto done;
    }
    for (dy = 0; dy < plan.height; ++dy)
        for (dz = 0; dz < plan.edge; ++dz)
            for (dx = 0; dx < plan.edge; ++dx) {
                replacement = plan.cells[((size_t) dy * plan.edge + dz) * plan.edge + dx];
                if (!replacement) continue;
                state = maage_block_registry_state(blocks, maage_world_get(world, plan.x + dx, plan.y + dy, plan.z + dz));
                if (!state) {
                    result = maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
                    goto done;
                }
                if (replacement == ids->state_dirt ? (state->block_id != ids->block_dirt && state->block_id != ids->block_grass) : !maage_tree_replaceable(state)) goto done;
            }
    result = maage_world_plant_tree(world, &plan);
done:
    free(plan.cells);
    return result;
}

static int maage_tree_candidate(const maage_generation_context *context, int gx, int gz, int *x, int *height, int *z, const maage_tree_feature **tree, unsigned long *random, int *snowfall) {
    const maage_builtin_ids *ids = maage_block_registry_builtin_ids(context->blocks);
    maage_generation_sample sample;
    const maage_generation_sample *column;
    const maage_block_state_definition *floor;
    maage_biome_id sample_biome;
    double total = 0, rank;
    unsigned int species;
    int dx, dz, sample_height;
    *random = maage_generation_feature_hash(context->seed, gx, gz, 14000UL);
    *x = gx * 8 + (int) (maage_generation_feature_next(random) % 8UL);
    *z = gz * 8 + (int) (maage_generation_feature_next(random) % 8UL);
    column = maage_surface_sample(context, *x, *z, &sample);
    if (!column || column->height <= context->policy->sea_level || column->height >= 240) return 0;
    *height = column->height;
    /* Profile material is reproducible even for a clipped neighboring feature.
     * Trees cannot turn a transitional sand/gravel patch into a dirt pedestal. */
    floor = maage_block_registry_state(context->blocks, column->surface);
    if (!floor || (floor->block_id != ids->block_grass && floor->block_id != ids->block_dirt)) return 0;
    for (species = 0; species < 3; ++species)
        total += column->decoration.trees[species];
    if (total <= 0 || maage_surface_unit(maage_generation_feature_next(random)) >= total) return 0;
    for (dz = -1; dz <= 1; ++dz)
        for (dx = -1; dx <= 1; ++dx)
            if (!maage_generation_surface(context->seed, context->biomes, *x + dx, *z + dz, &sample_height, &sample_biome, context->regions) || abs(sample_height - *height) > 1) return 0;
    rank = maage_surface_unit(maage_generation_feature_next(random)) * total;
    for (species = 0; species < 2 && rank >= column->decoration.trees[species]; ++species)
        rank -= column->decoration.trees[species];
    *tree = &maage_tree_features[species == 1 ? 2 : (species == 2 ? 1 : 0)];
    maage_generation_feature_next(random);
    /* A tree uses its root's shared climate at crown elevation throughout
     * its canopy, including portions replayed into another output column. */
    *snowfall = column->rainfall > 0 && maage_generation_cold(column, *height + maage_tree_height(*tree, *random) + 2);
    return 1;
}
int maage_generation_spawn_clear(maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, maage_generation_region_cache *regions) {
    maage_generation_context context;
    maage_generation_sample column;
    const maage_tree_feature *tree;
    unsigned long random;
    int gx, gz, tx, tz, height, snowfall;
    memset(&context, 0, sizeof(context));
    context.seed = seed;
    context.blocks = blocks;
    context.biomes = biomes;
    context.policy = policy;
    context.regions = regions;
    if (!maage_generation_sample_at(seed, biomes, policy, x, z, &column, regions)) return 0;
    random = maage_generation_feature_hash(seed, x, z, 13000UL);
    if (maage_cactus_height(&context, x, column.height, z, random)) return 0;
    for (gz = maage_generation_region(z, 8) - 1; gz <= maage_generation_region(z, 8) + 1; ++gz)
        for (gx = maage_generation_region(x, 8) - 1; gx <= maage_generation_region(x, 8) + 1; ++gx)
            if (maage_tree_candidate(&context, gx, gz, &tx, &height, &tz, &tree, &random, &snowfall) && abs(tx - x) <= tree->radius && abs(tz - z) <= tree->radius) return 0;
    return 1;
}
typedef struct maage_tree_spawn_column {
    const maage_block_registry *blocks;
    maage_block_state_id state;
    int x, z, top, snowfall, tree_snowfall;
} maage_tree_spawn_column;
static int maage_tree_spawn_write(void *context, int x, int y, int z, maage_block_state_id state, maage_generation_write_rule rule) {
    maage_tree_spawn_column *column = (maage_tree_spawn_column *) context;
    const maage_block_state_definition *current;
    if (x != column->x || z != column->z || y < column->top || rule != MAAGE_GENERATION_WRITE_FOLIAGE) return 0;
    if (y == column->top) {
        current = maage_block_registry_state(column->blocks, column->state);
        if (column->state == state || !maage_tree_replaceable(current)) return 0;
    }
    column->top = y;
    column->state = state;
    column->snowfall = column->tree_snowfall;
    return 1;
}
int maage_generation_spawn_floor(maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, double *feet_y, maage_generation_region_cache *regions) {
    const maage_builtin_ids *ids = maage_block_registry_builtin_ids(blocks);
    maage_generation_context context;
    maage_generation_sample column;
    maage_tree_spawn_column exposed;
    const maage_tree_feature *tree;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    unsigned long random;
    int gx, gz, tx, tz, ground, tree_ground, height, cactus, snowfall;
    memset(&context, 0, sizeof(context));
    context.seed = seed;
    context.blocks = blocks;
    context.biomes = biomes;
    context.policy = policy;
    context.regions = regions;
    if (!feet_y || !maage_generation_sample_at(seed, biomes, policy, x, z, &column, regions)) return 0;
    state = maage_block_registry_state(blocks, column.surface);
    if (!state || !maage_block_registry_block(blocks, state->block_id)) return 0;
    ground = column.height;
    exposed.blocks = blocks;
    exposed.x = x;
    exposed.z = z;
    exposed.top = ground;
    exposed.state = column.surface;
    exposed.snowfall = column.rainfall > 0 && maage_generation_cold(&column, ground + 1);
    exposed.tree_snowfall = 0;
    random = maage_generation_feature_hash(seed, x, z, 13000UL);
    cactus = maage_cactus_height(&context, x, ground, z, random);
    if (cactus) {
        exposed.top = ground + cactus;
        exposed.state = ids->state_cactus;
        exposed.snowfall = 0;
    }
    for (gz = maage_generation_region(z, 8) - 1; gz <= maage_generation_region(z, 8) + 1; ++gz)
        for (gx = maage_generation_region(x, 8) - 1; gx <= maage_generation_region(x, 8) + 1; ++gx) {
            if (!maage_tree_candidate(&context, gx, gz, &tx, &tree_ground, &tz, &tree, &random, &snowfall)) continue;
            height = maage_tree_height(tree, random);
            if (tree_ground + height + 2 >= 256) continue;
            exposed.tree_snowfall = snowfall;
            maage_tree_emit(ids, tree, height, tx, tree_ground, tz, random, maage_tree_spawn_write, &exposed);
        }
    state = maage_block_registry_state(blocks, exposed.state);
    block = state ? maage_block_registry_block(blocks, state->block_id) : NULL;
    if (!block) return 0;
    *feet_y = exposed.top + 1.0;
    if ((ground > policy->sea_level || exposed.top > ground) && exposed.snowfall && state->block_id != ids->block_ice && (state->block_id == ids->block_leaves || (block->opaque && block->material_solid))) *feet_y += 0.125;
    return 1;
}
maage_status maage_generation_vegetation(maage_generation_context *context) {
    const maage_builtin_ids *ids = maage_block_registry_builtin_ids(context->blocks);
    maage_tree_generation trees;
    const maage_tree_feature *tree;
    const maage_generation_sample *column;
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    unsigned long random;
    int gx, gz, x, z, height, result, top, local_x, local_z, index, snowfall;
    memset(&trees, 0, sizeof(trees));
    trees.context = context;
    for (gz = context->chunk->z * 2 - 1; gz <= context->chunk->z * 2 + 2; ++gz)
        for (gx = context->chunk->x * 2 - 1; gx <= context->chunk->x * 2 + 2; ++gx) {
            if (context->token && maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_VEGETATION);
            if (!maage_tree_candidate(context, gx, gz, &x, &height, &z, &tree, &random, &snowfall)) continue;
            result = maage_tree_place(&trees, tree, x, height, z, random, snowfall);
            if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_VEGETATION);
            if (result) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_VEGETATION];
        }
    /* Ground follows local climate/elevation; exposed tree cells retain their
     * feature owner's snow decision instead of slicing the canopy by column.
     * Actual support/shade comes only from this private output. */
    for (local_z = 0; local_z < 16; ++local_z) {
        if (context->token && maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_VEGETATION);
        for (local_x = 0; local_x < 16; ++local_x) {
            x = context->chunk->x * 16 + local_x;
            z = context->chunk->z * 16 + local_z;
            index = local_z * 16 + local_x;
            column = &context->columns[index];
            height = column->height;
            top = 254;
            while (top > height && maage_chunk_get(context->chunk, local_x, top, local_z) == ids->state_air)
                --top;
            state = maage_block_registry_state(context->blocks, maage_chunk_get(context->chunk, local_x, top, local_z));
            block = state ? maage_block_registry_block(context->blocks, state->block_id) : NULL;
            if (!block) return maage_generation_outcome(context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_VEGETATION);
            random = maage_generation_feature_hash(context->seed, x, z, 14001UL);
            snowfall = top == trees.top[index] ? trees.snow[index] : (column->rainfall > 0 && maage_generation_cold(column, top + 1));
            if ((height > context->policy->sea_level || top == trees.top[index]) && snowfall && top < 255 && state->block_id != ids->block_ice && (state->block_id == ids->block_leaves || (block->opaque && block->material_solid))) {
                result = maage_generation_write(context, x, top + 1, z, ids->state_snow, MAAGE_GENERATION_WRITE_AIR);
                if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_VEGETATION);
                if (result) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_VEGETATION];
            } else if (maage_surface_unit(random) < column->decoration.mushrooms) {
                state = maage_block_registry_state(context->blocks, maage_chunk_get(context->chunk, local_x, height, local_z));
                block = state ? maage_block_registry_block(context->blocks, state->block_id) : NULL;
                if (!block || !block->opaque) continue;
                for (top = height + 2; top < height + 12 && top < 255; ++top) {
                    state = maage_block_registry_state(context->blocks, maage_chunk_get(context->chunk, local_x, top, local_z));
                    if (!state) return maage_generation_outcome(context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_VEGETATION);
                    if (state->block_id == ids->block_leaves) {
                        result = maage_generation_write(context, x, height + 1, z, (random & 256UL) ? ids->state_brown_mushroom : ids->state_red_mushroom, MAAGE_GENERATION_WRITE_AIR);
                        if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_VEGETATION);
                        if (result) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_VEGETATION];
                        break;
                    }
                }
            }
        }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_VEGETATION, 0);
}

typedef struct maage_structure_feature {
    unsigned int id;
    int region_edge, chance, half_width, half_depth, height, minimum_y, maximum_y;
    size_t wall, floor;
} maage_structure_feature;
static const maage_structure_feature maage_dungeon_feature = {
    MAAGE_STRUCTURE_DUNGEON, 64, 3, 4, 4, 5, 16, 48,
    offsetof(maage_builtin_ids, state_cobblestone), offsetof(maage_builtin_ids, state_mossy_cobblestone)};
static int maage_structure_box(maage_generation_context *context, const maage_structure_feature *feature, int cx, int cy, int cz, unsigned long random) {
    const maage_builtin_ids *ids = maage_block_registry_builtin_ids(context->blocks);
    maage_block_state_id wall = MAAGE_CONTENT_ID(ids, feature->wall), floor = MAAGE_CONTENT_ID(ids, feature->floor);
    maage_block_state_id state;
    int x, y, z, result, changed = 0, doorway;
    doorway = (int) (random % 4UL);
    for (z = -feature->half_depth; z <= feature->half_depth; ++z)
        for (x = -feature->half_width; x <= feature->half_width; ++x)
            for (y = 0; y <= feature->height; ++y) {
                state = ids->state_air;
                if (!y) state = maage_generation_feature_hash(context->seed, cx + x, cz + z, (unsigned long) cy + 15001UL) % 4UL ? floor : wall;
                else if (y == feature->height || abs(x) == feature->half_width || abs(z) == feature->half_depth) state = wall;
                if (y >= 1 && y <= 2 && ((doorway < 2 && abs(x) <= 1 && z == (doorway ? feature->half_depth : -feature->half_depth)) || (doorway >= 2 && abs(z) <= 1 && x == (doorway == 2 ? -feature->half_width : feature->half_width))))
                    state = ids->state_air;
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
            y = feature->minimum_y + (int) (maage_generation_feature_next(&random) % (unsigned long) (feature->maximum_y - feature->minimum_y));
            fit = 1;
            for (dz = -feature->half_depth; dz <= feature->half_depth; dz += feature->half_depth)
                for (dx = -feature->half_width; dx <= feature->half_width; dx += feature->half_width) {
                    if (!maage_generation_surface(context->seed, context->biomes, x + dx, z + dz, &height, &biome, context->regions) || height <= y + feature->height + 4) fit = 0;
                }
            if (!fit) continue;
            result = maage_structure_box(context, feature, x, y, z, random);
            if (result < 0) return maage_generation_outcome(context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_STRUCTURES);
            if (result) ++context->chunk->generation_features[MAAGE_GENERATION_STAGE_STRUCTURES];
        }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_STRUCTURES, 0);
}
