#include "world_internal.h"
#include <stdlib.h>

int maage_world_chunk_coordinate(int coordinate)
{
    int quotient = coordinate / MAAGE_CHUNK_EDGE;
    if (coordinate % MAAGE_CHUNK_EDGE < 0) --quotient;
    return quotient;
}
int maage_world_local_coordinate(int coordinate)
{
    int local = coordinate % MAAGE_CHUNK_EDGE;
    return local < 0 ? local + MAAGE_CHUNK_EDGE : local;
}
size_t maage_chunk_cell(int x, int y, int z)
{
    return (size_t)(((y % 16) * 16 + z) * 16 + x);
}
maage_chunk *maage_chunk_create(int x, int z)
{
    maage_chunk *chunk = (maage_chunk *)calloc(1, sizeof(*chunk));
    if (chunk) { chunk->x = x; chunk->z = z; chunk->state = MAAGE_CHUNK_REQUESTED; }
    return chunk;
}
void maage_chunk_destroy(maage_chunk *chunk)
{
    int section;
    if (!chunk) return;
    for (section = 0; section < 16; ++section) free(chunk->sections[section]);
    maage_world_mesh_destroy(chunk->pending_mesh);
    free(chunk);
}
maage_block_state_id maage_chunk_get(const maage_chunk *chunk, int x, int y, int z)
{
    if (!chunk || x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= 256 || !chunk->sections[y / 16])
        return MAAGE_STATE_AIR;
    return chunk->sections[y / 16]->states[maage_chunk_cell(x, y, z)];
}
int maage_chunk_set(maage_chunk *chunk, int x, int y, int z, maage_block_state_id state)
{
    maage_section *section;
    if (!chunk || x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= 256) return 0;
    section = chunk->sections[y / 16];
    if (!section && state == MAAGE_STATE_AIR) return 1;
    if (!section) {
        section = (maage_section *)calloc(1, sizeof(*section));
        if (!section) return 0;
        chunk->sections[y / 16] = section;
    }
    section->states[maage_chunk_cell(x, y, z)] = state;
    return 1;
}
unsigned char maage_chunk_light(const maage_chunk *chunk, int x, int y, int z)
{
    if (!chunk || x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0) return 0;
    if (y >= 256) return 0xf0;
    if (chunk->sections[y / 16]) return chunk->sections[y / 16]->light[maage_chunk_cell(x, y, z)];
    return y >= chunk->heightmap[z * 16 + x] ? 0xf0 : 0;
}
