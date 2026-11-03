#ifndef MAAGE_CONTENT_H
#define MAAGE_CONTENT_H
#include "maage/blocks.h"
#include "maage/execution.h"
#include "maage/presentation.h"
#include "maage/biome.h"
#include "maage/items.h"
#include "maage/creative.h"
#include "maage/crafting.h"
#include "maage/smelting.h"
#include "maage/fluids.h"
#include "maage/block_geometry.h"
#include "maage/containers.h"
#include "maage/sounds.h"
#include "maage/entity_types.h"
#include "maage/models.h"
#include "maage/traits.h"

typedef struct maage_content maage_content;
typedef struct maage_content_module maage_content_module;
typedef enum maage_content_phase {
    MAAGE_CONTENT_PHASE_MODULES = 0,
    MAAGE_CONTENT_PHASE_DEFINE,
    MAAGE_CONTENT_PHASE_BIND_INTERNAL,
    MAAGE_CONTENT_PHASE_BIND_EXTERNAL,
    MAAGE_CONTENT_PHASE_BIND_COMPATIBILITY,
    MAAGE_CONTENT_PHASE_FINALIZE,
    MAAGE_CONTENT_PHASE_LOAD,
    MAAGE_CONTENT_PHASE_INITIALIZE
} maage_content_phase;
/* Failure names borrow the submitted descriptors; consume diagnostics before
 * releasing those descriptors. NULL module means a shared registry failure. */
typedef struct maage_content_failure {
    const char *module_name, *dependency_name;
    maage_content_phase phase;
    maage_status result;
    const char *reason;     /* Static diagnostic, not an additional outcome. */
    char content_name[256]; /* Bounded diagnostic copy; long identities may be truncated. */
    maage_execution_context execution;
} maage_content_failure;
typedef struct maage_content_context {
    maage_traits_registry *traits;
    maage_presentation_registry *presentation;
    maage_block_registry *blocks;
    maage_biome_registry *biomes;
    maage_item_registry *items;
    maage_creative_registry *creative;
    maage_recipe_registry *recipes;
    maage_smelting_registry *smelting;
    maage_fuel_registry *fuels;
    maage_fluid_registry *fluids;
    maage_block_geometry_registry *geometry;
    maage_container_registry *containers;
    maage_sound_registry *sounds;
    maage_entity_type_registry *entity_types;
    maage_model_registry *models;
    void *owner; /* Private ownership list; use maage_content_own. */
    const struct maage_content_config *config;
    const maage_content_module *module; /* Current callback only; never retain context. */
    void *module_data;                  /* Preserved across this module's phases; own() owns its lifetime. */
    maage_execution_context execution;  /* Copy for runtime behavior; never retain context. */
    maage_content_phase phase;
    maage_status_trace *trace; /* Task-owned callback trace; never retain. */
    void *lifecycle;           /* Private composition state; use maage_content_module_data. */
} maage_content_context;
/* Registration copies definitions. Context data and callbacks must outlive all
 * readers/jobs; own() transfers data only on SUCCESS, destroyed after registries. */
maage_status maage_content_own(maage_content_context *context, void *data, void (*destroy)(void *));
typedef maage_status (*maage_content_registration)(maage_content_context *context);
struct maage_content_module {
    const char *name; /* Root, or root:submodule with an explicit root dependency. */
    const char *const *dependencies;
    size_t dependency_count;
    maage_content_registration define;                                           /* Logical definitions only. */
    maage_content_registration bind_internal, bind_external, bind_compatibility; /* Numeric bindings before finalization. */
    /* Optional runtime-side binding opportunities, at each corresponding stage.
     * Zero sides means no side hooks or initializer. EXTERNAL additionally requires dependencies;
     * a non-NULL COMPATIBILITY callback explicitly declares compatibility work. */
    unsigned int sides, required_capabilities;
    maage_content_registration side_bind_internal, side_bind_external, side_bind_compatibility;
    maage_content_registration initialize; /* Once, after immutable finalization; after LOAD on Client. */
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
    maage_execution_context execution;  /* Required; explicit side and facilities. */
    const maage_content *identity_peer; /* Optional exact numeric identity check before initialize. */
    maage_content_failure *failure;     /* Optional caller-owned creation failure output. */
} maage_content_config;
/* Stable dependency-ordered DEFINE/binding/FINALIZE. Server initializes after
 * identity validation; Client defers initialization until LOAD is complete.
 * Descriptor arrays/code borrow for content lifetime. Logical definitions are
 * immutable; physical resources belong to independently owned presentation views. */
maage_status maage_content_create(const maage_content_config *config, maage_content **output, maage_status_trace *trace);
/* Client LOAD completion can be repeated; initialize is once per composition.
 * Both calls run on the owner thread. Client initialization requires LOAD. */
maage_status maage_content_load_complete(maage_content *content, maage_status_trace *trace);
maage_status maage_content_initialize(maage_content *content, maage_status_trace *trace);
typedef struct maage_content_view maage_content_view;
typedef struct maage_content_view_config {
    maage_content_presentation_resolver resolver;
    maage_content_appearance_resolver appearance_resolver;
    void *context;
    const maage_biome_definition *biomes; /* Copied presentation colors, indexed by unchanged biome ID. */
    size_t biome_count;
} maage_content_view_config;
/* No registration or finalization: shares types/items/names/behavior and copies
 * only resolved state appearances, geometry appearances and biome presentation.
 * Immutable views and their logical content outlive every borrowing job/mesh. */
maage_status maage_content_view_create(const maage_content *content, const maage_content_view_config *config, maage_content_view **output, maage_status_trace *trace);
const maage_block_registry *maage_content_view_blocks(const maage_content_view *view);
const maage_biome_registry *maage_content_view_biomes(const maage_content_view *view);
maage_status maage_content_view_destroy(maage_content_view *view);
const maage_traits_registry *maage_content_traits(const maage_content *content);
const maage_presentation_registry *maage_content_presentation(const maage_content *content);
const maage_block_registry *maage_content_blocks(const maage_content *content);
const maage_biome_registry *maage_content_biomes(const maage_content *content);
const maage_item_registry *maage_content_items(const maage_content *content);
const maage_creative_registry *maage_content_creative(const maage_content *content);
const maage_recipe_registry *maage_content_recipes(const maage_content *content);
const maage_smelting_registry *maage_content_smelting(const maage_content *content);
const maage_fuel_registry *maage_content_fuels(const maage_content *content);
const maage_sound_registry *maage_content_sounds(const maage_content *content);
const maage_entity_type_registry *maage_content_entity_types(const maage_content *content);
const maage_model_registry *maage_content_models(const maage_content *content);
const maage_container_registry *maage_content_containers(const maage_content *content);
const maage_fluid_registry *maage_content_fluids(const maage_content *content);
const maage_block_geometry_registry *maage_content_geometry(const maage_content *content);
/* Boundary-only validation. Rejects differing numeric identity maps before any
 * local session traffic; side-only networked identities need future negotiation. */
maage_status maage_content_validate_identity(const maage_content *content, const maage_content *peer, maage_status_trace *trace);
const maage_execution_context *maage_content_execution(const maage_content *content);
const char *maage_content_phase_name(maage_content_phase phase);
/* Descriptors remain borrowed for content lifetime so another runtime can compose
 * the same submitted module set independently. No callback state is shared. */
maage_status maage_content_composition(const maage_content *content, const maage_content_module *const **modules, size_t *count);
maage_status maage_content_destroy(maage_content *content);
#endif
