#ifndef MAAGE_WORLD_INTERNAL_H
#define MAAGE_WORLD_INTERNAL_H
#include "maage/world.h"
#include "maage/biome.h"
#include "maage/seed.h"
#define MAAGE_SECTION_VOLUME 4096
#define MAAGE_WORLD_GENERATOR_VERSION 1UL
#define MAAGE_WORLD_BLOCKSET_VERSION 1UL
/* All block/light arrays are numeric and owned by their column. Empty sections
 * are NULL; implicit air above the heightmap has skylight 15. */
typedef struct maage_section {
    maage_block_state_id states[MAAGE_SECTION_VOLUME];
    unsigned char light[MAAGE_SECTION_VOLUME];
} maage_section;
struct maage_chunk {
    int x, z;
    maage_section *sections[16];
    maage_biome_id biomes[256];
    unsigned short heightmap[256]; /* first air Y, 0..256 */
    maage_chunk_state state;
    unsigned long revision;
    int dirty, mesh_dirty, light_dirty, failed;
    maage_world_mesh *pending_mesh;
    struct maage_chunk *next;
};
maage_chunk *maage_chunk_create(int x, int z);
void maage_chunk_destroy(maage_chunk *chunk);
size_t maage_chunk_cell(int x, int y, int z);
maage_block_state_id maage_chunk_get(const maage_chunk *chunk, int x, int y, int z);
int maage_chunk_set(maage_chunk *chunk, int x, int y, int z, maage_block_state_id state);
unsigned char maage_chunk_light(const maage_chunk *chunk, int x, int y, int z);
/* Generation and initial lighting run on a detached column on the World worker.
 * All algorithms are version 1 approximations, not seed-compatible vanilla. */
int maage_generate_chunk(maage_chunk *chunk, maage_world_seed seed,
    const maage_block_registry *blocks, const maage_biome_registry *biomes);
void maage_generator_spawn(maage_world_seed seed, const maage_biome_registry *biomes, double feet[3]);
int maage_light_chunk(maage_chunk *chunk, const maage_block_registry *blocks,
    const maage_chunk *neighbors[4], maage_section *added[16]);
/* Neighbors are -X,+X,-Z,+Z, read-only. Newly needed air sections transfer via
 * added, including on failure; publish under the World mutex or destroy them. */
/* Called only by the single world worker: borrowed ready neighbors remain alive
 * until it finishes; Client retains only copied CPU/GPU geometry, never columns. */
maage_world_mesh *maage_world_mesh_create_chunk(const maage_block_registry *blocks,
    const maage_biome_registry *biomes, const maage_chunk *chunk,
    const maage_chunk *neighbors[4]);
#endif
