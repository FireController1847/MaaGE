#ifndef MAAGE_RENDER_SHADER_COMMON_H
#define MAAGE_RENDER_SHADER_COMMON_H
#include "render_shader_defs.h"

/* Native MSL and GLSL supply their vector and intrinsic spellings. The equations
 * remain authoritative here; the C renderer consumes only the numeric header. */
float maage_shader_animation(MAAGE_SHADER_VEC3 animation, float tick)
{
    if (abs(animation.y) <= 1.0 || animation.z <= 0.0) return 0.0;
    float count = abs(animation.y);
    float period = animation.y < 0.0 ? count * 2.0 - 2.0 : count;
    float frame = MAAGE_SHADER_MOD(floor(tick / animation.z), period);
    if (frame >= count) frame = period - frame;
    return frame * animation.x;
}

MAAGE_SHADER_VEC3 maage_shader_lighting(MAAGE_SHADER_VEC2 level, float daylight)
{
    MAAGE_SHADER_VEC2 normalized = MAAGE_SHADER_SATURATE(level);
    MAAGE_SHADER_VEC2 brightness = normalized /
        (MAAGE_RENDER_LIGHT_CURVE_BASE - MAAGE_RENDER_LIGHT_CURVE_SCALE * normalized);
    float sky = brightness.x * (daylight * MAAGE_RENDER_SKY_SCALE + MAAGE_RENDER_SKY_MINIMUM);
    float torch = brightness.y * MAAGE_RENDER_TORCH_SCALE;
    MAAGE_SHADER_VEC3 emission = MAAGE_SHADER_VEC3(torch,
        torch * (torch * MAAGE_RENDER_TORCH_GREEN_SCALE + MAAGE_RENDER_TORCH_GREEN_MINIMUM),
        torch * (torch * torch * MAAGE_RENDER_TORCH_BLUE_SCALE + MAAGE_RENDER_TORCH_BLUE_MINIMUM));
    MAAGE_SHADER_VEC3 ambient = MAAGE_SHADER_VEC3(
        sky * (daylight * MAAGE_RENDER_AMBIENT_SCALE + MAAGE_RENDER_AMBIENT_MINIMUM),
        sky * (daylight * MAAGE_RENDER_AMBIENT_SCALE + MAAGE_RENDER_AMBIENT_MINIMUM), sky);
    return MAAGE_SHADER_SATURATE(MAAGE_SHADER_SATURATE((ambient + emission) *
        MAAGE_RENDER_LIGHT_GAIN + MAAGE_RENDER_LIGHT_MINIMUM) *
        MAAGE_RENDER_LIGHT_GAIN + MAAGE_RENDER_LIGHT_MINIMUM);
}

float maage_shader_alpha(float material, float texel_alpha, float vertex_alpha)
{
    return material == float(MAAGE_MESH_MATERIAL_OPAQUE) ? 1.0 : texel_alpha * vertex_alpha;
}
#endif
