#ifndef MAAGE_CONTENT_H
#define MAAGE_CONTENT_H
#include "maage/blocks.h"
#include "maage/presentation.h"
#include "maage/biome.h"
#include "maage/items.h"
#include "maage/crafting.h"
#include "maage/smelting.h"

typedef struct maage_content maage_content;
typedef struct maage_content_context {
    maage_presentation_registry *presentation;
    maage_block_registry *blocks;
    maage_biome_registry *biomes;
    maage_item_registry *items;
    maage_recipe_registry *recipes;
    maage_smelting_registry *smelting;
    maage_fuel_registry *fuels;
    void *owner; /* Private ownership list; use maage_content_own. */
    const struct maage_content_config *config;
} maage_content_context;
/* Registration copies definitions. Context data and callbacks must outlive all
 * readers/jobs; own() transfers data only on SUCCESS, destroyed after registries. */
maage_status maage_content_own(maage_content_context *context, void *data, void (*destroy)(void *));
typedef maage_status (*maage_content_registration)(maage_content_context *context);
/* The logical state is read-only. Only copied face presentation and opaque
 * foliage UVs can change; numeric texture identities must remain unchanged. */
typedef maage_status (*maage_content_presentation_resolver)(void *context, const maage_block_state_definition *state, maage_block_face_definition faces[MAAGE_FACE_COUNT], float leaf_fast_uv[4]);
typedef struct maage_content_config {
    maage_content_registration register_content;
    maage_content_registration register_dependencies;
    maage_content_presentation_resolver resolver; /* NULL = headless */
    void *resolver_context;
    const maage_biome_registry *presentation_biomes;
} maage_content_config;
/* Empty registries -> content -> dependent recipes -> validation/finalization.
 * Callers explicitly select modules; the engine supplies no built-in game.
 * output resets to NULL on failure. Destroy after World and its jobs stop. */
maage_status maage_content_create(const maage_content_config *config, maage_content **output, maage_status_trace *trace);
const maage_presentation_registry *maage_content_presentation(const maage_content *content);
const maage_block_registry *maage_content_blocks(const maage_content *content);
const maage_biome_registry *maage_content_biomes(const maage_content *content);
const maage_item_registry *maage_content_items(const maage_content *content);
const maage_recipe_registry *maage_content_recipes(const maage_content *content);
const maage_smelting_registry *maage_content_smelting(const maage_content *content);
const maage_fuel_registry *maage_content_fuels(const maage_content *content);
maage_status maage_content_destroy(maage_content *content);
#endif
