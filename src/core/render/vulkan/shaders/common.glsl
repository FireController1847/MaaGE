#ifndef MAAGE_VULKAN_SHADER_COMMON
#define MAAGE_VULKAN_SHADER_COMMON
#define MAAGE_SHADER_VEC2 vec2
#define MAAGE_SHADER_VEC3 vec3
#define MAAGE_SHADER_VEC4 vec4
#define MAAGE_SHADER_MOD mod
#define MAAGE_SHADER_SATURATE(value) clamp(value, 0.0, 1.0)
#define MAAGE_VERTEX_VEC1 float
#define MAAGE_VERTEX_VEC2 vec2
#define MAAGE_VERTEX_VEC3 vec3
#define MAAGE_VERTEX_VEC4 vec4
#include "../../render_shader_common.h"

layout(push_constant) uniform maage_uniforms {
    MAAGE_RENDER_UNIFORM_FIELDS(mat4, vec4)
} maage;
#endif
