#include "world_internal.h"
#include "maage/jobs.h"

#include <math.h>
#include <string.h>

#define MAAGE_GENERATION_DEFAULT_SEA_Y 63
#define MAAGE_GENERATION_DEFAULT_SPAWN_RADIUS 4096
#define MAAGE_GENERATION_DEFAULT_SPAWN_STEP 16
#define MAAGE_GENERATION_WORD_MASK 4294967295UL
#define MAAGE_GENERATION_BIOME_SCALE 128

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

static maage_biome_id maage_generation_biome(maage_world_seed seed, int x, int z) {
    double continental, temperature, rainfall, hills;
    continental = maage_generation_fractal(seed, x / 1100.0, z / 1100.0, 100UL, 3);
    if (continental < -0.10) return MAAGE_BIOME_OCEAN;
    temperature = maage_generation_fractal(seed, x / 700.0, z / 700.0, 200UL, 2);
    rainfall = maage_generation_fractal(seed, x / 700.0, z / 700.0, 300UL, 2);
    hills = maage_generation_fractal(seed, x / 450.0, z / 450.0, 400UL, 2);
    if (hills > 0.16) return MAAGE_BIOME_HILLS;
    if (temperature < -0.11) return MAAGE_BIOME_TAIGA;
    if (temperature > 0.04 && rainfall < -0.04) return MAAGE_BIOME_DESERT;
    if (rainfall > 0.06) return MAAGE_BIOME_FOREST;
    return MAAGE_BIOME_PLAINS;
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

static int maage_generation_column_at(maage_world_seed seed, const maage_biome_registry *biomes, int x, int z, maage_generation_column *column) {
    const maage_biome_definition *definition;
    maage_biome_id ids[4];
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
        ids[corner] = maage_generation_biome(seed, cx, cz);
        definition = maage_biome_registry_get(biomes, ids[corner]);
        if (definition == NULL) return 0;
        heights[corner] = definition->base_height;
        variations[corner] = definition->variation;
    }
    column->biome = ids[(fx >= 0.5 ? 1 : 0) + (fz >= 0.5 ? 2 : 0)];
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
int maage_generation_surface(maage_world_seed seed, const maage_biome_registry *biomes, int x, int z, int *height, maage_biome_id *biome) {
    maage_generation_column column;
    if (!height || !biome || !maage_generation_column_at(seed, biomes, x, z, &column)) return 0;
    *height = column.height;
    *biome = column.biome;
    return 1;
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
    const maage_biome_definition *biome;
    maage_generation_column column;
    maage_generation_context context;
    maage_block_state_id state;
    unsigned long random;
    int x, z, y, wx, wz, top, depth;
    maage_status result;
    memset(&context, 0, sizeof(context));
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
            if (!maage_generation_column_at(seed, biomes, chunk->x * MAAGE_CHUNK_EDGE + x, chunk->z * MAAGE_CHUNK_EDGE + z, &column)) return maage_generation_outcome(&context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_GENERATE);
            context.surface[z * MAAGE_CHUNK_EDGE + x] = column.height;
            chunk->biomes[z * MAAGE_CHUNK_EDGE + x] = column.biome;
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
            column.height = context.surface[z * MAAGE_CHUNK_EDGE + x];
            column.biome = chunk->biomes[z * MAAGE_CHUNK_EDGE + x];
            biome = maage_biome_registry_get(biomes, column.biome);
            if (!biome) return maage_generation_outcome(&context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_GENERATE);
            top = column.height > policy->sea_level ? column.height : policy->sea_level;
            random = maage_generation_hash(seed, wx, wz, 700UL);
            depth = 3 + (int) ((random >> 5) & 1UL);
            for (y = 0; y <= top; ++y) {
                if (y > column.height) state = MAAGE_STATE_WATER;
                else if (y == 0 || (y < 5 && ((random >> (y * 3)) & 7UL) >= (unsigned long) y)) state = MAAGE_STATE_BEDROCK;
                else if (y == column.height) {
                    state = biome->surface;
                    if (column.height < policy->sea_level - 1 && column.biome != MAAGE_BIOME_DESERT) state = MAAGE_STATE_GRAVEL;
                } else if (y >= column.height - depth) state = biome->filler;
                else if (column.biome == MAAGE_BIOME_DESERT && y >= column.height - depth - 2) state = MAAGE_STATE_SANDSTONE;
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

maage_status maage_generation_spawn(maage_world_seed seed, const maage_biome_registry *biomes, const maage_generation_policy *policy, double feet[3]) {
    maage_generation_column column;
    int radius, side, offset, x, z, best_height, best_x, best_z;
    if (!feet || !biomes || !biomes->finalized || !maage_generation_policy_valid(policy)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
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
                if (maage_generation_column_at(seed, biomes, x, z, &column)) {
                    if (column.height > best_height) {
                        best_height = column.height;
                        best_x = x;
                        best_z = z;
                    }
                    if (column.height > policy->sea_level && column.biome != MAAGE_BIOME_OCEAN && maage_generation_spawn_clear(seed, biomes, policy, x, z)) {
                        feet[0] = x + 0.5;
                        feet[1] = column.height + 1.0;
                        if (!maage_generation_spawn_floor(seed, biomes, policy, x, z, &feet[1])) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
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
    if (!maage_generation_spawn_floor(seed, biomes, policy, best_x, best_z, &feet[1])) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
    if (feet[1] < policy->sea_level + 1.0) feet[1] = policy->sea_level + 1.0;
    feet[2] = best_z + 0.5;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_GENERATE, 0);
}
