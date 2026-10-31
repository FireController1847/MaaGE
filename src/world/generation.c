#include "world_internal.h"
#include "maage/jobs.h"

#include <math.h>
#include <string.h>

#define MAAGE_GENERATION_DEFAULT_SEA_Y 63
#define MAAGE_GENERATION_DEFAULT_SPAWN_RADIUS 4096
#define MAAGE_GENERATION_DEFAULT_SPAWN_STEP 16
#define MAAGE_GENERATION_WORD_MASK 4294967295UL
#define MAAGE_GENERATION_BIOME_SCALE 128
#define MAAGE_GENERATION_TRANSITION_MARGIN 0.02
#define MAAGE_GENERATION_REGION_SCALE 128
#define MAAGE_GENERATION_REGION_BLEND 16.0

maage_status maage_generation_policy_default(maage_generation_policy *policy) {
    if (!policy) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
    policy->sea_level = MAAGE_GENERATION_DEFAULT_SEA_Y;
    policy->spawn_search_radius = MAAGE_GENERATION_DEFAULT_SPAWN_RADIUS;
    policy->spawn_search_step = MAAGE_GENERATION_DEFAULT_SPAWN_STEP;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}

int maage_generation_policy_valid(const maage_generation_policy *policy) {
    return policy && policy->sea_level >= 0 && policy->sea_level < MAAGE_WORLD_HEIGHT &&
           policy->spawn_search_radius >= 0 && policy->spawn_search_radius < MAAGE_WORLD_LIMIT &&
           policy->spawn_search_step >= 1 && policy->spawn_search_step < MAAGE_WORLD_LIMIT;
}

/* Original approximation of classic gradient-noise terrain. All randomness
 * is a pure function of the signed seed's bits, coordinates and numeric stage.
 * Masked arithmetic has identical low 32 bits on 32- and 64-bit unsigned long. */
static unsigned long maage_generation_mix(unsigned long value) {
    value &= MAAGE_GENERATION_WORD_MASK;
    value ^= value >> 16;
    value = (value * 2146121005UL) & MAAGE_GENERATION_WORD_MASK;
    value ^= value >> 15;
    value = (value * 2221713035UL) & MAAGE_GENERATION_WORD_MASK;
    value ^= value >> 16;
    return value;
}

static unsigned long maage_generation_hash(maage_world_seed seed, int x, int z, unsigned long stage) {
    unsigned long value;
    value = maage_generation_mix(seed.low ^ stage);
    value = maage_generation_mix(value ^ seed.high);
    value = maage_generation_mix(value ^ (((unsigned long) x * 374761393UL) & MAAGE_GENERATION_WORD_MASK));
    return maage_generation_mix(value ^ (((unsigned long) z * 668265263UL) & MAAGE_GENERATION_WORD_MASK));
}

static double maage_generation_fade(double value) {
    return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
}

static double maage_generation_lerp(double a, double b, double weight) {
    return a + weight * (b - a);
}

static double maage_generation_gradient(unsigned long hash, double x, double z) {
    switch (hash & 7UL) {
    case 0:
        return x + z;
    case 1:
        return -x + z;
    case 2:
        return x - z;
    case 3:
        return -x - z;
    case 4:
        return x;
    case 5:
        return -x;
    case 6:
        return z;
    default:
        return -z;
    }
}

static double maage_generation_noise(maage_world_seed seed, double x, double z, unsigned long stage) {
    int ix, iz;
    double dx, dz, u, v, a, b;
    ix = (int) floor(x);
    iz = (int) floor(z);
    dx = x - ix;
    dz = z - iz;
    u = maage_generation_fade(dx);
    v = maage_generation_fade(dz);
    a = maage_generation_lerp(maage_generation_gradient(maage_generation_hash(seed, ix, iz, stage), dx, dz), maage_generation_gradient(maage_generation_hash(seed, ix + 1, iz, stage), dx - 1.0, dz), u);
    b = maage_generation_lerp(maage_generation_gradient(maage_generation_hash(seed, ix, iz + 1, stage), dx, dz - 1.0), maage_generation_gradient(maage_generation_hash(seed, ix + 1, iz + 1, stage), dx - 1.0, dz - 1.0), u);
    return maage_generation_lerp(a, b, v);
}

