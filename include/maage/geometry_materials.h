#ifndef MAAGE_GEOMETRY_MATERIALS_H
#define MAAGE_GEOMETRY_MATERIALS_H
/* Numeric surface behavior, constant across a triangle. Translucent surfaces
 * blend after solid/cutout geometry with depth testing and no depth writes.
 * Shared by portable geometry and both native shader languages. */
#define MAAGE_MESH_MATERIAL_OPAQUE 0
#define MAAGE_MESH_MATERIAL_CUTOUT 1
#define MAAGE_MESH_MATERIAL_TRANSLUCENT 2
#define MAAGE_MESH_MATERIAL_MULTIPLY 3
#define MAAGE_MESH_MATERIAL_STARFIELD 4
#define MAAGE_MESH_MATERIAL_COUNT 5
#define MAAGE_MESH_ROTATION_FACE_COUNT 6
/* Shared content and shader policy values; zero retains authored UVs. */
#define MAAGE_TEXTURE_VARIATION_FIXED 0
#define MAAGE_TEXTURE_VARIATION_ROTATE_90 1
#define MAAGE_TEXTURE_VARIATION_ROTATE_180 2
#define MAAGE_TEXTURE_VARIATION_MIRROR 3
#define MAAGE_TEXTURE_VARIATION_ROTATE_90_MIRROR 4
#define MAAGE_TEXTURE_VARIATION_ROTATE_180_MIRROR 5
#define MAAGE_TEXTURE_VARIATION_MAX MAAGE_TEXTURE_VARIATION_ROTATE_180_MIRROR
/* The second selector range marks planes inside the cell, not on its boundary. */
#define MAAGE_MESH_VARIATION_FACE_RANGE (MAAGE_MESH_ROTATION_FACE_COUNT * MAAGE_TEXTURE_VARIATION_MAX)
#define MAAGE_MESH_VARIATION_FACE_MAX (2 * MAAGE_MESH_VARIATION_FACE_RANGE)
#endif
