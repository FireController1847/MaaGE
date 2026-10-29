#ifndef MAAGE_CLIENT_ITEM_MODELS_H
#define MAAGE_CLIENT_ITEM_MODELS_H
#include "resources.h"
#include "maage/render.h"
#define MAAGE_ITEM_MODEL_FACES 66U
/* One initialization-resolved presentation path for GUI, hand and entities. */
maage_status maage_client_item_model(const maage_client_resources *resources,
                                     const maage_item_registry *items, const maage_block_registry *blocks,
                                     const maage_item_stack *stack, maage_mesh_vertex *vertices, unsigned int *indices,
                                     maage_mesh_data *data);
void maage_client_item_box(const float grass_color[3], maage_mesh_vertex *vertices,
                           unsigned int *indices, size_t *faces, const double minimum[3], const double maximum[3],
                           const maage_block_state_definition *state, float material);
/* Returns up to six quads, in 16x16 slot coordinates and world-atlas UVs. */
size_t maage_client_item_icon(const maage_item_registry *items,
                              const maage_block_registry *blocks, const maage_item_stack *stack,
                              const float grass[3], const float textures[MAAGE_CONTENT_TEXTURE_COUNT][4],
                              maage_render_ui_vertex vertices[24]);
#endif