static double maage_generation_fractal(maage_world_seed seed, double x, double z, unsigned long stage, int octaves) {
    double value, amplitude, weight;
    int octave;
    value = 0.0;
    weight = 0.0;
    amplitude = 1.0;
    for (octave = 0; octave < octaves; ++octave) {
        value += amplitude * maage_generation_noise(seed, x + 17.125, z - 9.75, stage + (unsigned long) octave * 1013UL);
        weight += amplitude;
        amplitude *= 0.5;
        x *= 2.0;
        z *= 2.0;
    }
    return value / weight;
}

/* Continental transitions retain the original geographic field. */
static double maage_generation_transition(double value, double threshold) {
    double margin = MAAGE_GENERATION_TRANSITION_MARGIN;
    if (value <= threshold - margin) return 0;
    if (value >= threshold + margin) return 1;
    return maage_generation_fade((value - threshold + margin) / (2 * margin));
}
static maage_biome_id maage_generation_biome(maage_world_seed seed, int x, int z, int land_only, maage_biome_id *climate) {
    static const double sea_limit = -0.10, hill_limit = 0.16, cold_limit = -0.11, warm_limit = 0.04, dry_limit = -0.04, forest_limit = 0.06;
    double temperature, rainfall, hills;
    maage_biome_id biome;
    if (!land_only && maage_generation_fractal(seed, x / 1100.0, z / 1100.0, 100UL, 3) < sea_limit) {
        if (climate) *climate = MAAGE_BIOME_OCEAN;
        return MAAGE_BIOME_OCEAN;
    }
    temperature = maage_generation_fractal(seed, x / 700.0, z / 700.0, 200UL, 2);
    rainfall = maage_generation_fractal(seed, x / 700.0, z / 700.0, 300UL, 2);
    hills = maage_generation_fractal(seed, x / 450.0, z / 450.0, 400UL, 2);
    if (temperature < cold_limit) biome = MAAGE_BIOME_TAIGA;
    else if (temperature > warm_limit && rainfall < dry_limit) biome = MAAGE_BIOME_DESERT;
    else if (rainfall > forest_limit) biome = MAAGE_BIOME_FOREST;
    else biome = MAAGE_BIOME_PLAINS;
    if (climate) *climate = biome;
    return hills > hill_limit ? MAAGE_BIOME_HILLS : biome;
}

