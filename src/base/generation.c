#include "base_internal.h"
#include "maage/jobs.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAAGE_GENERATION_DEFAULT_SEA_Y 63
#define MAAGE_GENERATION_DEFAULT_SPAWN_RADIUS 4096
#define MAAGE_GENERATION_DEFAULT_SPAWN_STEP 16
#define MAAGE_GENERATION_WORD_MASK 4294967295UL
#define MAAGE_GENERATION_REGION_SCALE 256
#define MAAGE_GENERATION_REGION_RADIUS 384.0
#define MAAGE_GENERATION_REGION_SAMPLES 25

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

static maage_biome_id maage_generation_biome(const maage_base_ids *ids, maage_world_seed seed, int x, int z, maage_biome_id *climate) {
    static const double hill_limit = 0.16, cold_limit = -0.11, warm_limit = 0.04, dry_limit = -0.04, forest_limit = 0.06;
    double temperature, rainfall, hills;
    maage_biome_id biome;
    if (maage_generation_fractal(seed, x / 1100.0, z / 1100.0, 100UL, 3) < -0.10) {
        if (climate) *climate = ids->biome_ocean;
        return ids->biome_ocean;
    }
    temperature = maage_generation_fractal(seed, x / 700.0, z / 700.0, 200UL, 2);
    rainfall = maage_generation_fractal(seed, x / 700.0, z / 700.0, 300UL, 2);
    hills = maage_generation_fractal(seed, x / 450.0, z / 450.0, 400UL, 2);
    if (temperature < cold_limit) biome = ids->biome_taiga;
    else if (temperature > warm_limit && rainfall < dry_limit) biome = ids->biome_desert;
    else if (rainfall > forest_limit) biome = ids->biome_forest;
    else biome = ids->biome_plains;
    if (climate) *climate = biome;
    return hills > hill_limit ? ids->biome_hills : biome;
}

