#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

#define MAAGE_VERTEX_FIELD(slot, type, name) layout(location = slot) in type name;
MAAGE_RENDER_MESH_ATTRIBUTES(MAAGE_VERTEX_FIELD)
#undef MAAGE_VERTEX_FIELD
layout(location = 0) out vec2 maage_fragment_uv;
layout(location = 1) out vec4 maage_fragment_color;
layout(location = 2) out vec2 maage_fragment_light;
layout(location = 3) flat out float maage_fragment_material;
layout(location = 4) out float maage_fog_depth;

void main()
{
    gl_Position = maage.matrix * vec4(position, 1.0);
    maage_fog_depth = gl_Position.w;
    /* The shared right-handed matrix already has Vulkan/Metal 0..W depth.
     * Positive Vulkan viewport height requires only this native Y inversion. */
    gl_Position.y = -gl_Position.y;
    maage_fragment_uv = uv;
    maage_fragment_uv.y += maage_shader_animation(animation, maage.parameters.x);
    maage_fragment_color = color;
    maage_fragment_light = light;
    maage_fragment_material = material;
}