static unsigned int maage_generation_region_index(maage_world_seed seed, int x, int z) {
    return (unsigned int) (maage_generation_hash(seed, x, z, 900UL) % MAAGE_GENERATION_REGION_CACHE_CAPACITY);
}
static maage_generation_region_seed maage_generation_region_raw(maage_world_seed seed, int x, int z, maage_generation_region_cache *cache) {
    maage_generation_region_seed region;
    unsigned int index = maage_generation_region_index(seed, x, z);
    unsigned long random;
    if (cache && cache->entries[index].valid && cache->entries[index].x == x && cache->entries[index].z == z) return cache->entries[index];
    memset(&region, 0, sizeof(region));
    region.x = x;
    region.z = z;
    region.valid = 1;
    random = maage_generation_hash(seed, x, z, 1000UL);
    region.site_x = x * (double) MAAGE_GENERATION_REGION_SCALE + MAAGE_GENERATION_REGION_SCALE * 0.5 + ((random & 65535UL) / 65536.0 - 0.5) * 48;
    random = maage_generation_hash(seed, x, z, 1100UL);
    region.site_z = z * (double) MAAGE_GENERATION_REGION_SCALE + MAAGE_GENERATION_REGION_SCALE * 0.5 + ((random & 65535UL) / 65536.0 - 0.5) * 48;
    region.biome = maage_generation_biome(seed, (int) floor(region.site_x), (int) floor(region.site_z), 1, &region.climate);
    if (cache) cache->entries[index] = region;
    return region;
}
static maage_biome_id maage_generation_region_supported(maage_biome_id center, const unsigned int votes[MAAGE_BIOME_COUNT]) {
    maage_biome_id best = center;
    unsigned int i;
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i)
        if (votes[i] > votes[best]) best = (maage_biome_id) i;
    /* Remove isolated seeds only when a surrounding majority supports the
     * replacement. Junctions and supported small regions keep their identity. */
    return votes[center] < 3 && votes[best] >= 5 ? best : center;
}
static maage_generation_region_seed maage_generation_region_at(maage_world_seed seed, int x, int z, maage_generation_region_cache *cache) {
    maage_generation_region_seed region = maage_generation_region_raw(seed, x, z, cache), neighbor;
    unsigned int biomes[MAAGE_BIOME_COUNT], climates[MAAGE_BIOME_COUNT];
    int dx, dz;
    if (region.supported) return region;
    memset(biomes, 0, sizeof(biomes));
    memset(climates, 0, sizeof(climates));
    for (dz = -1; dz <= 1; ++dz)
        for (dx = -1; dx <= 1; ++dx) {
            neighbor = maage_generation_region_raw(seed, x + dx, z + dz, cache);
            ++biomes[neighbor.biome];
            ++climates[neighbor.climate];
        }
    region.supported_biome = maage_generation_region_supported(region.biome, biomes);
    region.supported_climate = region.supported_biome == MAAGE_BIOME_HILLS ? maage_generation_region_supported(region.climate, climates) : region.supported_biome;
    region.supported = 1;
    /* Raw-neighbor queries may evict this direct-mapped entry. Reinsert a
     * complete value; never retain pointers across another cache lookup. */
    if (cache) cache->entries[maage_generation_region_index(seed, x, z)] = region;
    return region;
}
typedef struct maage_generation_environment {
    maage_generation_region_seed regions[9];
    double weights[9], ocean;
} maage_generation_environment;
static maage_biome_id maage_generation_region_sample(maage_world_seed seed, int x, int z, maage_generation_environment *environment, maage_generation_region_cache *cache) {
    maage_generation_environment local, *sample = environment ? environment : &local;
    double continental, region_x = x, region_z = z, dx, dz, distances[9], scores[MAAGE_BIOME_COUNT], nearest = 0, gap, total = 0, land;
    maage_biome_id biome;
    int gx, gz, ix, iz, i = 0, best = 0;
    memset(sample, 0, sizeof(*sample));
    continental = maage_generation_fractal(seed, x / 1100.0, z / 1100.0, 100UL, 3);
    if (continental < -0.10 && (!environment || continental <= -0.10 - MAAGE_GENERATION_TRANSITION_MARGIN)) {
        sample->ocean = 1;
        return MAAGE_BIOME_OCEAN;
    }
    /* Broad landform deformation and fine edge detail move one regional map;
     * they never choose materials or feature climates independently. */
    region_x += 24 * maage_generation_fractal(seed, x / 96.0, z / 96.0, 500UL, 2) + 3 * maage_generation_noise(seed, x / 8.0, z / 8.0, 1200UL);
    region_z += 24 * maage_generation_fractal(seed, x / 96.0, z / 96.0, 800UL, 2) + 3 * maage_generation_noise(seed, x / 8.0, z / 8.0, 1300UL);
    gx = (int) floor(region_x / MAAGE_GENERATION_REGION_SCALE);
    gz = (int) floor(region_z / MAAGE_GENERATION_REGION_SCALE);
    for (iz = -1; iz <= 1; ++iz)
        for (ix = -1; ix <= 1; ++ix, ++i) {
            sample->regions[i] = maage_generation_region_at(seed, gx + ix, gz + iz, cache);
            dx = region_x - sample->regions[i].site_x;
            dz = region_z - sample->regions[i].site_z;
            distances[i] = dx * dx + dz * dz;
            if (!i || distances[i] < nearest) {
                nearest = distances[i];
                best = i;
            }
        }
    /* Combine seeds of the same biome before categorical selection. This
     * rounds joins between their cells instead of exposing individual-cell
     * vertices. Compact support leaves every seed's pure interior intact. */
    memset(scores, 0, sizeof(scores));
    for (i = 0; i < 9; ++i) {
        gap = (distances[i] - nearest) / (MAAGE_GENERATION_REGION_SCALE * MAAGE_GENERATION_REGION_BLEND);
        sample->weights[i] = gap <= 0 ? 1 : (gap < 1 ? maage_generation_fade(1 - gap) : 0);
        scores[sample->regions[i].supported_biome] += sample->weights[i];
        total += sample->weights[i];
    }
    biome = sample->regions[best].supported_biome;
    for (i = 0; i < MAAGE_BIOME_COUNT; ++i)
        if (scores[i] > scores[biome]) biome = (maage_biome_id) i;
    if (environment) {
        land = maage_generation_transition(continental, -0.10);
        sample->ocean = 1 - land;
        for (i = 0; i < 9; ++i)
            sample->weights[i] *= land / total;
    }
    return continental < -0.10 ? MAAGE_BIOME_OCEAN : biome;
}