static unsigned int maage_generation_region_index(int x, int z) {
    /* A local 7x7 footprint has no collisions in this 8x8 ring, unlike a
     * hashed direct map that can repeatedly evict neighboring raw sites. */
    return (unsigned int) x % MAAGE_GENERATION_REGION_CACHE_EDGE + ((unsigned int) z % MAAGE_GENERATION_REGION_CACHE_EDGE) * MAAGE_GENERATION_REGION_CACHE_EDGE;
}
static maage_generation_sample_entry *maage_generation_sample_entry_at(maage_generation_region_cache *cache, int x, int z) {
    unsigned int index = (unsigned int) x % MAAGE_GENERATION_SAMPLE_CACHE_EDGE + ((unsigned int) z % MAAGE_GENERATION_SAMPLE_CACHE_EDGE) * MAAGE_GENERATION_SAMPLE_CACHE_EDGE;
    return cache ? &cache->samples[index] : NULL;
}
static maage_generation_region_seed maage_generation_region_raw(const maage_base_ids *ids, maage_world_seed seed, int x, int z, maage_generation_region_cache *cache) {
    maage_generation_region_seed region;
    unsigned int index = maage_generation_region_index(x, z);
    unsigned long random;
    if (cache && cache->entries[index].valid && cache->entries[index].x == x && cache->entries[index].z == z) return cache->entries[index];
    memset(&region, 0, sizeof(region));
    region.x = x;
    region.z = z;
    region.valid = 1;
    random = maage_generation_hash(seed, x, z, 1000UL);
    region.site_x = x * (double) MAAGE_GENERATION_REGION_SCALE + MAAGE_GENERATION_REGION_SCALE * 0.5 + ((random & 65535UL) / 65536.0 - 0.5) * 160;
    random = maage_generation_hash(seed, x, z, 1100UL);
    region.site_z = z * (double) MAAGE_GENERATION_REGION_SCALE + MAAGE_GENERATION_REGION_SCALE * 0.5 + ((random & 65535UL) / 65536.0 - 0.5) * 160;
    region.biome = maage_generation_biome(ids, seed, (int) floor(region.site_x), (int) floor(region.site_z), &region.climate);
    if (cache) cache->entries[index] = region;
    return region;
}
static unsigned int base_biome_slot(const maage_base_ids *ids, maage_biome_id biome) {
    unsigned int slot;
    for (slot = 0; slot < MAAGE_BASE_BIOME_COUNT; ++slot)
        if (ids->biome_ids[slot] == biome) return slot;
    return MAAGE_BASE_BIOME_COUNT;
}
static maage_biome_id maage_generation_region_supported(const maage_base_ids *ids, maage_biome_id identity, const unsigned int votes[MAAGE_BASE_BIOME_COUNT]) {
    unsigned int center = base_biome_slot(ids, identity), best = center;
    unsigned int i;
    if (center >= MAAGE_BASE_BIOME_COUNT) return MAAGE_CONTENT_ID_INVALID;
    for (i = 0; i < MAAGE_BASE_BIOME_COUNT; ++i)
        if (votes[i] > votes[best]) best = (maage_biome_id) i;
    /* Remove isolated seeds only when a surrounding majority supports the
     * replacement. Junctions and supported small regions keep their identity. */
    return votes[center] < 3 && votes[best] >= 5 ? ids->biome_ids[best] : identity;
}
static maage_generation_region_seed maage_generation_region_at(const maage_base_ids *ids, maage_world_seed seed, int x, int z, maage_generation_region_cache *cache) {
    maage_generation_region_seed region = maage_generation_region_raw(ids, seed, x, z, cache), neighbor;
    unsigned int biomes[MAAGE_BASE_BIOME_COUNT], climates[MAAGE_BASE_BIOME_COUNT];
    int dx, dz;
    unsigned int biome_slot, climate_slot;
    if (region.supported) return region;
    memset(biomes, 0, sizeof(biomes));
    memset(climates, 0, sizeof(climates));
    for (dz = -1; dz <= 1; ++dz)
        for (dx = -1; dx <= 1; ++dx) {
            neighbor = maage_generation_region_raw(ids, seed, x + dx, z + dz, cache);
            biome_slot = base_biome_slot(ids, neighbor.biome);
            climate_slot = base_biome_slot(ids, neighbor.climate);
            if (biome_slot >= MAAGE_BASE_BIOME_COUNT || climate_slot >= MAAGE_BASE_BIOME_COUNT) {
                region.supported_biome = region.supported_climate = MAAGE_CONTENT_ID_INVALID;
                return region;
            }
            ++biomes[biome_slot];
            ++climates[climate_slot];
        }
    region.supported_biome = maage_generation_region_supported(ids, region.biome, biomes);
    region.supported_climate = region.supported_biome == ids->biome_hills ? maage_generation_region_supported(ids, region.climate, climates) : region.supported_biome;
    region.supported = 1;
    /* Raw-neighbor queries may evict this direct-mapped entry. Reinsert a
     * complete value; never retain pointers across another cache lookup. */
    if (cache) cache->entries[maage_generation_region_index(x, z)] = region;
    return region;
}
typedef struct maage_generation_environment {
    maage_generation_region_seed regions[MAAGE_GENERATION_REGION_SAMPLES];
    double weights[MAAGE_GENERATION_REGION_SAMPLES], ocean, coast, flow;
} maage_generation_environment;
static maage_biome_id maage_generation_region_sample(const maage_base_ids *ids, maage_world_seed seed, int x, int z, maage_generation_environment *sample, maage_generation_region_cache *cache) {
    double region_x = x, region_z = z, dx, dz, distance, radius, weight, nearest = MAAGE_GENERATION_REGION_RADIUS * MAAGE_GENERATION_REGION_RADIUS, total = 0;
    int gx, gz, ix, iz, i = 0;
    maage_biome_id selected = MAAGE_CONTENT_ID_INVALID;
    memset(sample, 0, sizeof(*sample));
    region_x += 128 * maage_generation_fractal(seed, x / 320.0, z / 320.0, 500UL, 2) + 32 * maage_generation_fractal(seed, x / 96.0, z / 96.0, 1200UL, 2);
    region_z += 128 * maage_generation_fractal(seed, x / 320.0, z / 320.0, 800UL, 2) + 32 * maage_generation_fractal(seed, x / 96.0, z / 96.0, 1300UL, 2);
    gx = (int) floor(region_x / MAAGE_GENERATION_REGION_SCALE);
    gz = (int) floor(region_z / MAAGE_GENERATION_REGION_SCALE);
    /* Terrestrial identity belongs to the nearest supported land site.
     * Ocean coverage combines radial influences so enclosed water follows
     * a rounded basin rather than the polygon of one regional site.
     * Even with 80-block site offsets, omitted sites are beyond the compact
     * kernel's radius and one site is always within that radius. */
    for (iz = -2; iz <= 2; ++iz)
        for (ix = -2; ix <= 2; ++ix, ++i) {
            sample->regions[i] = maage_generation_region_at(ids, seed, gx + ix, gz + iz, cache);
            dx = region_x - sample->regions[i].site_x;
            dz = region_z - sample->regions[i].site_z;
            distance = dx * dx + dz * dz;
            if (sample->regions[i].supported_biome != ids->biome_ocean && distance < nearest) {
                nearest = distance;
                selected = sample->regions[i].supported_biome;
            }
            radius = distance / (MAAGE_GENERATION_REGION_RADIUS * MAAGE_GENERATION_REGION_RADIUS);
            if (radius >= 1) continue;
            weight = 1 - radius;
            sample->weights[i] = weight * weight * weight;
            total += sample->weights[i];
        }
    if (total <= 0) return MAAGE_CONTENT_ID_INVALID;
    for (i = 0; i < MAAGE_GENERATION_REGION_SAMPLES; ++i) {
        sample->weights[i] /= total;
        if (sample->regions[i].supported_biome == ids->biome_ocean) sample->ocean += sample->weights[i];
    }
    /* Identity and elevation use this same signed, smoothly blended shore
     * coordinate. Its positive land side owns the narrow beach band. */
    sample->coast = MAAGE_GENERATION_REGION_SCALE * (1 - 2 * sample->ocean);
    return sample->coast <= 0 ? ids->biome_ocean : selected;
}

