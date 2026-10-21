#include "world_internal.h"
#include <stdlib.h>
#include <string.h>

/* Small, fully synthetic chunks ensure mode-specific culling is independent
 * of external Minecraft resources and does not require the graphics backend. */
#define REQUIRE(condition) do { if (!(condition)) abort(); } while (0)
#define CHECK(call) do { maage_status result = (call); REQUIRE(!maage_status_failed(result)); } while (0)

static void register_block(maage_block_registry *registry, maage_block_id id,
    const char *name, int opaque)
{
    maage_block_definition block;
    maage_block_state_definition state;
    unsigned int face;
    memset(&block, 0, sizeof(block));
    block.id = id;
    block.opaque = opaque;
    CHECK(maage_block_registry_register_block(registry, name, &block));
    memset(&state, 0, sizeof(state));
    state.id = id; state.block_id = id;
    for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
        state.faces[face].uv[0] = 0.1f;
        state.faces[face].uv[1] = 0.2f;
        state.faces[face].uv[2] = 0.3f;
        state.faces[face].uv[3] = 0.4f;
        state.faces[face].tint[0] = 0.4f;
        state.faces[face].tint[1] = 0.8f;
        state.faces[face].tint[2] = 0.3f;
        state.faces[face].tint[3] = 1.0f;
    }
    if (id == MAAGE_BLOCK_LEAVES) {
        state.leaf_fast_uv[0] = 0.6f;
        state.leaf_fast_uv[1] = 0.65f;
        state.leaf_fast_uv[2] = 0.7f;
        state.leaf_fast_uv[3] = 0.75f;
    }
    CHECK(maage_block_registry_register_state(registry, &state));
}

static void verify_mesh(const maage_block_registry *blocks,
    const maage_biome_registry *biomes, const maage_chunk *center,
    const maage_chunk *east, maage_leaves_mode mode, size_t faces,
    float first_u)
{
    const maage_chunk *neighbors[4];
    const maage_mesh_data *data;
    maage_world_mesh *mesh = NULL;
    memset(neighbors, 0, sizeof(neighbors));
    neighbors[1] = east;
    CHECK(maage_world_mesh_create_chunk_job(blocks, biomes, center, neighbors,
        mode, NULL, &mesh, NULL));
    REQUIRE(mesh != NULL);
    data = maage_world_mesh_data(mesh);
    REQUIRE(data != NULL);
    REQUIRE(data->vertex_count == faces * 4);
    REQUIRE(data->index_count == faces * 6);
    if (faces) REQUIRE(data->vertices[0].uv[0] == first_u);
    CHECK(maage_world_mesh_destroy(mesh));
}

int main(void)
{
    maage_block_registry *blocks = NULL;
    maage_biome_registry biomes;
    maage_chunk *center = NULL, *east = NULL;
    CHECK(maage_block_registry_create(0, 0, &blocks));
    register_block(blocks, MAAGE_BLOCK_AIR, "minecraft:air", 0);
    register_block(blocks, MAAGE_BLOCK_STONE, "minecraft:stone", 1);
    register_block(blocks, MAAGE_BLOCK_LEAVES, "minecraft:leaves", 0);
    CHECK(maage_block_registry_finalize(blocks));
    CHECK(maage_biome_registry_init(&biomes));
    CHECK(maage_biome_registry_finalize(&biomes));

    /* A single leaf uses the alternate UV only in Fast mode. */
    CHECK(maage_chunk_create(0, 0, &center));
    CHECK(maage_chunk_set(center, 4, 8, 8, MAAGE_STATE_LEAVES));
    verify_mesh(blocks, &biomes, center, NULL, MAAGE_LEAVES_FAST, 6, 0.6f);
    verify_mesh(blocks, &biomes, center, NULL, MAAGE_LEAVES_SMART, 6, 0.1f);
    verify_mesh(blocks, &biomes, center, NULL, MAAGE_LEAVES_FANCY, 6, 0.1f);
    CHECK(maage_chunk_set(center, 5, 8, 8, MAAGE_STATE_LEAVES));
    verify_mesh(blocks, &biomes, center, NULL, MAAGE_LEAVES_FAST, 10, 0.6f);
    verify_mesh(blocks, &biomes, center, NULL, MAAGE_LEAVES_SMART, 10, 0.1f);
    verify_mesh(blocks, &biomes, center, NULL, MAAGE_LEAVES_FANCY, 12, 0.1f);
    CHECK(maage_chunk_destroy(center));

    /* Opaque stone hides the adjacent leaf face in every mode. In Fast,
     * solid-looking foliage also hides the reverse stone face. */
    CHECK(maage_chunk_create(0, 0, &center));
    CHECK(maage_chunk_set(center, 4, 8, 8, MAAGE_STATE_LEAVES));
    CHECK(maage_chunk_set(center, 5, 8, 8, MAAGE_STATE_STONE));
    verify_mesh(blocks, &biomes, center, NULL, MAAGE_LEAVES_SMART, 11, 0.1f);
    verify_mesh(blocks, &biomes, center, NULL, MAAGE_LEAVES_FANCY, 11, 0.1f);
    verify_mesh(blocks, &biomes, center, NULL, MAAGE_LEAVES_FAST, 10, 0.6f);
    CHECK(maage_chunk_destroy(center));

    /* Adjacent columns obey exactly the same shared-face rules; missing
     * neighbors leave a visible border face until the neighbor arrives. */
    CHECK(maage_chunk_create(0, 0, &center));
    CHECK(maage_chunk_create(1, 0, &east));
    CHECK(maage_chunk_set(center, 15, 8, 8, MAAGE_STATE_LEAVES));
    CHECK(maage_chunk_set(east, 0, 8, 8, MAAGE_STATE_LEAVES));
    verify_mesh(blocks, &biomes, center, NULL, MAAGE_LEAVES_SMART, 6, 0.1f);
    verify_mesh(blocks, &biomes, center, east, MAAGE_LEAVES_FAST, 5, 0.6f);
    verify_mesh(blocks, &biomes, center, east, MAAGE_LEAVES_SMART, 5, 0.1f);
    verify_mesh(blocks, &biomes, center, east, MAAGE_LEAVES_FANCY, 6, 0.1f);

    CHECK(maage_chunk_destroy(east));
    CHECK(maage_chunk_destroy(center));
    CHECK(maage_biome_registry_destroy(&biomes));
    CHECK(maage_block_registry_destroy(blocks));
    return 0;
}