typedef struct maage_generation_column {
    maage_biome_id biome;
    int height;
} maage_generation_column;

static int maage_generation_grid(int coordinate) {
    int quotient;
    quotient = coordinate / MAAGE_GENERATION_BIOME_SCALE;
    if (coordinate % MAAGE_GENERATION_BIOME_SCALE < 0) --quotient;
    return quotient;
}

static int maage_generation_column_at(maage_world_seed seed, const maage_biome_registry *biomes, int x, int z, maage_generation_column *column, maage_generation_environment *environment, maage_generation_region_cache *regions) {
    const maage_biome_definition *definition;
    maage_biome_id biome;
    double heights[4], variations[4];
    double fx, fz, base, variation, terrain, detail;
    int gx, gz, corner, cx, cz;
    gx = maage_generation_grid(x);
    gz = maage_generation_grid(z);
    fx = (x - gx * MAAGE_GENERATION_BIOME_SCALE) / (double) MAAGE_GENERATION_BIOME_SCALE;
    fz = (z - gz * MAAGE_GENERATION_BIOME_SCALE) / (double) MAAGE_GENERATION_BIOME_SCALE;
    for (corner = 0; corner < 4; ++corner) {
        cx = (gx + (corner & 1)) * MAAGE_GENERATION_BIOME_SCALE;
        cz = (gz + (corner >> 1)) * MAAGE_GENERATION_BIOME_SCALE;
        biome = maage_generation_biome(seed, cx, cz, 0, NULL);
        definition = maage_biome_registry_get(biomes, biome);
        if (definition == NULL) return 0;
        heights[corner] = definition->base_height;
        variations[corner] = definition->variation;
    }
    /* Supported regional identity is independent of the unchanged terrain
     * anchors. All categorical blocks and ecology use this regional map. */
    column->biome = maage_generation_region_sample(seed, x, z, environment, regions);
    if (!maage_biome_registry_get(biomes, column->biome)) return 0;
    fx = maage_generation_fade(fx);
    fz = maage_generation_fade(fz);
    base = maage_generation_lerp(maage_generation_lerp(heights[0], heights[1], fx), maage_generation_lerp(heights[2], heights[3], fx), fz);
    variation = maage_generation_lerp(maage_generation_lerp(variations[0], variations[1], fx), maage_generation_lerp(variations[2], variations[3], fx), fz);
    terrain = maage_generation_fractal(seed, x / 96.0, z / 96.0, 500UL, 4);
    detail = maage_generation_fractal(seed, x / 24.0, z / 24.0, 600UL, 2);
    column->height = (int) floor(base + terrain * variation * 2.5 + detail * 1.5);
    if (column->height < 3) column->height = 3;
    if (column->height > MAAGE_WORLD_HEIGHT - 4) column->height = MAAGE_WORLD_HEIGHT - 4;
    return 1;
}
int maage_generation_surface(maage_world_seed seed, const maage_biome_registry *biomes, int x, int z, int *height, maage_biome_id *biome, maage_generation_region_cache *regions) {
    maage_generation_column column;
    if (!height || !biome || !maage_generation_column_at(seed, biomes, x, z, &column, NULL, regions)) return 0;
    *height = column.height;
    *biome = column.biome;
    return 1;
}
static int maage_generation_influence(maage_generation_sample *sample, const maage_biome_registry *biomes, maage_biome_id biome, maage_biome_id climate, double weight) {
    const maage_biome_definition *definition = maage_biome_registry_get(biomes, biome), *background;
    const maage_biome_decoration *decoration;
    double temperature, rainfall, trees = 0, background_trees = 0;
    unsigned int species;
    if (!definition) return 0;
    background = maage_biome_registry_get(biomes, climate);
    if (!background) return 0;
    decoration = &definition->decoration;
    temperature = definition->temperature;
    rainfall = definition->rainfall;
    /* Relief may cool a region, but cannot turn a cold/wet climate into a
     * warmer/drier stripe. Keep its own sparse density and registered surface;
     * the regional tree mixture supplies the species for that density. */
    if (biome == MAAGE_BIOME_HILLS) {
        if (background->temperature < temperature) temperature = background->temperature;
        if (background->rainfall > rainfall) rainfall = background->rainfall;
        for (species = 0; species < 3; ++species) {
            trees += decoration->trees[species];
            background_trees += background->decoration.trees[species];
        }
    }
    sample->temperature += temperature * weight;
    sample->rainfall += rainfall * weight;
    for (species = 0; species < 3; ++species)
        sample->decoration.trees[species] += (background_trees > 0 ? background->decoration.trees[species] * trees / background_trees : decoration->trees[species]) * weight;
    sample->decoration.grass += decoration->grass * weight;
    sample->decoration.flowers += decoration->flowers * weight;
    sample->decoration.dead_bushes += decoration->dead_bushes * weight;
    sample->decoration.cacti += decoration->cacti * weight;
    sample->decoration.mushrooms += decoration->mushrooms * weight;
    sample->decoration.emeralds += decoration->emeralds * weight;
    sample->decoration.lakes += decoration->lakes * weight;
    return 1;
}
int maage_generation_sample_at(maage_world_seed seed, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, maage_generation_sample *sample, maage_generation_region_cache *regions) {
    maage_generation_column column;
    maage_generation_environment environment;
    const maage_biome_definition *definition;
    unsigned int i;
    if (!sample || !maage_generation_policy_valid(policy) || !maage_generation_column_at(seed, biomes, x, z, &column, &environment, regions)) return 0;
    definition = maage_biome_registry_get(biomes, column.biome);
    if (!definition) return 0;
    memset(sample, 0, sizeof(*sample));
    sample->height = column.height;
    sample->biome = column.biome;
    /* Surface, filler and persisted identity always agree. Environmental
     * interpolation affects the boundary's ecology, never categorical blocks. */
    sample->surface = definition->surface;
    sample->filler = definition->filler;
    /* A small coherent offset in equivalent elevation varies the snowline
     * without a separate snow/ice/vegetation random decision. */
    sample->lapse_offset = 4 * maage_generation_fractal(seed, x / 16.0, z / 16.0, 1400UL, 1);
    if (environment.ocean > 0 && !maage_generation_influence(sample, biomes, MAAGE_BIOME_OCEAN, MAAGE_BIOME_OCEAN, environment.ocean)) return 0;
    for (i = 0; i < 9; ++i)
        if (environment.weights[i] > 0 && !maage_generation_influence(sample, biomes, environment.regions[i].supported_biome, environment.regions[i].supported_climate, environment.weights[i])) return 0;
    return 1;
}