/* Drainage has its own seeded cells, independent of climate and chunk order.
 * Overlapping radial class influences round channel junctions; smoothly warped
 * coordinates give bends without sampling or modifying neighboring columns. */
static double maage_generation_river_distance(maage_world_seed seed, int x, int z) {
    double px, pz, sx, sz, dx, dz, radius, weight, balance, coverage[2];
    unsigned long random;
    int gx, gz, ix, iz, group;
    px = x + 64 * maage_generation_fractal(seed, x / 120.0, z / 120.0, 1700UL, 2);
    pz = z + 64 * maage_generation_fractal(seed, x / 120.0, z / 120.0, 1800UL, 2);
    gx = (int) floor(px / 192.0);
    gz = (int) floor(pz / 192.0);
    /* The 288-block support overlaps several sites of each class. A 5x5
     * window contains every contributor, with zero boundary derivatives;
     * changing the window cannot introduce a seam or a nearest-site corner. */
    coverage[0] = coverage[1] = 0;
    for (iz = -2; iz <= 2; ++iz)
        for (ix = -2; ix <= 2; ++ix) {
            random = maage_generation_hash(seed, gx + ix, gz + iz, 1900UL);
            group = (int) (random & 1UL);
            sx = (gx + ix + 0.5) * 192 + (((random >> 1) & 255UL) / 255.0 - 0.5) * 64;
            sz = (gz + iz + 0.5) * 192 + (((random >> 9) & 255UL) / 255.0 - 0.5) * 64;
            dx = px - sx;
            dz = pz - sz;
            radius = (dx * dx + dz * dz) / (288.0 * 288.0);
            if (radius >= 1) continue;
            weight = 1 - radius;
            coverage[group] += weight * weight * weight;
        }
    /* Coverage is nonzero because the containing cell's site is at most
     * 128 blocks away on each axis. Distance grows without a finite cap so
     * wide highland valleys cannot carve a class's entire dry interior. */
    balance = fabs(coverage[0] - coverage[1]) / (coverage[0] + coverage[1]);
    return balance < 1 ? 96 * balance / (1 - balance * balance) : HUGE_VAL;
}

typedef struct maage_generation_column {
    maage_biome_id biome;
    int height;
} maage_generation_column;

