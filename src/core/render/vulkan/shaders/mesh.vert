#version 450
#extension GL_GOOGLE_include_directive : require
#define MAAGE_SHADER_VEC2 vec2
#define MAAGE_SHADER_VEC3 vec3
#define MAAGE_SHADER_MOD mod
#define MAAGE_SHADER_SATURATE(value) clamp(value, 0.0, 1.0)
#include "../../render_shader_common.h"

layout(location = MAAGE_RENDER_MESH_POSITION_LOCATION) in vec3 maage_position;
layout(location = MAAGE_RENDER_MESH_NORMAL_LOCATION) in vec3 maage_normal;
layout(location = MAAGE_RENDER_MESH_UV_LOCATION) in vec2 maage_uv;
layout(location = MAAGE_RENDER_MESH_COLOR_LOCATION) in vec4 maage_color;
layout(location = MAAGE_RENDER_MESH_LIGHT_LOCATION) in vec2 maage_light;
layout(location = MAAGE_RENDER_MESH_MATERIAL_LOCATION) in float maage_material;
layout(location = MAAGE_RENDER_MESH_ANIMATION_LOCATION) in vec3 maage_animation;
layout(push_constant) uniform maage_uniforms {
    mat4 matrix;
    vec4 lighting;
    vec4 parameters;
} maage;
layout(location = 0) out vec2 maage_fragment_uv;
layout(location = 1) out vec4 maage_fragment_color;
layout(location = 2) out vec2 maage_fragment_light;
layout(location = 3) flat out float maage_fragment_material;

void main()
{
    gl_Position = maage.matrix * vec4(maage_position, 1.0);
    /* The shared right-handed matrix already has Vulkan/Metal 0..W depth.
     * Positive Vulkan viewport height requires only this native Y inversion. */
    gl_Position.y = -gl_Position.y;
    maage_fragment_uv = maage_uv;
    maage_fragment_uv.y += maage_shader_animation(maage_animation, maage.parameters.x);
    maage_fragment_color = maage_color;
    maage_fragment_light = maage_light;
    maage_fragment_material = maage_material;
}
