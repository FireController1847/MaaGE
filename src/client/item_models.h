#ifndef MAAGE_CLIENT_ITEM_MODELS_H
#define MAAGE_CLIENT_ITEM_MODELS_H
#include "resources.h"
#include "maage/render.h"
/* One initialization-resolved presentation path for GUI, hand and entities.
 * Capacity is in quads, independent of the registered provider's complexity. */
maage_status maage_client_item_model_size(const maage_client_resources *resources, const maage_item_registry *items, const maage_block_registry *blocks, const maage_item_stack *stack, size_t *faces);
maage_status maage_client_item_model(const maage_client_resources *resources, const maage_item_registry *items, const maage_block_registry *blocks, const maage_item_stack *stack, maage_mesh_vertex *vertices, unsigned int *indices, size_t face_capacity, maage_mesh_data *data);
/* Creates exact-sized owned arrays, released by model_destroy after upload.
 * Allocation is at cache replacement, never per displayed frame or block. */
maage_status maage_client_item_model_create(const maage_client_resources *resources, const maage_item_registry *items, const maage_block_registry *blocks, const maage_item_stack *stack, maage_mesh_data *data);
maage_status maage_client_item_model_destroy(maage_mesh_data *data);
void maage_client_item_box(const float grass_color[3], maage_mesh_vertex *vertices, unsigned int *indices, size_t *faces, const double minimum[3], const double maximum[3], const maage_block_state_definition *state, float material);
/* Streams projected quads in 16x16 slot coordinates and world-atlas UVs;
 * consumer batching owns storage and failures stop generation immediately. */
typedef maage_status (*maage_client_item_icon_sink)(void *context, const maage_render_ui_vertex vertices[4]);
maage_status maage_client_item_icon(const maage_model_registry *models, const maage_item_registry *items, const maage_block_registry *blocks, const maage_item_stack *stack, const float grass[3], const float (*textures)[4], size_t texture_count, maage_client_item_icon_sink sink, void *context);
typedef struct maage_client_item_icon_cache maage_client_item_icon_cache;
/* Registered model icons retain their projected quads for one resource
 * generation. Lazy creation occurs once per encountered numeric variant. */
maage_status maage_client_item_icon_cached(maage_client_item_icon_cache **, const maage_model_registry *, const maage_item_registry *, const maage_block_registry *, const maage_item_stack *, const float[3], const float (*)[4], size_t, maage_client_item_icon_sink, void *);
void maage_client_item_icon_cache_destroy(maage_client_item_icon_cache *);
#endif