static int maage_generation_column_at(const maage_base_ids *ids, maage_world_seed seed, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, maage_generation_column *column, maage_generation_environment *environment, maage_generation_region_cache *regions) {
    maage_generation_environment local, *sample = environment ? environment : &local;
    maage_generation_sample_entry *cached = maage_generation_sample_entry_at(regions, x, z);
    const maage_biome_definition *definition, *climate;
    maage_biome_id biome;
    double base = 0, variation = 0, temperature = 0, land = 0, terrain, detail, elevation, uncarved, coast, distance, width, depth, pool, cross, bank, target, span;
    int i, ocean;
    if (!environment && cached && cached->valid && cached->x == x && cached->z == z) {
        column->height = cached->sample.height;
        column->biome = cached->sample.biome;
        return 1;
    }
    biome = maage_generation_region_sample(ids, seed, x, z, sample, regions);
    if (!maage_biome_registry_get(biomes, biome)) return 0;
    for (i = 0; i < MAAGE_GENERATION_REGION_SAMPLES; ++i) {
        if (sample->weights[i] <= 0) continue;
        definition = maage_biome_registry_get(biomes, sample->regions[i].supported_biome);
        climate = maage_biome_registry_get(biomes, sample->regions[i].supported_climate);
        if (!definition || !climate) return 0;
        if (sample->regions[i].supported_biome != ids->biome_ocean) {
            base += definition->base_height * sample->weights[i];
            variation += definition->variation * sample->weights[i];
            land += sample->weights[i];
        }
        temperature += (definition->temperature < climate->temperature ? definition->temperature : climate->temperature) * sample->weights[i];
    }
    terrain = maage_generation_fractal(seed, x / 96.0, z / 96.0, 500UL, 4);
    detail = maage_generation_fractal(seed, x / 24.0, z / 24.0, 600UL, 2);
    /* One world-space coastal ramp aligns the actual sea-level crossing
     * with the categorical shore. Terrain noise fades into that crossing;
     * it cannot cut holes through an elevation-selected beach band. */
    ocean = biome == ids->biome_ocean;
    if (ocean) {
        definition = maage_biome_registry_get(biomes, ids->biome_ocean);
        if (!definition) return 0;
        elevation = definition->base_height + policy->sea_level - MAAGE_GENERATION_DEFAULT_SEA_Y + terrain * definition->variation * 2.5 + detail * 1.5;
        if (elevation > policy->sea_level - 3) elevation = policy->sea_level - 3;
    } else {
        if (land <= 0) return 0;
        elevation = base / land + policy->sea_level - MAAGE_GENERATION_DEFAULT_SEA_Y + terrain * variation / land * 2.5 + detail * 1.5;
        if (elevation < policy->sea_level + 2) elevation = policy->sea_level + 2;
    }
    coast = fabs(sample->coast) / 128;
    coast = coast < 1 ? maage_generation_fade(coast) : 1;
    elevation = maage_generation_lerp(policy->sea_level, elevation, coast);
    uncarved = elevation;
    column->biome = biome;
    if (!ocean && sample->coast >= 0 && sample->coast < 12) {
        if (biome == ids->biome_hills) column->biome = ids->biome_stone_beach;
        else column->biome = temperature < 0.15 ? ids->biome_cold_beach : ids->biome_beach;
    }
    distance = maage_generation_river_distance(seed, x, z);
    width = 8 + 12 * maage_generation_fractal(seed, x / 160.0, z / 160.0, 2000UL, 2);
    pool = (maage_generation_noise(seed, x / 240.0 + 11.5, z / 240.0 - 7.5, 2200UL) - 0.18) / 0.35;
    pool = pool <= 0 ? 0 : (pool < 1 ? maage_generation_fade(pool) : 1);
    width += 14 * pool;
    if (width < 3) width = 3;
    depth = 5 + 7 * maage_generation_fractal(seed, x / 112.0, z / 112.0, 2100UL, 2) + maage_generation_noise(seed, x / 24.0, z / 24.0, 2101UL);
    if (depth < 2) depth = 2;
    if (depth > 9) depth = 9;
    /* One profile rises from the bed to untouched terrain. Separate inner
     * depth and outer bank ramps would leave a steep trench beside a shelf.
     * Higher relief gets a wider valley instead of a taller channel wall. */
    target = policy->sea_level - depth;
    span = width + 24 + (elevation > policy->sea_level ? (elevation - policy->sea_level) * 2 : 0);
    cross = distance / span;
    bank = cross < 1 ? 1 - maage_generation_fade(cross) : 0;
    if (elevation > target) elevation = maage_generation_lerp(elevation, target, bank);
    column->height = (int) floor(elevation);
    /* Every submerged part of the carved channel is river, including its
     * shallow margins and pools. The pre-carve ocean retains mouth ownership. */
    if (!ocean && bank > 0 && elevation < uncarved && column->height < policy->sea_level) {
        column->biome = temperature < 0.15 ? ids->biome_frozen_river : ids->biome_river;
        cross = (policy->sea_level - elevation) / depth;
        sample->flow = (cross < 1 ? cross : 1) * (1 - pool * 0.5);
    }
    if (column->height < 3) column->height = 3;
    if (column->height > MAAGE_WORLD_HEIGHT - 4) column->height = MAAGE_WORLD_HEIGHT - 4;
    if (cached) {
        if (!cached->valid || cached->x != x || cached->z != z) cached->complete = 0;
        cached->x = x;
        cached->z = z;
        cached->valid = 1;
        cached->sample.height = column->height;
        cached->sample.biome = column->biome;
    }
    return 1;
}
int maage_generation_surface(const maage_base_ids *ids, maage_world_seed seed, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, int *height, maage_biome_id *biome, maage_generation_region_cache *regions) {
    maage_generation_column column;
    if (!height || !biome || !maage_generation_policy_valid(policy) || !maage_generation_column_at(ids, seed, biomes, policy, x, z, &column, NULL, regions)) return 0;
    *height = column.height;
    *biome = column.biome;
    return 1;
}
static int maage_generation_influence(const maage_base_ids *ids, maage_generation_sample *sample, const maage_biome_registry *biomes, maage_biome_id biome, maage_biome_id climate, double weight) {
    const maage_biome_definition *definition = maage_biome_registry_get(biomes, biome), *background;
    const maage_base_biome_decoration *decoration;
    double temperature, rainfall, trees = 0, background_trees = 0;
    unsigned int species;
    if (!definition || definition->generation_data_size != sizeof(maage_base_biome_decoration) || !definition->generation_data) return 0;
    background = maage_biome_registry_get(biomes, climate);
    if (!background || background->generation_data_size != sizeof(maage_base_biome_decoration) || !background->generation_data) return 0;
    decoration = (const maage_base_biome_decoration *) definition->generation_data;
    temperature = definition->temperature;
    rainfall = definition->rainfall;
    /* Relief may cool a region, but cannot turn a cold/wet climate into a
     * warmer/drier stripe. Keep its own sparse density and registered surface;
     * the regional tree mixture supplies the species for that density. */
    if (biome == ids->biome_hills) {
        if (background->temperature < temperature) temperature = background->temperature;
        if (background->rainfall > rainfall) rainfall = background->rainfall;
        for (species = 0; species < 3; ++species) {
            trees += decoration->trees[species];
            background_trees += (*(const maage_base_biome_decoration *) background->generation_data).trees[species];
        }
    }
    sample->temperature += temperature * weight;
    sample->rainfall += rainfall * weight;
    for (species = 0; species < 3; ++species)
        sample->decoration.trees[species] += (background_trees > 0 ? (*(const maage_base_biome_decoration *) background->generation_data).trees[species] * trees / background_trees : decoration->trees[species]) * weight;
    sample->decoration.grass += decoration->grass * weight;
    sample->decoration.flowers += decoration->flowers * weight;
    sample->decoration.dead_bushes += decoration->dead_bushes * weight;
    sample->decoration.cacti += decoration->cacti * weight;
    sample->decoration.mushrooms += decoration->mushrooms * weight;
    sample->decoration.emeralds += decoration->emeralds * weight;
    sample->decoration.lakes += decoration->lakes * weight;
    return 1;
}
static int maage_generation_soil_depth(maage_world_seed seed, int x, int z) {
    return 3 + (int) ((maage_generation_hash(seed, x, z, 700UL) >> 5) & 1UL);
}
/* Evaluate the few nearby sediment owners against this column's soil layers.
 * Only submerged anchors participate. Each pass changes eligible material
 * inside its own vertical interval, so a patch never replaces a whole bank
 * profile or creates a sandstone foundation below a thin sand deposit. */
