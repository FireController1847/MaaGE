#include "registry_policy.h"
#include "maage/presentation.h"
#include <stdlib.h>
#include <string.h>
struct maage_presentation_registry {
    content_policy *policy;
    maage_presentation_bindings bindings;
    char **sources;
    maage_texture_definition *textures;
    size_t source_count, texture_count;
    int finalized;
};
static maage_status presentation_status(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_presentation_create(maage_presentation_registry **output) {
    if (!output) return presentation_status(MAAGE_STATUS_INVALID_ARGUMENT);
    *output = (maage_presentation_registry *) calloc(1, sizeof(**output));
    return presentation_status(*output ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ALLOCATION_FAILED);
}
maage_status maage_presentation_destroy(maage_presentation_registry *registry) {
    size_t i;
    if (!registry) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    for (i = 0; i < registry->source_count; ++i)
        free(registry->sources[i]);
    free(registry->sources);
    free(registry->textures);
    free(registry);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_presentation_source_register(maage_presentation_registry *registry, const char *path, unsigned long *id) {
    char **grown, *copy;
    size_t count, i;
    if (!registry || registry->finalized || !path || !*path || !id || *path == '/' || strchr(path, '\\') || strstr(path, "..") || strchr(path, ':')) return presentation_status(MAAGE_STATUS_INVALID_ARGUMENT);
    for (i = 0; i < registry->source_count; ++i)
        if (!strcmp(registry->sources[i], path)) {
            *id = (unsigned long) i;
            return presentation_status(MAAGE_STATUS_NOOP);
        }
    count = registry->source_count + 1;
    if (!count || count > 4294967295UL || count > (size_t) - 1 / sizeof(*grown)) return presentation_status(MAAGE_STATUS_BUFFER_OVERFLOW);
    copy = (char *) malloc(strlen(path) + 1);
    if (!copy) return presentation_status(MAAGE_STATUS_ALLOCATION_FAILED);
    grown = (char **) realloc(registry->sources, count * sizeof(*grown));
    if (!grown) {
        free(copy);
        return presentation_status(MAAGE_STATUS_ALLOCATION_FAILED);
    }
    strcpy(copy, path);
    registry->sources = grown;
    *id = (unsigned long) registry->source_count;
    registry->sources[registry->source_count++] = copy;
    return presentation_status(MAAGE_STATUS_SUCCESS);
}
maage_status maage_presentation_texture_register(maage_presentation_registry *registry, const maage_texture_definition *definition, maage_texture_id *id) {
    maage_texture_definition *grown;
    size_t count;
    if (!registry || registry->finalized || !definition || !id || (definition->opaque != 0 && definition->opaque != 1) || (definition->pingpong != 0 && definition->pingpong != 1) || (definition->source != MAAGE_CONTENT_ID_INVALID && definition->source >= registry->source_count) || (definition->fallback != MAAGE_CONTENT_ID_INVALID && definition->fallback >= registry->source_count)) return presentation_status(MAAGE_STATUS_INVALID_ARGUMENT);
    if ((definition->sheet_width == 0) != (definition->sheet_height == 0) || (definition->sheet_width && (definition->frame_ticks || definition->frame_edge))) return presentation_status(MAAGE_STATUS_INVALID_ARGUMENT);
    count = registry->texture_count + 1;
    if (!count || count > 4294967295UL || count > (size_t) - 1 / sizeof(*grown)) return presentation_status(MAAGE_STATUS_BUFFER_OVERFLOW);
    grown = (maage_texture_definition *) realloc(registry->textures, count * sizeof(*grown));
    if (!grown) return presentation_status(MAAGE_STATUS_ALLOCATION_FAILED);
    registry->textures = grown;
    *id = (maage_texture_id) registry->texture_count;
    registry->textures[registry->texture_count++] = *definition;
    return presentation_status(MAAGE_STATUS_SUCCESS);
}
maage_status maage_presentation_finalize(maage_presentation_registry *registry) {
    maage_status policy_result;
    if (registry) {
        policy_result = content_policy_finalize(registry->policy);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized) return presentation_status(MAAGE_STATUS_INVALID_STATE);
    registry->finalized = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_FINALIZE, 0);
}
size_t maage_presentation_source_count(const maage_presentation_registry *registry) {
    return registry ? registry->source_count : 0;
}
size_t maage_presentation_texture_count(const maage_presentation_registry *registry) {
    return registry ? registry->texture_count : 0;
}
const char *maage_presentation_source(const maage_presentation_registry *registry, unsigned long id) {
    return registry && id < registry->source_count ? registry->sources[id] : NULL;
}
const maage_texture_definition *maage_presentation_texture(const maage_presentation_registry *registry, maage_texture_id id) {
    return registry && id < registry->texture_count ? &registry->textures[id] : NULL;
}

maage_status maage_presentation_bind(maage_presentation_registry *registry, const maage_presentation_bindings *bindings) {
    maage_status policy_result;
    if (registry) {
        policy_result = content_policy_register(registry->policy, CONTENT_PRESENTATION_BINDINGS, "bindings", 0);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized || !bindings) return presentation_status(MAAGE_STATUS_INVALID_STATE);
    registry->bindings = *bindings;
    return presentation_status(MAAGE_STATUS_SUCCESS);
}
const maage_presentation_bindings *maage_presentation_get_bindings(const maage_presentation_registry *registry) {
    return registry ? &registry->bindings : NULL;
}
int maage_presentation_source_animated(const maage_presentation_registry *registry, unsigned long source) {
    size_t i;
    if (!registry) return 0;
    for (i = 0; i < registry->texture_count; ++i)
        if (registry->textures[i].source == source && registry->textures[i].frame_ticks) return 1;
    return 0;
}

void content_presentation_policy(maage_presentation_registry *registry, content_policy *policy) {
    registry->policy = policy;
}
