#ifndef MAAGE_CLIENT_MODELS_H
#define MAAGE_CLIENT_MODELS_H
#include "resources.h"
#include "maage/models.h"
#include "maage/render.h"
/* Geometry uses local model pixels; instances supply scale and hierarchy.
 * Owned CPU arrays are created only when an immutable cache variant is first
 * encountered. Resource reload destroys all dependent caches. */
maage_status maage_client_model_part_create(const maage_client_resources *, const maage_model_definition *, size_t, maage_texture_id, unsigned long, maage_mesh_data *);
#endif
