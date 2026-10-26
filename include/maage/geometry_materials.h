#ifndef MAAGE_GEOMETRY_MATERIALS_H
#define MAAGE_GEOMETRY_MATERIALS_H
/* Numeric surface behavior, constant across a triangle. Translucent surfaces
 * blend after solid/cutout geometry with depth testing and no depth writes.
 * Shared by portable geometry and both native shader languages. */
#define MAAGE_MESH_MATERIAL_OPAQUE 0
#define MAAGE_MESH_MATERIAL_CUTOUT 1
#define MAAGE_MESH_MATERIAL_TRANSLUCENT 2
#define MAAGE_MESH_MATERIAL_MULTIPLY 3
#endif