int maage_generation_cold(const maage_generation_sample *sample, int y) {
    return sample->temperature - (y > 64 ? (y - 64 + sample->lapse_offset) / 600.0 : 0) < 0.15;
}

maage_status maage_generation_outcome(maage_generation_context *context, unsigned char code, unsigned char operation) {
    maage_status result;
    if (code >= 0x80) {
        if (context->token && maage_job_cancelled(context->token)) result = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_WORLD, operation, MAAGE_DETAIL_CANCELLED);
        else if (maage_status_failed(context->failure)) result = context->failure;
        else
            result = maage_status_make(code, MAAGE_CONTEXT_WORLD, operation, 0);
        return MAAGE_STATUS_TRACE(context->trace, result, MAAGE_CONTEXT_WORLD, operation);
    }
    return maage_status_make(code, 0, operation, 0);
}
static maage_status maage_generation_stage_begin(maage_generation_context *context, maage_generation_stage stage) {
    static const unsigned char operations[] = {MAAGE_OPERATION_GENERATE, MAAGE_OPERATION_TERRAIN,
                                               MAAGE_OPERATION_CAVES, MAAGE_OPERATION_ORES, MAAGE_OPERATION_SURFACE,
                                               MAAGE_OPERATION_VEGETATION, MAAGE_OPERATION_STRUCTURES, MAAGE_OPERATION_FINALIZE};
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, operations[stage], 0);
    if (context->token) {
        if (maage_job_cancelled(context->token)) return maage_generation_outcome(context, MAAGE_STATUS_INTERRUPTED, operations[stage]);
        result = maage_job_set_stage(context->token, (unsigned int) stage + 1U);
    }
    return MAAGE_STATUS_TRACE(context->trace, result, MAAGE_CONTEXT_WORLD, operations[stage]);
}
static maage_status maage_generation_features(maage_generation_context *context) {
    int x, z, top;
    maage_status result;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_CAVES);
    if (!maage_status_failed(result)) result = maage_generation_carve(context);
    if (maage_status_failed(result)) return result;
    context->chunk->generation_stages |= 1U << MAAGE_GENERATION_STAGE_CAVES;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_UNDERGROUND);
    if (!maage_status_failed(result)) result = maage_generation_underground(context);
    if (maage_status_failed(result)) return result;
    context->chunk->generation_stages |= 1U << MAAGE_GENERATION_STAGE_UNDERGROUND;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_SURFACE);
    if (!maage_status_failed(result)) result = maage_generation_decorate(context);
    if (maage_status_failed(result)) return result;
    context->chunk->generation_stages |= 1U << MAAGE_GENERATION_STAGE_SURFACE;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_VEGETATION);
    if (!maage_status_failed(result)) result = maage_generation_vegetation(context);
    if (maage_status_failed(result)) return result;
    context->chunk->generation_stages |= 1U << MAAGE_GENERATION_STAGE_VEGETATION;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_STRUCTURES);
    if (!maage_status_failed(result)) result = maage_generation_structures(context);
    if (maage_status_failed(result)) return result;
    context->chunk->generation_stages |= 1U << MAAGE_GENERATION_STAGE_STRUCTURES;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_FINALIZE);
    if (maage_status_failed(result)) return result;
    for (z = 0; z < 16; ++z)
        for (x = 0; x < 16; ++x) {
            top = MAAGE_WORLD_HEIGHT;
            while (top > 0 && maage_chunk_get(context->chunk, x, top - 1, z) == MAAGE_STATE_AIR)
                --top;
            context->chunk->heightmap[z * 16 + x] = (unsigned short) top;
        }
    context->chunk->generation_stages |= 1U << MAAGE_GENERATION_STAGE_FINALIZE;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_GENERATE, 0);
}

