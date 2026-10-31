#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D maage_atlas;
layout(location = 0) in vec2 maage_fragment_uv;
layout(location = 1) in vec4 maage_fragment_color;
layout(location = 2) in vec2 maage_fragment_light;
layout(location = 3) flat in float maage_fragment_material;
layout(location = 4) in float maage_fog_depth;
layout(location = 0) out vec4 maage_result;

void main()
{
    vec4 texel = texture(maage_atlas, maage_fragment_uv);
    if (maage_shader_cutout(maage_fragment_material, texel.a)) discard;
    maage_result = maage_shader_fog(maage_shader_mesh_color(texel, maage_fragment_color, maage_fragment_light, maage_fragment_material, maage.lighting), maage_fog_depth, maage_fragment_material, maage.fog);
}