static int maage_generation_sediment(const maage_base_ids *ids, maage_world_seed seed, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, maage_generation_sample *sample, maage_generation_region_cache *regions) {
    static const unsigned int attempts[3] = {3, 1, 1};
    static const unsigned long radii[3] = {5, 2, 4};
    static const int reach[3] = {2, 1, 2};
    maage_block_state_id materials[3];
    const maage_block_state_definition *state;
    maage_generation_column anchor;
    unsigned long random;
    unsigned int pass, attempt;
    int gx, gz, ix, iz, cx, cz, dx, dz, radius, depth, layer, y, eligible = 0;
    depth = maage_generation_soil_depth(seed, x, z);
    for (layer = 0; layer < MAAGE_GENERATION_SOIL_LAYERS; ++layer) {
        sample->soil[layer] = layer ? sample->filler : sample->surface;
        if (layer > depth) continue;
        state = maage_block_registry_state(ids->blocks, sample->soil[layer]);
        if (!state) return 0;
        if (state->block_id == ids->block_dirt || state->block_id == ids->block_grass || state->block_id == ids->block_clay) eligible = 1;
    }
    if (!eligible || sample->height - depth > policy->sea_level + 2) return 1;
    materials[0] = ids->state_sand;
    materials[1] = ids->state_clay;
    materials[2] = ids->state_gravel;
    gx = maage_generation_region(x, 16);
    gz = maage_generation_region(z, 16);
    for (pass = 0; pass < 3; ++pass)
        for (iz = -1; iz <= 1; ++iz)
            for (ix = -1; ix <= 1; ++ix)
                for (attempt = 0; attempt < attempts[pass]; ++attempt) {
                    random = maage_generation_hash(seed, gx + ix, gz + iz, 24000UL + pass * 16UL + attempt);
                    cx = (gx + ix) * 16 + (int) (random & 15UL);
                    cz = (gz + iz) * 16 + (int) ((random >> 4) & 15UL);
                    radius = 2 + (int) ((random >> 8) % radii[pass]);
                    dx = x - cx;
                    dz = z - cz;
                    if (dx * dx + dz * dz > radius * radius) continue;
                    if (!maage_generation_column_at(ids, seed, biomes, policy, cx, cz, &anchor, NULL, regions)) return 0;
                    if (anchor.height >= policy->sea_level) continue;
                    for (layer = 0; layer <= depth; ++layer) {
                        y = sample->height - layer;
                        if (abs(y - anchor.height - 1) > reach[pass]) continue;
                        state = maage_block_registry_state(ids->blocks, sample->soil[layer]);
                        if (!state) return 0;
                        if (state->block_id != ids->block_dirt && (pass == 1 ? state->block_id != ids->block_clay : state->block_id != ids->block_grass)) continue;
                        sample->soil[layer] = materials[pass];
                    }
                }
    sample->surface = sample->soil[0];
    return 1;
}
int maage_generation_sample_at(const maage_base_ids *ids, maage_world_seed seed, const maage_biome_registry *biomes, const maage_generation_policy *policy, int x, int z, maage_generation_sample *sample, maage_generation_region_cache *regions) {
    maage_generation_column column;
    maage_generation_environment environment;
    const maage_biome_definition *definition;
    maage_generation_sample_entry *cached = maage_generation_sample_entry_at(regions, x, z);
    unsigned int i;
    if (!sample || !maage_generation_policy_valid(policy)) return 0;
    if (cached && cached->valid && cached->complete && cached->x == x && cached->z == z) {
        *sample = cached->sample;
        return 1;
    }
    if (!maage_generation_column_at(ids, seed, biomes, policy, x, z, &column, &environment, regions)) return 0;
    definition = maage_biome_registry_get(biomes, column.biome);
    if (!definition || definition->generation_data_size != sizeof(maage_base_biome_decoration) || !definition->generation_data) return 0;
    memset(sample, 0, sizeof(*sample));
    sample->height = column.height;
    sample->biome = column.biome;
    /* Surface, filler and persisted identity always agree. Environmental
     * interpolation affects the boundary's ecology, never categorical blocks. */
    sample->surface = definition->surface;
    sample->filler = definition->filler;
    /* A small coherent offset in equivalent elevation varies the snowline
     * without a separate snow/ice/vegetation random decision. */
    sample->plants = maage_generation_noise(seed, x / 160.0, z / 160.0, 2300UL);
    sample->lapse_offset = 4 * maage_generation_fractal(seed, x / 16.0, z / 16.0, 1400UL, 1);
    for (i = 0; i < MAAGE_GENERATION_REGION_SAMPLES; ++i)
        if (environment.weights[i] > 0 && !maage_generation_influence(ids, sample, biomes, environment.regions[i].supported_biome, environment.regions[i].supported_climate, environment.weights[i])) return 0;
    /* Overlays suppress terrestrial decoration but inherit neighboring
     * climate. Flow keeps the cold channel center open while its shallow
     * margins can freeze, independently of the river biome's full footprint. */
    if (column.biome == ids->biome_river || column.biome == ids->biome_frozen_river || column.biome == ids->biome_beach || column.biome == ids->biome_cold_beach || column.biome == ids->biome_stone_beach) {
        sample->decoration = *(const maage_base_biome_decoration *) definition->generation_data;
        if (environment.flow > 0 && sample->temperature < 0.25) sample->temperature = maage_generation_lerp(sample->temperature, 0.25, environment.flow);
    }
    /* Shallow submerged land uses its soil; only sufficiently deep ground
     * exposes gravel over stone. Shallow coasts retain their own sand/rock. */
    if (sample->height < policy->sea_level - 1 && column.biome != ids->biome_stone_beach) sample->surface = sample->filler;
    if (sample->height < policy->sea_level - 7 - maage_generation_soil_depth(seed, x, z) && column.biome != ids->biome_stone_beach) {
        sample->surface = ids->state_gravel;
        sample->filler = ids->state_stone;
    }
    if (!maage_generation_sediment(ids, seed, biomes, policy, x, z, sample, regions)) return 0;
    if (cached) {
        cached->x = x;
        cached->z = z;
        cached->valid = cached->complete = 1;
        cached->sample = *sample;
    }
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
    const maage_base_ids *ids = context->ids;
    int x, z, top;
    maage_status result;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_CAVES);
    if (!maage_status_failed(result)) result = maage_generation_carve(context);
    if (maage_status_failed(result)) return result;
    result = maage_generation_features_run(context->blocks, MAAGE_GENERATION_STAGE_CAVES, context->chunk, context->seed, context->biomes, context->policy, context->token, context->trace);
    if (maage_status_failed(result)) return result;
    *context->output.stages |= 1U << MAAGE_GENERATION_STAGE_CAVES;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_UNDERGROUND);
    if (!maage_status_failed(result)) result = maage_generation_underground(context);
    if (maage_status_failed(result)) return result;
    result = maage_generation_features_run(context->blocks, MAAGE_GENERATION_STAGE_UNDERGROUND, context->chunk, context->seed, context->biomes, context->policy, context->token, context->trace);
    if (maage_status_failed(result)) return result;
    *context->output.stages |= 1U << MAAGE_GENERATION_STAGE_UNDERGROUND;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_SURFACE);
    if (!maage_status_failed(result)) result = maage_generation_decorate(context);
    if (maage_status_failed(result)) return result;
    result = maage_generation_features_run(context->blocks, MAAGE_GENERATION_STAGE_SURFACE, context->chunk, context->seed, context->biomes, context->policy, context->token, context->trace);
    if (maage_status_failed(result)) return result;
    *context->output.stages |= 1U << MAAGE_GENERATION_STAGE_SURFACE;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_VEGETATION);
    if (!maage_status_failed(result)) result = maage_generation_vegetation(context);
    if (maage_status_failed(result)) return result;
    result = maage_generation_features_run(context->blocks, MAAGE_GENERATION_STAGE_VEGETATION, context->chunk, context->seed, context->biomes, context->policy, context->token, context->trace);
    if (maage_status_failed(result)) return result;
    *context->output.stages |= 1U << MAAGE_GENERATION_STAGE_VEGETATION;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_STRUCTURES);
    if (!maage_status_failed(result)) result = maage_generation_structures(context);
    if (maage_status_failed(result)) return result;
    result = maage_generation_features_run(context->blocks, MAAGE_GENERATION_STAGE_STRUCTURES, context->chunk, context->seed, context->biomes, context->policy, context->token, context->trace);
    if (maage_status_failed(result)) return result;
    *context->output.stages |= 1U << MAAGE_GENERATION_STAGE_STRUCTURES;
    result = maage_generation_features_run(context->blocks, MAAGE_GENERATION_STAGE_FINALIZE, context->chunk, context->seed, context->biomes, context->policy, context->token, context->trace);
    if (maage_status_failed(result)) return result;
    result = maage_generation_stage_begin(context, MAAGE_GENERATION_STAGE_FINALIZE);
    if (maage_status_failed(result)) return result;
    for (z = 0; z < 16; ++z)
        for (x = 0; x < 16; ++x) {
            top = MAAGE_WORLD_HEIGHT;
            while (top > 0 && maage_chunk_get(context->chunk, x, top - 1, z) == ids->state_air)
                --top;
            context->output.heightmap[z * 16 + x] = (unsigned short) top;
        }
    *context->output.stages |= 1U << MAAGE_GENERATION_STAGE_FINALIZE;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_GENERATE, 0);
}

