#include <maage/world.h>

#include <stdlib.h>

#define MAAGE_WORLD_CHUNKS 3
#define MAAGE_WORLD_SPAN (MAAGE_WORLD_CHUNKS * MAAGE_CHUNK_EDGE)
#define MAAGE_SECTION_VOLUME (MAAGE_CHUNK_EDGE * MAAGE_CHUNK_EDGE * MAAGE_CHUNK_EDGE)

struct maage_world {
    const maage_block_registry *registry;
    maage_block_state_id sections[MAAGE_WORLD_CHUNKS * MAAGE_WORLD_CHUNKS]
        [MAAGE_SECTION_VOLUME];
};

static size_t maage_world_section(int x, int z)
{
    return (size_t)(z / MAAGE_CHUNK_EDGE * MAAGE_WORLD_CHUNKS +
        x / MAAGE_CHUNK_EDGE);
}

static size_t maage_world_cell(int x, int y, int z)
{
    return (size_t)((z % MAAGE_CHUNK_EDGE * MAAGE_CHUNK_EDGE + y) *
        MAAGE_CHUNK_EDGE + x % MAAGE_CHUNK_EDGE);
}

static int maage_world_surface(int x, int z)
{
    int height;
    height = 4;
    if (x >= 30 && x <= 41 && z >= 22 && z <= 38) ++height;
    if (x >= 32 && x <= 39 && z >= 24 && z <= 35) ++height;
    if (x >= 8 && x <= 17 && z >= 9 && z <= 18) ++height;
    return height;
}

maage_world *maage_world_create_static(const maage_block_registry *registry)
{
    maage_world *world;
    const maage_block_state_definition *state;
    int x;
    int y;
    int z;
    int height;
    maage_block_state_id id;
    if (!maage_block_registry_is_finalized(registry)) return NULL;
    for (id = MAAGE_STATE_AIR; id <= MAAGE_STATE_DIRT; ++id) {
        state = maage_block_registry_state(registry, id);
        if (state == NULL || state->block_id != id) return NULL;
    }
    world = (maage_world *)calloc(1, sizeof(*world));
    if (world == NULL) return NULL;
    world->registry = registry;
    for (z = 0; z < MAAGE_WORLD_SPAN; ++z) {
        for (x = 0; x < MAAGE_WORLD_SPAN; ++x) {
            height = maage_world_surface(x, z);
            for (y = 0; y <= height; ++y) {
                id = MAAGE_STATE_STONE;
                if (y == height) id = MAAGE_STATE_GRASS;
                else if (y >= height - 2) id = MAAGE_STATE_DIRT;
                world->sections[maage_world_section(x, z)]
                    [maage_world_cell(x, y, z)] = id;
            }
        }
    }
    return world;
}

void maage_world_destroy(maage_world *world)
{
    free(world);
}

const maage_block_registry *maage_world_registry(const maage_world *world)
{
    return world != NULL ? world->registry : NULL;
}

void maage_world_bounds(const maage_world *world, int minimum[3], int maximum[3])
{
    if (minimum != NULL) minimum[0] = minimum[1] = minimum[2] = 0;
    if (maximum == NULL) return;
    maximum[0] = maximum[2] = world != NULL ? MAAGE_WORLD_SPAN : 0;
    maximum[1] = world != NULL ? MAAGE_CHUNK_EDGE : 0;
}

void maage_world_spawn(const maage_world *world, double feet[3])
{
    if (feet == NULL) return;
    feet[0] = feet[2] = MAAGE_WORLD_SPAN / 2 + 0.5;
    feet[1] = world != NULL ?
        maage_world_surface(MAAGE_WORLD_SPAN / 2, MAAGE_WORLD_SPAN / 2) + 1.01 :
        0.0;
}

maage_block_state_id maage_world_get(const maage_world *world, int x, int y, int z)
{
    if (world == NULL || x < 0 || y < 0 || z < 0 || x >= MAAGE_WORLD_SPAN ||
        y >= MAAGE_CHUNK_EDGE || z >= MAAGE_WORLD_SPAN) return MAAGE_STATE_AIR;
    return world->sections[maage_world_section(x, z)][maage_world_cell(x, y, z)];
}

int maage_world_solid(const maage_world *world, int x, int y, int z)
{
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    if (world == NULL) return 1;
    if (x < 0 || z < 0 || x >= MAAGE_WORLD_SPAN || z >= MAAGE_WORLD_SPAN ||
        y < 0) return 1;
    state = maage_block_registry_state(world->registry,
        maage_world_get(world, x, y, z));
    if (state == NULL) return 1;
    block = maage_block_registry_block(world->registry, state->block_id);
    /* Invalid data blocks motion instead of silently masquerading as air. */
    return block == NULL || block->solid;
}
