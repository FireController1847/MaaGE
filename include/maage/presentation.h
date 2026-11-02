#ifndef MAAGE_PRESENTATION_H
#define MAAGE_PRESENTATION_H
#include "maage/blocks.h"
typedef struct maage_presentation_registry maage_presentation_registry;
typedef struct maage_texture_definition {
    unsigned long source, fallback; /* Numeric source IDs; INVALID generates the missing tile. */
    int opaque;
    unsigned int frame_ticks; /* Zero static; otherwise vertically stacked square frames. */
    int pingpong;
    unsigned int frame_edge; /* Zero uses the atlas block edge; otherwise exact target frame pixels. */
} maage_texture_definition;
/* Application presentation roles are explicit bindings, never special paths. */
typedef struct maage_presentation_bindings {
    unsigned long skin, grass, foliage, breaking[10];
    unsigned long environment[5]; /* Sun, moon atlas, rain, snow, particles. */
    maage_texture_id missing;
} maage_presentation_bindings;
maage_status maage_presentation_bind(maage_presentation_registry *, const maage_presentation_bindings *);
const maage_presentation_bindings *maage_presentation_get_bindings(const maage_presentation_registry *);
int maage_presentation_source_animated(const maage_presentation_registry *, unsigned long);
maage_status maage_presentation_create(maage_presentation_registry **);
maage_status maage_presentation_destroy(maage_presentation_registry *);
maage_status maage_presentation_source_register(maage_presentation_registry *, const char *, unsigned long *);
maage_status maage_presentation_texture_register(maage_presentation_registry *, const maage_texture_definition *, maage_texture_id *);
maage_status maage_presentation_finalize(maage_presentation_registry *);
size_t maage_presentation_source_count(const maage_presentation_registry *);
size_t maage_presentation_texture_count(const maage_presentation_registry *);
const char *maage_presentation_source(const maage_presentation_registry *, unsigned long);
const maage_texture_definition *maage_presentation_texture(const maage_presentation_registry *, maage_texture_id);
#endif