maage_status maage_base_generate(maage_chunk *chunk, maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, const maage_job_token *token, maage_status_trace *trace) {
    const maage_base_ids *ids = maage_base_generation_references(blocks);
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
    context.ids = ids;
    memset(&regions, 0, sizeof(regions));
    context.regions = &regions;
    context.trace = trace;
    context.token = token;
    result = maage_chunk_generation_output(chunk, &context.output);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE);
    if (!ids || !maage_generation_policy_valid(policy) || chunk == NULL ||
        !maage_block_registry_is_finalized(blocks) ||
        biomes == NULL || !biomes->finalized ||
        context.output.x < -MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE ||
        context.output.x >= MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE ||
        context.output.z < -MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE ||
        context.output.z >= MAAGE_WORLD_LIMIT / MAAGE_CHUNK_EDGE) return maage_generation_outcome(&context, MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_GENERATE);
    context.chunk = chunk;
    context.columns = columns;
    context.seed = seed;
    context.blocks = blocks;
    context.biomes = biomes;
    context.policy = policy;
    context.token = token;
    if (!maage_generation_features_valid(ids, blocks)) return maage_generation_outcome(&context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_GENERATE);
    result = maage_generation_stage_begin(&context, MAAGE_GENERATION_STAGE_BASE);
    if (maage_status_failed(result)) return result;
    for (z = 0; z < MAAGE_CHUNK_EDGE; ++z) {
        if (token && maage_job_cancelled(token)) return maage_generation_outcome(&context, MAAGE_STATUS_INTERRUPTED, MAAGE_OPERATION_GENERATE);
        for (x = 0; x < MAAGE_CHUNK_EDGE; ++x) {
            if (!maage_generation_sample_at(ids, seed, biomes, policy, context.output.x * MAAGE_CHUNK_EDGE + x, context.output.z * MAAGE_CHUNK_EDGE + z, &columns[z * MAAGE_CHUNK_EDGE + x], &regions)) return maage_generation_outcome(&context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_GENERATE);
            context.output.biomes[z * MAAGE_CHUNK_EDGE + x] = context.columns[z * MAAGE_CHUNK_EDGE + x].biome;
        }
    }
    result = maage_generation_features_run(blocks, MAAGE_GENERATION_STAGE_BASE, chunk, seed, biomes, policy, token, trace);
    if (maage_status_failed(result)) return result;
    *context.output.stages |= 1U << MAAGE_GENERATION_STAGE_BASE;
    result = maage_generation_stage_begin(&context, MAAGE_GENERATION_STAGE_TERRAIN);
    if (maage_status_failed(result)) return result;
    for (z = 0; z < MAAGE_CHUNK_EDGE; ++z) {
        if (token && maage_job_cancelled(token)) return maage_generation_outcome(&context, MAAGE_STATUS_INTERRUPTED, MAAGE_OPERATION_GENERATE);
        for (x = 0; x < MAAGE_CHUNK_EDGE; ++x) {
            wx = context.output.x * MAAGE_CHUNK_EDGE + x;
            wz = context.output.z * MAAGE_CHUNK_EDGE + z;
            column = &context.columns[z * MAAGE_CHUNK_EDGE + x];
            filler = maage_block_registry_state(blocks, column->filler);
            if (!maage_block_registry_state(blocks, column->surface) || !filler) return maage_generation_outcome(&context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_TERRAIN);
            top = column->height > policy->sea_level ? column->height : policy->sea_level;
            random = maage_generation_hash(seed, wx, wz, 700UL);
            depth = maage_generation_soil_depth(seed, wx, wz);
            for (y = 0; y <= top; ++y) {
                if (y > column->height) state = ids->state_water;
                else if (y == 0 || (y < 5 && ((random >> (y * 3)) & 7UL) >= (unsigned long) y)) state = ids->state_bedrock;
                else if (y == column->height) {
                    state = column->surface;
                } else if (y >= column->height - depth) state = column->soil[column->height - y];
                else if (filler->block_id == ids->block_sand && y >= column->height - depth - 2) state = ids->state_sandstone;
                else
                    state = ids->state_stone;
                if (!maage_block_registry_state(blocks, state)) return maage_generation_outcome(&context, MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_OPERATION_GENERATE);
                result = maage_chunk_set(chunk, x, y, z, state);
                if (maage_status_failed(result)) {
                    context.failure = result;
                    return maage_generation_outcome(&context, MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_GENERATE);
                }
            }
            context.output.heightmap[z * MAAGE_CHUNK_EDGE + x] = (unsigned short) (top + 1);
        }
    }
    result = maage_generation_features_run(blocks, MAAGE_GENERATION_STAGE_TERRAIN, chunk, seed, biomes, policy, token, trace);
    if (maage_status_failed(result)) return result;
    *context.output.stages |= 1U << MAAGE_GENERATION_STAGE_TERRAIN;
    return maage_generation_features(&context);
}