maage_status maage_generate_chunk_job(maage_chunk *chunk, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, const maage_job_token *token, maage_status_trace *trace) {
    const maage_generation_sample *column;
    const maage_block_state_definition *filler;
    maage_generation_sample columns[256];
    maage_generation_region_cache regions;
    maage_generation_context context;
    maage_block_state_id state;
    unsigned long random;
    int x, z, y, wx, wz, top, depth;
    maage_status result;
    memset(&context, 0, sizeof(context));
    memset(&regions, 0, sizeof(regions));
    context.regions = &regions;
    context.trace = trace;
    context.token = token;
    if (!maage_generation_policy_valid(policy) || chunk == NULL ||
        !maage_block_registry_is_finalized(blocks) ||
        biomes == NULL || !biomes->finalized ||
        chunk->x < -MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE ||
        chunk->x >= MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE ||
        chunk->z < -MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE ||
        chunk->z >= MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE) return maage_generation_outcome(&context, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_GENERATE);
    context.chunk = chunk;
    context.columns = columns;
    context.seed = seed;
    context.blocks = blocks;
    context.biomes = biomes;
    context.policy = policy;
    context.token = token;
    if (!maage_generation_features_valid(blocks)) return maage_generation_outcome(&context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_GENERATE);
    result = maage_generation_stage_begin(&context, MAAGE_GENERATION_STAGE_BASE);
    if (maage_status_failed(result)) return result;
    for (z = 0; z < MAAGE_CHUNK_EDGE; ++z) {
        if (token && maage_job_cancelled(token)) return maage_generation_outcome(&context, MAAGE_STATUS_INTERRUPTED, MAAGE_OPERATION_GENERATE);
        for (x = 0; x < MAAGE_CHUNK_EDGE; ++x) {
            if (!maage_generation_sample_at(seed, biomes, policy, chunk->x * MAAGE_CHUNK_EDGE + x, chunk->z * MAAGE_CHUNK_EDGE + z, &columns[z * MAAGE_CHUNK_EDGE + x], &regions)) return maage_generation_outcome(&context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_GENERATE);
            chunk->biomes[z * MAAGE_CHUNK_EDGE + x] = context.columns[z * MAAGE_CHUNK_EDGE + x].biome;
        }
    }
    chunk->generation_stages |= 1U << MAAGE_GENERATION_STAGE_BASE;
    result = maage_generation_stage_begin(&context, MAAGE_GENERATION_STAGE_TERRAIN);
    if (maage_status_failed(result)) return result;
    for (z = 0; z < MAAGE_CHUNK_EDGE; ++z) {
        if (token && maage_job_cancelled(token)) return maage_generation_outcome(&context, MAAGE_STATUS_INTERRUPTED, MAAGE_OPERATION_GENERATE);
        for (x = 0; x < MAAGE_CHUNK_EDGE; ++x) {
            wx = chunk->x * MAAGE_CHUNK_EDGE + x;
            wz = chunk->z * MAAGE_CHUNK_EDGE + z;
            column = &context.columns[z * MAAGE_CHUNK_EDGE + x];
            filler = maage_block_registry_state(blocks, column->filler);
            if (!maage_block_registry_state(blocks, column->surface) || !filler) return maage_generation_outcome(&context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_TERRAIN);
            top = column->height > policy->sea_level ? column->height : policy->sea_level;
            random = maage_generation_hash(seed, wx, wz, 700UL);
            depth = 3 + (int) ((random >> 5) & 1UL);
            for (y = 0; y <= top; ++y) {
                if (y > column->height) state = MAAGE_STATE_WATER;
                else if (y == 0 || (y < 5 && ((random >> (y * 3)) & 7UL) >= (unsigned long) y)) state = MAAGE_STATE_BEDROCK;
                else if (y == column->height) {
                    state = column->surface;
                    if (column->height < policy->sea_level - 1 && filler->block_id != MAAGE_BLOCK_SAND) state = MAAGE_STATE_GRAVEL;
                } else if (y >= column->height - depth) state = column->filler;
                else if (filler->block_id == MAAGE_BLOCK_SAND && y >= column->height - depth - 2) state = MAAGE_STATE_SANDSTONE;
                else
                    state = MAAGE_STATE_STONE;
                if (!maage_block_registry_state(blocks, state)) return maage_generation_outcome(&context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_GENERATE);
                result = maage_chunk_set(chunk, x, y, z, state);
                if (maage_status_failed(result)) {
                    context.failure = result;
                    return maage_generation_outcome(&context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_GENERATE);
                }
            }
            chunk->heightmap[z * MAAGE_CHUNK_EDGE + x] = (unsigned short) (top + 1);
        }
    }
    chunk->generation_stages |= 1U << MAAGE_GENERATION_STAGE_TERRAIN;
    return maage_generation_features(&context);
}

