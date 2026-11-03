#ifndef MAAGE_MODELS_H
#define MAAGE_MODELS_H
#include "maage/blocks.h"
#include "maage/geometry_materials.h"

typedef unsigned long maage_model_id;
typedef struct maage_model_registry maage_model_registry;
/* Model pixels use a right-handed Y-up basis. Cuboids are relative to their
 * part's pivot. The conventional box unwrap occupies 2*(width+depth) pixels
 * horizontally and depth+height vertically, beginning at texture_x/y. */
typedef struct maage_model_cuboid {
    double minimum[3], size[3], inflate;
    unsigned int texture_x, texture_y;
    unsigned long material;
} maage_model_cuboid;
typedef struct maage_model_part {
    const char *name, *channel;  /* Registration names only; never queried per frame. */
    unsigned long parent;        /* Earlier part index, or INVALID for a root. */
    maage_texture_id texture_id; /* INVALID inherits the selected model texture. */
    double pivot[3], rotation[3];
    const maage_model_cuboid *cuboids;
    size_t cuboid_count;
} maage_model_part;
typedef struct maage_model_animation {
    double age, walk_phase, walk_amount, yaw, pitch, attack, charge;
    unsigned long variant, flags;
} maage_model_animation;
typedef struct maage_model_transform {
    double translation[3], rotation[3], scale[3];
    float tint[4]; /* Immutable variant color, sampled with other animation inputs zero when cached. */
    int visible;
} maage_model_transform;
struct maage_model_definition;
typedef struct maage_model_quad {
    float positions[4][3], uv[4][2]; /* Local pixels and normalized sheet coordinates. */
    unsigned int face;
    unsigned long material;
} maage_model_quad;
typedef struct maage_model_sink {
    maage_status (*quad)(void *, const maage_model_quad *);
    void *context;
} maage_model_sink;
typedef maage_status (*maage_model_geometry)(const void *, const struct maage_model_definition *, size_t, const maage_model_sink *);
typedef maage_status (*maage_model_animator)(const void *, const maage_model_animation *, size_t, maage_model_transform *);
typedef struct maage_model_definition {
    const maage_model_part *parts;
    size_t part_count;
    maage_texture_id texture_id;
    unsigned int texture_width, texture_height;
    double scale;                  /* Pixels to world units; ordinarily 1/16. */
    maage_model_geometry geometry; /* Optional provider replaces cuboids for each part. */
    maage_model_animator animate;
    const void *context; /* Borrowed immutable module data. */
} maage_model_definition;
/* Names, parts and cuboids are copied. Callback/context lifetime includes every
 * borrowing resource generation. IDs start at one; zero means no model. */
maage_status maage_model_registry_create(maage_model_registry **);
maage_status maage_model_registry_destroy(maage_model_registry *);
maage_status maage_model_registry_register(maage_model_registry *, const char *, const maage_model_definition *, maage_model_id *);
maage_status maage_model_registry_update(maage_model_registry *, maage_model_id, const maage_model_definition *);
maage_status maage_model_registry_finalize(maage_model_registry *);
int maage_model_registry_is_finalized(const maage_model_registry *);
size_t maage_model_registry_count(const maage_model_registry *);
const maage_model_definition *maage_model_registry_get(const maage_model_registry *, maage_model_id);
const char *maage_model_registry_name(const maage_model_registry *, maage_model_id);
maage_status maage_model_registry_resolve(const maage_model_registry *, const char *, maage_model_id *);
maage_status maage_model_part_resolve(const maage_model_definition *, const char *, size_t *);
/* Count/write providers are deterministic and allocation-free. The sink must
 * retain nothing; failures propagate without continuing emission. */
maage_status maage_model_emit_part(const maage_model_definition *, size_t, const maage_model_sink *);
void maage_model_transform_init(maage_model_transform *);
/* Matrix includes this part and all ancestors, in model pixels. Scratch-free
 * composition requires parents to precede children and never visits names. */
maage_status maage_model_part_matrix(const maage_model_definition *, const maage_model_animation *, size_t, float[16], int *);
#endif