maage_status maage_base_spawn(maage_world_seed seed, const maage_block_registry *blocks, const maage_biome_registry *biomes, const maage_generation_policy *policy, double feet[3]) {
    const maage_base_ids *ids = maage_base_generation_references(blocks);
    maage_generation_column column;
    maage_generation_region_cache regions;
    int radius, side, offset, x, z, best_height, best_x, best_z;
    if (!ids || !feet || !maage_block_registry_is_finalized(blocks) || !biomes || !biomes->finalized || !maage_generation_policy_valid(policy)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
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
                if (maage_generation_column_at(ids, seed, biomes, policy, x, z, &column, NULL, &regions)) {
                    if (column.height > best_height) {
                        best_height = column.height;
                        best_x = x;
                        best_z = z;
                    }
                    if (column.height > policy->sea_level && !maage_biome_registry_get(biomes, column.biome)->aquatic && maage_generation_spawn_clear(ids, seed, blocks, biomes, policy, x, z, &regions)) {
                        feet[0] = x + 0.5;
                        feet[1] = column.height + 1.0;
                        if (!maage_generation_spawn_floor(ids, seed, blocks, biomes, policy, x, z, &feet[1], &regions)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
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
    if (!maage_generation_spawn_floor(ids, seed, blocks, biomes, policy, best_x, best_z, &feet[1], &regions)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_GENERATE, 0);
    if (feet[1] < policy->sea_level + 1.0) feet[1] = policy->sea_level + 1.0;
    feet[2] = best_z + 0.5;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_GENERATE, 0);
}
