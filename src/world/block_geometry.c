#include "registry_policy.h"
#include "maage/block_geometry.h"
#include <stdlib.h>
#include <string.h>

struct maage_block_geometry_registry {
    content_policy *policy;
    maage_block_geometry_definition *definitions;
    char **names;
    maage_block_geometry_id *buckets;
    size_t count, capacity, bucket_count;
    int finalized;
};
static maage_status geometry_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, code < 0x40 ? 0 : MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static unsigned long geometry_hash(const char *name) {
    unsigned long hash = 2166136261UL;
    while (*name)
        hash = ((hash ^ (unsigned char) *name++) * 16777619UL) & 0xffffffffUL;
    return hash;
}
static int geometry_name_valid(const char *name) {
    int colon = 0;
    if (!name || !*name || *name == ':') return 0;
    while (*name) {
        if (*name == ':') {
            if (colon || !name[1]) return 0;
            colon = 1;
        } else if (!((*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9') || *name == '_' || *name == '-' || *name == '/' || *name == '.')) return 0;
        ++name;
    }
    return colon;
}
static size_t geometry_bucket(const maage_block_geometry_registry *registry, const char *name) {
    size_t bucket = geometry_hash(name) & (registry->bucket_count - 1);
    while (registry->buckets[bucket] && strcmp(registry->names[registry->buckets[bucket] - 1], name))
        bucket = (bucket + 1) & (registry->bucket_count - 1);
    return bucket;
}
maage_status maage_block_geometry_create(maage_block_geometry_registry **output) {
    if (!output) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = (maage_block_geometry_registry *) calloc(1, sizeof(**output));
    return geometry_status(*output ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
}
maage_status maage_block_geometry_destroy(maage_block_geometry_registry *registry) {
    size_t i;
    if (!registry) return geometry_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < registry->count; ++i) {
        free(registry->names[i]);
        free((void *) registry->definitions[i].appearances);
    }
    free(registry->names);
    free(registry->definitions);
    free(registry->buckets);
    free(registry);
    return geometry_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_block_geometry_register(maage_block_geometry_registry *registry, const char *name, const maage_block_geometry_definition *definition, maage_block_geometry_id *id) {
    maage_status policy_result;
    maage_block_geometry_definition *definitions, definition_copy;
    maage_block_face_definition *appearances = NULL;
    maage_block_geometry_id *buckets;
    char **names, *copy;
    size_t capacity, count, index, bucket;
    if (registry) {
        policy_result = content_policy_register(registry->policy, CONTENT_GEOMETRY, name, (unsigned long) (registry->count + 1));
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (id) *id = 0;
    if (!registry || registry->finalized || !id || !definition || !geometry_name_valid(name) || definition->primitive > MAAGE_BLOCK_GEOMETRY_CUSTOM || (definition->primitive == MAAGE_BLOCK_GEOMETRY_CUSTOM ? !definition->emit : definition->emit != NULL) || (definition->appearance_count && !definition->appearances)) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (definition->appearance_count > (size_t) - 1 / sizeof(*appearances)) return geometry_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    for (index = 0; index < definition->appearance_count; ++index)
        if (!maage_block_face_definition_valid(&definition->appearances[index])) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    definition_copy = *definition;
    if (registry->bucket_count && registry->buckets[geometry_bucket(registry, name)]) return geometry_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (registry->count >= MAAGE_CONTENT_ID_INVALID - 1UL) return geometry_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    copy = (char *) malloc(strlen(name) + 1);
    if (!copy) return geometry_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    strcpy(copy, name);
    if (registry->count == registry->capacity) {
        capacity = registry->capacity ? registry->capacity * 2 : 16;
        if (capacity < registry->capacity || capacity > (size_t) - 1 / sizeof(*definitions) || capacity > (size_t) - 1 / sizeof(*names)) {
            free(copy);
            return geometry_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        }
        definitions = (maage_block_geometry_definition *) realloc(registry->definitions, capacity * sizeof(*definitions));
        if (!definitions) {
            free(copy);
            return geometry_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        }
        registry->definitions = definitions;
        names = (char **) realloc(registry->names, capacity * sizeof(*names));
        if (!names) {
            free(copy);
            return geometry_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        }
        registry->names = names;
        registry->capacity = capacity;
    }
    if (!registry->bucket_count || registry->count + 1 >= registry->bucket_count / 2) {
        count = registry->bucket_count ? registry->bucket_count * 2 : 32;
        if (count < registry->bucket_count || count > (size_t) - 1 / sizeof(*buckets)) {
            free(copy);
            return geometry_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        }
        buckets = (maage_block_geometry_id *) calloc(count, sizeof(*buckets));
        if (!buckets) {
            free(copy);
            return geometry_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        }
        free(registry->buckets);
        registry->buckets = buckets;
        registry->bucket_count = count;
        for (index = 0; index < registry->count; ++index)
            registry->buckets[geometry_bucket(registry, registry->names[index])] = (maage_block_geometry_id) index + 1;
    }
    if (definition_copy.appearance_count) {
        appearances = (maage_block_face_definition *) malloc(definition_copy.appearance_count * sizeof(*appearances));
        if (!appearances) {
            free(copy);
            return geometry_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        }
        memcpy(appearances, definition_copy.appearances, definition_copy.appearance_count * sizeof(*appearances));
    }
    bucket = geometry_bucket(registry, name);
    registry->definitions[registry->count] = definition_copy;
    registry->definitions[registry->count].appearances = appearances;
    registry->names[registry->count] = copy;
    *id = (maage_block_geometry_id) ++registry->count;
    registry->buckets[bucket] = *id;
    return geometry_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
maage_status maage_block_geometry_update_appearance(maage_block_geometry_registry *registry, maage_block_geometry_id id, size_t index, const maage_block_face_definition *appearance) {
    maage_status policy_result;
    maage_block_geometry_definition *definition;
    if (registry) {
        policy_result = content_policy_update(registry->policy, CONTENT_GEOMETRY, maage_block_geometry_name(registry, id));
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || !appearance || !id || id > registry->count || !maage_block_face_definition_valid(appearance)) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    if (registry->finalized) return geometry_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_UPDATE);
    definition = &registry->definitions[id - 1];
    if (index >= definition->appearance_count) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    ((maage_block_face_definition *) definition->appearances)[index] = *appearance;
    return geometry_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
}
maage_status maage_block_geometry_finalize(maage_block_geometry_registry *registry) {
    maage_status policy_result;
    size_t geometry, appearance;
    if (registry) {
        policy_result = content_policy_finalize(registry->policy);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || registry->finalized) return geometry_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    for (geometry = 0; geometry < registry->count; ++geometry)
        for (appearance = 0; appearance < registry->definitions[geometry].appearance_count; ++appearance)
            if (!maage_block_face_definition_valid(&registry->definitions[geometry].appearances[appearance])) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
    registry->finalized = 1;
    return geometry_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
int maage_block_geometry_is_finalized(const maage_block_geometry_registry *registry) {
    return registry && registry->finalized;
}
size_t maage_block_geometry_count(const maage_block_geometry_registry *registry) {
    return registry ? registry->count : 0;
}
const maage_block_geometry_definition *maage_block_geometry_get(const maage_block_geometry_registry *registry, maage_block_geometry_id id) {
    return registry && id && id <= registry->count ? &registry->definitions[id - 1] : NULL;
}
const char *maage_block_geometry_name(const maage_block_geometry_registry *registry, maage_block_geometry_id id) {
    return registry && id && id <= registry->count ? registry->names[id - 1] : NULL;
}
maage_status maage_block_geometry_resolve(const maage_block_geometry_registry *registry, const char *name, maage_block_geometry_id *id) {
    maage_status policy_result;
    if (id) *id = 0;
    if (registry) {
        policy_result = content_policy_read(registry->policy, CONTENT_GEOMETRY, name);
        if (maage_status_failed(policy_result)) return policy_result;
    }
    if (!registry || !name || !id) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    if (registry->bucket_count) *id = registry->buckets[geometry_bucket(registry, name)];
    return geometry_status(*id ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_block_geometry_quad_validate(const maage_block_geometry_quad *quad) {
    unsigned int corner, axis;
    if (!quad || quad->face >= MAAGE_FACE_COUNT || (quad->cull != 0 && quad->cull != 1) || (quad->double_sided != 0 && quad->double_sided != 1) || (quad->material != MAAGE_BLOCK_GEOMETRY_INHERIT_MATERIAL && quad->material > MAAGE_MESH_MATERIAL_MULTIPLY)) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_MESH);
    for (corner = 0; corner < 4; ++corner) {
        for (axis = 0; axis < 3; ++axis)
            if (!(quad->positions[corner][axis] >= 0 && quad->positions[corner][axis] <= 1)) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_MESH);
        for (axis = 0; axis < 2; ++axis)
            if (!(quad->uv[corner][axis] >= 0 && quad->uv[corner][axis] <= 1)) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_MESH);
    }
    return geometry_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MESH);
}
maage_status maage_block_geometry_cuboid(const maage_block_geometry_sink *sink, const maage_block_shape *bounds, const maage_block_face_definition faces[MAAGE_FACE_COUNT], unsigned int face_mask, unsigned long material, int cull) {
    static const unsigned char corners[6][4][3] = {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
    maage_block_geometry_quad quad;
    maage_status result;
    unsigned int face, corner, axis;
    if (!sink || !sink->quad || !bounds || (face_mask & ~63U) || (cull != 0 && cull != 1) || (material != MAAGE_BLOCK_GEOMETRY_INHERIT_MATERIAL && material > MAAGE_MESH_MATERIAL_MULTIPLY)) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_MESH);
    for (axis = 0; axis < 3; ++axis)
        if (!(bounds->minimum[axis] >= 0 && bounds->maximum[axis] <= 1 && bounds->minimum[axis] < bounds->maximum[axis])) return geometry_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_MESH);
    memset(&quad, 0, sizeof(quad));
    quad.material = material;
    for (face = 0; face < 6; ++face) {
        if (!(face_mask & (1U << face))) continue;
        quad.face = face;
        quad.cull = cull && (face & 1U ? bounds->maximum[face / 2] == 1 : bounds->minimum[face / 2] == 0);
        quad.appearance = faces ? &faces[face] : NULL;
        for (corner = 0; corner < 4; ++corner) {
            for (axis = 0; axis < 3; ++axis)
                quad.positions[corner][axis] = (float) (corners[face][corner][axis] ? bounds->maximum[axis] : bounds->minimum[axis]);
            quad.uv[corner][0] = corner == 1 || corner == 2 ? 1.0f : 0.0f;
            quad.uv[corner][1] = corner < 2 ? 1.0f : 0.0f;
        }
        result = sink->quad(sink->context, &quad);
        if (maage_status_failed(result)) return result;
    }
    return geometry_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MESH);
}

void content_geometry_policy(maage_block_geometry_registry *registry, content_policy *policy) {
    registry->policy = policy;
}
