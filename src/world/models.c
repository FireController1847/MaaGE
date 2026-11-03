#include "registry_policy.h"
#include "maage/models.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct maage_model_registry {
    content_policy *policy;
    maage_model_definition *definitions;
    char **names;
    maage_model_id *buckets;
    size_t count, capacity, bucket_count;
    int finalized;
};
static maage_status model_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, code < 0x40 ? 0 : MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static unsigned long model_hash(const char *name) {
    unsigned long hash = 2166136261UL;
    while (*name)
        hash = ((hash ^ (unsigned char) *name++) * 16777619UL) & 0xffffffffUL;
    return hash;
}
static int model_name_valid(const char *name) {
    int colon = 0;
    if (!name || !*name || *name == ':') return 0;
    for (; *name; ++name) {
        if (*name == ':') {
            if (colon || !name[1]) return 0;
            colon = 1;
        } else if (!((*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9') || *name == '_' || *name == '-' || *name == '/' || *name == '.')) return 0;
    }
    return colon;
}
static int model_finite(double value) {
    return value >= -DBL_MAX && value <= DBL_MAX;
}
static size_t model_bucket(const maage_model_registry *registry, const char *name) {
    size_t bucket = model_hash(name) & (registry->bucket_count - 1);
    while (registry->buckets[bucket] && strcmp(registry->names[registry->buckets[bucket] - 1], name))
        bucket = (bucket + 1) & (registry->bucket_count - 1);
    return bucket;
}
static char *model_string(const char *text) {
    char *copy;
    if (!text) return NULL;
    copy = (char *) malloc(strlen(text) + 1);
    if (copy) strcpy(copy, text);
    return copy;
}
static void model_release(maage_model_definition *definition) {
    maage_model_part *parts = (maage_model_part *) definition->parts;
    size_t i;
    for (i = 0; i < definition->part_count; ++i) {
        free((void *) parts[i].name);
        free((void *) parts[i].channel);
        free((void *) parts[i].cuboids);
    }
    free(parts);
    memset(definition, 0, sizeof(*definition));
}
static maage_status model_copy(const maage_model_definition *source, maage_model_definition *output) {
    maage_model_part *parts;
    maage_model_cuboid *boxes;
    const maage_model_cuboid *box;
    size_t i, j, k, axis;
    if (!source || !source->part_count || !source->parts || !source->texture_width || !source->texture_height || !(source->scale > 0) || !model_finite(source->scale) || source->texture_id == MAAGE_CONTENT_ID_INVALID) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (source->part_count > (size_t) - 1 / sizeof(*parts)) return model_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    for (i = 0; i < source->part_count; ++i) {
        if (!source->parts[i].name || !*source->parts[i].name || (source->parts[i].parent != MAAGE_CONTENT_ID_INVALID && source->parts[i].parent >= i) || (source->parts[i].cuboid_count && !source->parts[i].cuboids)) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        for (k = 0; k < i; ++k)
            if (!strcmp(source->parts[k].name, source->parts[i].name)) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        if (source->parts[i].cuboid_count > (size_t) - 1 / sizeof(*boxes)) return model_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        for (axis = 0; axis < 3; ++axis)
            if (!model_finite(source->parts[i].pivot[axis]) || !model_finite(source->parts[i].rotation[axis])) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        for (j = 0; j < source->parts[i].cuboid_count; ++j) {
            box = &source->parts[i].cuboids[j];
            if (!(box->inflate >= 0) || !model_finite(box->inflate) || box->material > MAAGE_MESH_MATERIAL_TRANSLUCENT) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
            for (axis = 0; axis < 3; ++axis)
                if (!model_finite(box->minimum[axis]) || !(box->size[axis] > 0) || !model_finite(box->size[axis])) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
            if (box->texture_x + 2.0 * (box->size[0] + box->size[2]) > source->texture_width || box->texture_y + box->size[2] + box->size[1] > source->texture_height) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
        }
    }
    memset(output, 0, sizeof(*output));
    parts = (maage_model_part *) calloc(source->part_count, sizeof(*parts));
    if (!parts) return model_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    *output = *source;
    output->parts = parts;
    for (i = 0; i < source->part_count; ++i) {
        parts[i] = source->parts[i];
        parts[i].name = model_string(source->parts[i].name);
        parts[i].channel = model_string(source->parts[i].channel);
        parts[i].cuboids = NULL;
        if (!parts[i].name || (source->parts[i].channel && !parts[i].channel)) goto failed;
        if (parts[i].cuboid_count) {
            boxes = (maage_model_cuboid *) malloc(parts[i].cuboid_count * sizeof(*boxes));
            if (!boxes) goto failed;
            memcpy(boxes, source->parts[i].cuboids, parts[i].cuboid_count * sizeof(*boxes));
            parts[i].cuboids = boxes;
        }
    }
    return model_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
failed:
    model_release(output);
    return model_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
}
maage_status maage_model_registry_create(maage_model_registry **output) {
    if (!output) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = (maage_model_registry *) calloc(1, sizeof(**output));
    return model_status(*output ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
}
maage_status maage_model_registry_destroy(maage_model_registry *registry) {
    size_t i;
    if (!registry) return model_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < registry->count; ++i) {
        model_release(&registry->definitions[i]);
        free(registry->names[i]);
    }
    free(registry->definitions);
    free(registry->names);
    free(registry->buckets);
    free(registry);
    return model_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_model_registry_register(maage_model_registry *registry, const char *name, const maage_model_definition *definition, maage_model_id *id) {
    maage_model_definition copy, *definitions;
    maage_model_id *buckets;
    char **names, *name_copy;
    size_t capacity, count, i;
    maage_status result;
    if (id) *id = 0;
    if (!registry || registry->finalized || !id || !model_name_valid(name)) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    result = content_policy_register(registry->policy, CONTENT_MODEL, name, (unsigned long) registry->count + 1);
    if (maage_status_failed(result)) return result;
    if (registry->count >= MAAGE_CONTENT_ID_INVALID - 1UL) return model_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    if (registry->bucket_count && registry->buckets[model_bucket(registry, name)]) return model_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    result = model_copy(definition, &copy);
    if (maage_status_failed(result)) return result;
    name_copy = model_string(name);
    if (!name_copy) {
        model_release(&copy);
        return model_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    if (registry->count == registry->capacity) {
        capacity = registry->capacity ? registry->capacity * 2 : 16;
        if (capacity < registry->capacity || capacity > (size_t) - 1 / sizeof(*definitions) || capacity > (size_t) - 1 / sizeof(*names)) {
            result = model_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
            goto failed;
        }
        definitions = (maage_model_definition *) realloc(registry->definitions, capacity * sizeof(*definitions));
        if (!definitions) {
            result = model_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
            goto failed;
        }
        registry->definitions = definitions;
        names = (char **) realloc(registry->names, capacity * sizeof(*names));
        if (!names) {
            result = model_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
            goto failed;
        }
        registry->names = names;
        registry->capacity = capacity;
    }
    if (!registry->bucket_count || registry->count + 1 >= registry->bucket_count / 2) {
        count = registry->bucket_count ? registry->bucket_count * 2 : 32;
        if (count < registry->bucket_count || count > (size_t) - 1 / sizeof(*buckets)) {
            result = model_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
            goto failed;
        }
        buckets = (maage_model_id *) calloc(count, sizeof(*buckets));
        if (!buckets) {
            result = model_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
            goto failed;
        }
        free(registry->buckets);
        registry->buckets = buckets;
        registry->bucket_count = count;
        for (i = 0; i < registry->count; ++i)
            registry->buckets[model_bucket(registry, registry->names[i])] = (maage_model_id) i + 1;
    }
    registry->names[registry->count] = name_copy;
    registry->definitions[registry->count] = copy;
    *id = (maage_model_id) ++registry->count;
    registry->buckets[model_bucket(registry, name)] = *id;
    return model_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
failed:
    free(name_copy);
    model_release(&copy);
    return result;
}
maage_status maage_model_registry_update(maage_model_registry *registry, maage_model_id id, const maage_model_definition *definition) {
    maage_model_definition copy;
    maage_status result;
    if (!registry || registry->finalized || !id || id > registry->count) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_SET);
    result = content_policy_update(registry->policy, CONTENT_MODEL, registry->names[id - 1]);
    if (maage_status_failed(result)) return result;
    result = model_copy(definition, &copy);
    if (maage_status_failed(result)) return result;
    model_release(&registry->definitions[id - 1]);
    registry->definitions[id - 1] = copy;
    return model_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_SET);
}
maage_status maage_model_registry_finalize(maage_model_registry *registry) {
    maage_status result;
    if (!registry || registry->finalized) return model_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    result = content_policy_finalize(registry->policy);
    if (maage_status_failed(result)) return result;
    registry->finalized = 1;
    return model_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
}
int maage_model_registry_is_finalized(const maage_model_registry *registry) {
    return registry && registry->finalized;
}
size_t maage_model_registry_count(const maage_model_registry *registry) {
    return registry ? registry->count : 0;
}
const maage_model_definition *maage_model_registry_get(const maage_model_registry *registry, maage_model_id id) {
    return registry && id && id <= registry->count ? &registry->definitions[id - 1] : NULL;
}
const char *maage_model_registry_name(const maage_model_registry *registry, maage_model_id id) {
    return registry && id && id <= registry->count ? registry->names[id - 1] : NULL;
}
maage_status maage_model_registry_resolve(const maage_model_registry *registry, const char *name, maage_model_id *id) {
    maage_status result;
    if (id) *id = 0;
    if (!registry || !name || !id) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    result = content_policy_read(registry->policy, CONTENT_MODEL, name);
    if (maage_status_failed(result)) return result;
    if (registry->bucket_count) *id = registry->buckets[model_bucket(registry, name)];
    return model_status(*id ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_model_part_resolve(const maage_model_definition *model, const char *name, size_t *part) {
    size_t i;
    if (!model || !name || !part) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    for (i = 0; i < model->part_count; ++i)
        if (!strcmp(model->parts[i].name, name)) {
            *part = i;
            return model_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_RESOLVE);
        }
    return model_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_model_emit_part(const maage_model_definition *model, size_t part, const maage_model_sink *sink) {
    static const unsigned int corners[6][4][3] = {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
    const maage_model_cuboid *box;
    maage_model_quad quad;
    maage_status result;
    size_t i;
    unsigned int face, corner, axis;
    double u0, u1, v0, v1, x, w, d;
    if (!model || part >= model->part_count || !sink || !sink->quad) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_MESH);
    if (model->geometry) return model->geometry(model->context, model, part, sink);
    for (i = 0; i < model->parts[part].cuboid_count; ++i) {
        box = &model->parts[part].cuboids[i];
        x = box->texture_x;
        w = box->size[0];
        d = box->size[2];
        for (face = 0; face < 6; ++face) {
            v0 = box->texture_y + d;
            v1 = v0 + box->size[1];
            if (face == 0) {
                u0 = x + d;
                u1 = x;
            } else if (face == 1) {
                u0 = x + d + w + d;
                u1 = x + d + w;
            } else if (face == 4) {
                u0 = x + d + w;
                u1 = x + d;
            } else if (face == 5) {
                u0 = x + d + w + d + w;
                u1 = x + d + w + d;
            } else if (face == 3) {
                u0 = x + d;
                u1 = u0 + w;
                v0 = box->texture_y + d;
                v1 = box->texture_y;
            } else {
                u0 = x + d + w;
                u1 = u0 + w;
                v0 = box->texture_y;
                v1 = box->texture_y + d;
            }
            memset(&quad, 0, sizeof(quad));
            quad.face = face;
            quad.material = box->material;
            for (corner = 0; corner < 4; ++corner) {
                for (axis = 0; axis < 3; ++axis)
                    quad.positions[corner][axis] = (float) (box->minimum[axis] - box->inflate + corners[face][corner][axis] * (box->size[axis] + 2 * box->inflate));
                quad.uv[corner][0] = (float) ((corner == 1 || corner == 2 ? u1 : u0) / model->texture_width);
                quad.uv[corner][1] = (float) ((corner < 2 ? v1 : v0) / model->texture_height);
            }
            result = sink->quad(sink->context, &quad);
            if (maage_status_failed(result)) return result;
        }
    }
    return model_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_MESH);
}
void maage_model_transform_init(maage_model_transform *transform) {
    unsigned int i;
    memset(transform, 0, sizeof(*transform));
    for (i = 0; i < 3; ++i)
        transform->scale[i] = 1;
    for (i = 0; i < 4; ++i)
        transform->tint[i] = 1;
    transform->visible = 1;
}
static void model_identity(float matrix[16]) {
    unsigned int i;
    memset(matrix, 0, 16 * sizeof(*matrix));
    for (i = 0; i < 4; ++i)
        matrix[i * 5] = 1;
}
static void model_multiply(float output[16], const float left[16], const float right[16]) {
    float values[16];
    unsigned int row, column, k;
    for (column = 0; column < 4; ++column)
        for (row = 0; row < 4; ++row) {
            values[column * 4 + row] = 0;
            for (k = 0; k < 4; ++k)
                values[column * 4 + row] += left[k * 4 + row] * right[column * 4 + k];
        }
    memcpy(output, values, sizeof(values));
}
maage_status maage_model_part_matrix(const maage_model_definition *model, const maage_model_animation *animation, size_t part, float matrix[16], int *visible) {
    maage_model_transform transform;
    const maage_model_part *definition;
    float local[16], rotation[16];
    double angle, sine, cosine;
    unsigned int axis, i, first, second;
    size_t parent;
    maage_status result;
    if (!model || part >= model->part_count || !matrix || !visible) return model_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_PRESENT);
    model_identity(matrix);
    *visible = 1;
    parent = part;
    for (;;) {
        definition = &model->parts[parent];
        maage_model_transform_init(&transform);
        if (animation && model->animate) {
            result = model->animate(model->context, animation, parent, &transform);
            if (maage_status_failed(result)) return result;
        }
        if (!transform.visible) *visible = 0;
        model_identity(local);
        for (axis = 0; axis < 3; ++axis) {
            if (!model_finite(transform.translation[axis]) || !model_finite(transform.rotation[axis]) || !model_finite(transform.scale[axis])) return model_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_PRESENT);
            local[12 + axis] = (float) (definition->pivot[axis] + transform.translation[axis]);
        }
        for (axis = 0; axis < 3; ++axis) {
            angle = definition->rotation[axis] + transform.rotation[axis];
            sine = sin(angle);
            cosine = cos(angle);
            first = (axis + 1) % 3;
            second = (axis + 2) % 3;
            model_identity(rotation);
            rotation[first * 4 + first] = rotation[second * 4 + second] = (float) cosine;
            rotation[first * 4 + second] = (float) sine;
            rotation[second * 4 + first] = (float) -sine;
            model_multiply(local, local, rotation);
        }
        for (axis = 0; axis < 3; ++axis)
            for (i = 0; i < 4; ++i)
                local[axis * 4 + i] *= (float) transform.scale[axis];
        model_multiply(matrix, local, matrix);
        if (definition->parent == MAAGE_CONTENT_ID_INVALID) break;
        parent = (size_t) definition->parent;
    }
    return model_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_PRESENT);
}
void content_models_policy(maage_model_registry *registry, content_policy *policy) {
    registry->policy = policy;
}
