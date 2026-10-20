#ifndef MAAGE_WORLD_H
#define MAAGE_WORLD_H

#include <maage/blocks.h>
#include <maage/geometry.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAAGE_CHUNK_EDGE 16

typedef struct maage_world maage_world;
typedef struct maage_world_mesh maage_world_mesh;

/* A finite 3x3 collection of 16^3 sections, generated once from numeric states
 * AIR/STONE/GRASS/DIRT. The frozen registry is borrowed and must outlive world.
 * World and CPU mesh have no native, graphics, filesystem, or Client dependency. */
maage_world *maage_world_create_static(const maage_block_registry *registry);
void maage_world_destroy(maage_world *world);
const maage_block_registry *maage_world_registry(const maage_world *world);
/* Bounds are inclusive minimum and exclusive maximum, in block coordinates. */
void maage_world_bounds(const maage_world *world, int minimum[3], int maximum[3]);
void maage_world_spawn(const maage_world *world, double feet[3]);
/* Out-of-bounds reads are air, allowing visible exterior faces. The collision
 * query separately closes X/Z boundaries and everything below the world floor. */
maage_block_state_id maage_world_get(const maage_world *world, int x, int y, int z);
int maage_world_solid(const maage_world *world, int x, int y, int z);

/* Caller owns mesh; data is borrowed until destroy. Mesh vertices use world
 * coordinates, outward normals and CCW winding. An indexed quad has four shared
 * vertices. Neighbor queries cross section boundaries before emitting faces.
 * Construction makes two linear passes and allocates once, never per face. */
maage_world_mesh *maage_world_mesh_create(const maage_world *world);
const maage_mesh_data *maage_world_mesh_data(const maage_world_mesh *mesh);
void maage_world_mesh_destroy(maage_world_mesh *mesh);

#ifdef __cplusplus
}
#endif
#endif
