#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

#define MAAGE_VERTEX_BYTE4 vec4
#define MAAGE_VERTEX_BYTE2 vec2
#define MAAGE_VERTEX_UINT1 uint
#define MAAGE_VERTEX_FIELD(slot, type, name) layout(location = slot) in type name;
MAAGE_RENDER_GPU_MESH_ATTRIBUTES(MAAGE_VERTEX_FIELD)
#undef MAAGE_VERTEX_FIELD
layout(location = 0) out vec2 maage_fragment_uv;
layout(location = 1) out vec4 maage_fragment_color;
layout(location = 2) out vec2 maage_fragment_light;
layout(location = 3) flat out float maage_fragment_material;
layout(location = 4) out vec3 maage_fog_position;

layout(set = 1, binding = 0, std430) readonly buffer maage_surface_table {
    vec4 entries[];
};
layout(location = 5) flat out vec4 maage_tile;

void main()
{
    gl_Position = maage.matrix * vec4(position, 1.0);
    maage_fog_position = gl_Position.xyw;
    /* The shared right-handed matrix already has Vulkan/Metal 0..W depth.
     * Positive Vulkan viewport height requires only this native Y inversion. */
    gl_Position.y = -gl_Position.y;
    vec4 parameters = entries[surface * 2U];
    maage_tile = entries[surface * 2U + 1U];
    maage_fragment_uv = uv;
    float animation_offset = maage_shader_animation(parameters.xyz, maage.parameters.x);
    if (maage_tile.z > maage_tile.x) maage_tile.yw += animation_offset;
    else maage_fragment_uv.y += animation_offset;
    maage_fragment_color = color;
    maage_fragment_light = light;
    maage_fragment_material = parameters.w;
}
