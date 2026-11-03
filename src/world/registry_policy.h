#ifndef MAAGE_REGISTRY_POLICY_H
#define MAAGE_REGISTRY_POLICY_H
#include "maage/content.h"

typedef enum content_domain {
    CONTENT_BLOCK,
    CONTENT_ITEM,
    CONTENT_BIOME,
    CONTENT_FLUID,
    CONTENT_GEOMETRY,
    CONTENT_CONTAINER,
    CONTENT_CREATIVE,
    CONTENT_SOUND_RESOURCE,
    CONTENT_SOUND,
    CONTENT_MATERIAL,
    CONTENT_MUSIC,
    CONTENT_AMBIENT,
    CONTENT_ENTITY,
    CONTENT_FEATURE,
    CONTENT_ORE,
    CONTENT_BLOCK_BINDINGS,
    CONTENT_SOUND_BINDINGS,
    CONTENT_PRESENTATION_BINDINGS,
    CONTENT_INVENTORY_BINDING
} content_domain;
typedef struct content_identity {
    char *name;
    const maage_content_module *owner;
    content_domain domain;
    unsigned long id;
} content_identity;
typedef struct content_policy {
    maage_content_context *context;
    content_identity *identities;
    size_t count, capacity, *buckets, bucket_count;
    maage_status failure;
    const char *reason, *dependency;
    char content_name[256];
    int sealed, engine;
} content_policy;
/* Scope is attached by composition, never a process-global current module.
 * Reservation failures poison composition even if a callback ignores them.
 * A failed registration must abort its callback; no rollback/replacement API. */
maage_status content_policy_register(content_policy *, content_domain, const char *, unsigned long);
maage_status content_policy_update(content_policy *, content_domain, const char *);
maage_status content_policy_read(content_policy *, content_domain, const char *);
const content_identity *content_policy_identity(const content_policy *, content_domain, const char *);
maage_status content_policy_finalize(content_policy *);
void content_policy_destroy(content_policy *);
size_t content_blocks_identity_count(const maage_block_registry *, content_domain);
const char *content_policy_domain_name(content_domain);
maage_status content_blocks_view(const maage_block_registry *, const maage_block_geometry_registry *, const maage_content_view_config *, maage_block_registry **);
maage_status content_geometry_view(const maage_block_geometry_registry *, const maage_content_view_config *, maage_block_geometry_registry **);
void content_blocks_policy(maage_block_registry *, content_policy *);
void content_items_policy(maage_item_registry *, content_policy *);
void content_fluids_policy(maage_fluid_registry *, content_policy *);
void content_geometry_policy(maage_block_geometry_registry *, content_policy *);
void content_containers_policy(maage_container_registry *, content_policy *);
void content_creative_policy(maage_creative_registry *, content_policy *);
void content_sounds_policy(maage_sound_registry *, content_policy *);
void content_presentation_policy(maage_presentation_registry *, content_policy *);
#endif