maage_status maage_generation_spawn(maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, double feet[3]) {
    maage_generation_column column;
    maage_generation_region_cache regions;
    int radius, side, offset, x, z, best_height, best_x, best_z;
    if (!feet || !maage_block_registry_is_finalized(blocks) || !biomes || !biomes->finalized || !maage_generation_policy_valid(policy)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
    memset(&regions, 0, sizeof(regions));
    best_height = -1;
    best_x = best_z = 0;
    /* Deterministic bounded spiral of surface samples. A finite fallback
     * selects the highest sampled position, or sea level at the origin. */
    radius = 0;
    for (;;) {
        for (side = 0; side < (radius == 0 ? 1 : 4); ++side) {
            offset = -radius;
            for (;;) {
                x = side < 2 ? offset : (side == 2 ? -radius : radius);
                z = side < 2 ? (side == 0 ? -radius : radius) : offset;
                if (maage_generation_column_at(seed, biomes, x, z, &column, NULL, &regions)) {
                    if (column.height > best_height) {
                        best_height = column.height;
                        best_x = x;
                        best_z = z;
                    }
                    if (column.height > policy->sea_level && !maage_biome_registry_get(biomes, column.biome)->aquatic && maage_generation_spawn_clear(seed, blocks, biomes, policy, x, z, &regions)) {
                        feet[0] = x + 0.5;
                        feet[1] = column.height + 1.0;
                        if (!maage_generation_spawn_floor(seed, blocks, biomes, policy, x, z, &feet[1], &regions)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
                        feet[2] = z + 0.5;
                        return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_GENERATE, 0);
                    }
                }
                if (offset > radius - policy->spawn_search_step) break;
                offset += policy->spawn_search_step;
            }
        }
        if (radius >= policy->spawn_search_radius) break;
        if (radius > policy->spawn_search_radius - policy->spawn_search_step) radius = policy->spawn_search_radius;
        else
            radius += policy->spawn_search_step;
    }
    feet[0] = best_x + 0.5;
    if (!maage_generation_spawn_floor(seed, blocks, biomes, policy, best_x, best_z, &feet[1], &regions)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
    if (feet[1] < policy->sea_level + 1.0) feet[1] = policy->sea_level + 1.0;
    feet[2] = best_z + 0.5;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_GENERATE, 0);
}
