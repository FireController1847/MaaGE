#ifndef MAAGE_WORLD_H
#define MAAGE_WORLD_H
#include "maage/blocks.h"
#include "maage/geometry.h"
#include "maage/seed.h"
#include "maage/biome.h"
#include <limits.h>
/* The column/save domain needs 8-bit bytes, signed coordinates through 30M,
 * and allocations larger than 64 KiB. Narrower targets can still build Core. */
#if CHAR_BIT != 8 || INT_MAX < 30000000L
#error World requires 8-bit bytes and an int coordinate range through 30000000
#endif
typedef char maage_world_required_address_space[((size_t)-1 >= 262144UL) ? 1 : -1];
#define MAAGE_CHUNK_EDGE 16
#define MAAGE_WORLD_HEIGHT 256
#define MAAGE_WORLD_LIMIT 30000000
#define MAAGE_WORLD_MAX_RADIUS 8
#define MAAGE_WORLD_MAX_CHUNKS 512

#ifdef __cplusplus
extern "C" {
#endif
typedef struct maage_world maage_world;
typedef struct maage_chunk maage_chunk;
typedef struct maage_world_mesh maage_world_mesh;
typedef struct maage_world_store maage_world_store;
typedef enum maage_chunk_state {
    MAAGE_CHUNK_REQUESTED = 0, MAAGE_CHUNK_LOADING, MAAGE_CHUNK_GENERATING,
    MAAGE_CHUNK_GENERATED, MAAGE_CHUNK_LIGHTING, MAAGE_CHUNK_MESH_PENDING,
    MAAGE_CHUNK_READY, MAAGE_CHUNK_SAVING, MAAGE_CHUNK_UNLOADING, MAAGE_CHUNK_FAILED
} maage_chunk_state;
typedef struct maage_world_config {
    const maage_block_registry *registry;
    const maage_biome_registry *biomes;
    maage_world_seed seed;
    maage_world_store *store; /* borrowed; close after world destruction */
    int render_distance;
    int new_world;
    double position[3];
} maage_world_config;
typedef struct maage_world_stats {
    unsigned int loaded, pending, ready;
    unsigned long generated, loaded_from_disk, meshes;
    int failed;
} maage_world_stats;
typedef struct maage_world_mesh_update {
    int x, z;
    unsigned long revision;
    maage_world_mesh *mesh; /* ownership transfers to caller */
} maage_world_mesh_update;
/* Floor division/positive modulo, including negative world coordinates. */
int maage_world_chunk_coordinate(int coordinate);
int maage_world_local_coordinate(int coordinate);
/* One bounded worker owns generation/loading/lighting/CPU meshing/saving and
 * eviction. Fully initialized columns are published under a mutex. Never holds
 * graphical objects. Registry/biomes/store must outlive this World. */
maage_world *maage_world_create(const maage_world_config *config);
void maage_world_stream(maage_world *world, double x, double z, double yaw);
void maage_world_get_stats(maage_world *world, maage_world_stats *stats);
int maage_world_take_mesh(maage_world *world, maage_world_mesh_update *update);
int maage_world_chunk_loaded(maage_world *world, int x, int z);
/* Stop/join worker and save dirty columns. Returns zero on any failed save. */
int maage_world_flush(maage_world *world);
void maage_world_destroy(maage_world *world);
const maage_block_registry *maage_world_registry(const maage_world *world);
void maage_world_spawn(const maage_world *world, double feet[3]);
/* Missing/loading columns return air for visual queries but are solid barriers
 * for collision, preventing falls/teleports. Only complete columns are visible. */
maage_block_state_id maage_world_get(const maage_world *world, int x, int y, int z);
int maage_world_solid(const maage_world *world, int x, int y, int z);
const maage_mesh_data *maage_world_mesh_data(const maage_world_mesh *mesh);
void maage_world_mesh_destroy(maage_world_mesh *mesh);
#ifdef __cplusplus
}
#endif
#endif
