#ifndef MAAGE_CONTENT_H
#define MAAGE_CONTENT_H
#include "maage/blocks.h"
#include "maage/presentation.h"
#include "maage/biome.h"
#include "maage/items.h"
#include "maage/crafting.h"
#include "maage/smelting.h"
#include "maage/fluids.h"
#include "maage/block_geometry.h"
#include "maage/containers.h"
#include "maage/sounds.h"

typedef struct maage_content maage_content;
typedef struct maage_content_module maage_content_module;
typedef enum maage_content_phase {
    MAAGE_CONTENT_PHASE_MODULES = 0,
    MAAGE_CONTENT_PHASE_DEFINE,
    MAAGE_CONTENT_PHASE_RESOLVE,
    MAAGE_CONTENT_PHASE_FINALIZE,
    MAAGE_CONTENT_PHASE_INITIALIZE
} maage_content_phase;
/* Failure names borrow the submitted descriptors; consume diagnostics before
 * releasing those descriptors. NULL module means a shared registry failure. */
typedef struct maage_content_failure {
    const char *module_name, *dependency_name;
    maage_content_phase phase;
    maage_status result;
} maage_content_failure;
typedef struct maage_content_context {
    maage_presentation_registry *presentation;
    maage_block_registry *blocks;
    maage_biome_registry *biomes;
    maage_item_registry *items;
    maage_recipe_registry *recipes;
    maage_smelting_registry *smelting;
    maage_fuel_registry *fuels;
    maage_fluid_registry *fluids;
    maage_block_geometry_registry *geometry;
    maage_container_registry *containers;
    maage_sound_registry *sounds;
    void *owner; /* Private ownership list; use maage_content_own. */
    const struct maage_content_config *config;
    const maage_content_module *module; /* Current callback only; never retain context. */
    void *module_data;                  /* Preserved across this module's phases; own() owns its lifetime. */
    void *lifecycle;                    /* Private composition state; use maage_content_module_data. */
} maage_content_context;
/* Registration copies definitions. Context data and callbacks must outlive all
 * readers/jobs; own() transfers data only on SUCCESS, destroyed after registries. */
maage_status maage_content_own(maage_content_context *context, void *data, void (*destroy)(void *));
typedef maage_status (*maage_content_registration)(maage_content_context *context);
struct maage_content_module {
    const char *name; /* Unique namespaced identity, independent of registration position. */
    const char *const *dependencies;
    size_t dependency_count;
    maage_content_registration define;     /* Independent definitions; dependencies have defined first. */
    maage_content_registration resolve;    /* All modules have defined; bind numeric cross-references. */
    maage_content_registration initialize; /* All registries are immutable; prepare runtime data. */
};
/* Registration/initialization boundary lookup only. A known module may publish
 * NULL data; missing modules fail explicitly. Resolve content names through the
 * public registries, and retain numeric references for allocation-free runtime use. */
maage_status maage_content_module_data(const maage_content_context *context, const char *name, void **data);
/* The logical state is read-only. Only copied face presentation and opaque
 * foliage UVs can change; numeric texture identities must remain unchanged. */
typedef maage_status (*maage_content_presentation_resolver)(void *context, const maage_block_state_definition *state, maage_block_face_definition faces[MAAGE_FACE_COUNT], float leaf_fast_uv[4]);
/* Resolve one copied provider-owned appearance without requiring a block
 * state. Numeric texture identities remain unchanged, as for state faces. */
typedef maage_status (*maage_content_appearance_resolver)(void *context, const maage_block_face_definition *logical, maage_block_face_definition *resolved);
typedef struct maage_content_config {
    const maage_content_module *const *modules;
    size_t module_count;
    maage_content_failure *failure;               /* Optional caller-owned creation failure output. */
    maage_content_presentation_resolver resolver; /* NULL = headless */
    maage_content_appearance_resolver appearance_resolver; /* NULL for headless. */
    void *resolver_context;
    const maage_biome_registry *presentation_biomes;
} maage_content_config;
/* Validate the complete dependency graph before callbacks, then execute all
 * define callbacks, all resolve callbacks, finalization, and all initialize
 * callbacks. Stable topological order chooses the earliest submitted ready
 * module, preserving deterministic numeric registration order across frontends.
 * Arrays, descriptors and config borrow only through this call. Callbacks and
 * registered behavior data outlive all registry users/jobs; own() can retain data.
 * The engine supplies no built-in game. output resets to NULL on failure.
 * Destroy after World and its jobs stop. */
maage_status maage_content_create(const maage_content_config *config, maage_content **output, maage_status_trace *trace);
const maage_presentation_registry *maage_content_presentation(const maage_content *content);
const maage_block_registry *maage_content_blocks(const maage_content *content);
const maage_biome_registry *maage_content_biomes(const maage_content *content);
const maage_item_registry *maage_content_items(const maage_content *content);
const maage_recipe_registry *maage_content_recipes(const maage_content *content);
const maage_smelting_registry *maage_content_smelting(const maage_content *content);
const maage_fuel_registry *maage_content_fuels(const maage_content *content);
const maage_sound_registry *maage_content_sounds(const maage_content *content);
const maage_container_registry *maage_content_containers(const maage_content *content);
const maage_fluid_registry *maage_content_fluids(const maage_content *content);
const maage_block_geometry_registry *maage_content_geometry(const maage_content *content);
maage_status maage_content_destroy(maage_content *content);
#endif
