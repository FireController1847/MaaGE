#version 450
#extension GL_GOOGLE_include_directive : require
#define MAAGE_SHADER_VEC2 vec2
#define MAAGE_SHADER_VEC3 vec3
#define MAAGE_SHADER_MOD mod
#define MAAGE_SHADER_SATURATE(value) clamp(value, 0.0, 1.0)
#include "../../render_shader_common.h"

layout(set = 0, binding = 0) uniform sampler2D maage_atlas;
layout(push_constant) uniform maage_uniforms {
    mat4 matrix;
    vec4 lighting;
    vec4 parameters;
} maage;
layout(location = 0) in vec2 maage_fragment_uv;
layout(location = 1) in vec4 maage_fragment_color;
layout(location = 2) in vec2 maage_fragment_light;
layout(location = 3) flat in float maage_fragment_material;
layout(location = 0) out vec4 maage_result;

void main()
{
    vec4 texel = texture(maage_atlas, maage_fragment_uv);
    if (maage_fragment_material == float(MAAGE_MESH_MATERIAL_CUTOUT) &&
        texel.a < MAAGE_RENDER_CUTOUT_ALPHA) discard;
    if (maage_fragment_material == float(MAAGE_MESH_MATERIAL_MULTIPLY)) {
        maage_result = vec4(mix(vec3(0.5), texel.rgb * maage_fragment_color.rgb, texel.a), 1.0);
        return;
    }
    vec2 level = maage.lighting.w > 0.0 ? maage.lighting.yz : maage_fragment_light;
    vec3 light = maage_shader_lighting(level, maage.lighting.x);
    maage_result = vec4(texel.rgb * maage_fragment_color.rgb * light,
        maage_shader_alpha(maage_fragment_material, texel.a, maage_fragment_color.a));
}
