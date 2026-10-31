#ifndef MAAGE_RENDER_SHADER_COMMON_H
#define MAAGE_RENDER_SHADER_COMMON_H
#include "render_shader_defs.h"

/* Native MSL and GLSL supply their vector and intrinsic spellings. The equations
 * remain authoritative here; the C renderer consumes only the numeric header. */
float maage_shader_animation(MAAGE_SHADER_VEC3 animation, float tick) {
    if (abs(animation.y) <= 1.0 || animation.z <= 0.0) return 0.0;
    float count = abs(animation.y);
    float period = animation.y < 0.0 ? count * 2.0 - 2.0 : count;
    float frame = MAAGE_SHADER_MOD(floor(tick / animation.z), period);
    if (frame >= count) frame = period - frame;
    return frame * animation.x;
}

MAAGE_SHADER_VEC3 maage_shader_lighting(MAAGE_SHADER_VEC2 level, float daylight) {
    MAAGE_SHADER_VEC2 normalized = MAAGE_SHADER_SATURATE(level);
    MAAGE_SHADER_VEC2 brightness = normalized / (MAAGE_RENDER_LIGHT_CURVE_BASE - MAAGE_RENDER_LIGHT_CURVE_SCALE * normalized);
    float sky = brightness.x * (daylight * MAAGE_RENDER_SKY_SCALE + MAAGE_RENDER_SKY_MINIMUM);
    float torch = brightness.y * MAAGE_RENDER_TORCH_SCALE;
    MAAGE_SHADER_VEC3 emission = MAAGE_SHADER_VEC3(torch, torch * (torch * MAAGE_RENDER_TORCH_GREEN_SCALE + MAAGE_RENDER_TORCH_GREEN_MINIMUM), torch * (torch * torch * MAAGE_RENDER_TORCH_BLUE_SCALE + MAAGE_RENDER_TORCH_BLUE_MINIMUM));
    MAAGE_SHADER_VEC3 ambient = MAAGE_SHADER_VEC3(sky * (daylight * MAAGE_RENDER_AMBIENT_SCALE + MAAGE_RENDER_AMBIENT_MINIMUM), sky * (daylight * MAAGE_RENDER_AMBIENT_SCALE + MAAGE_RENDER_AMBIENT_MINIMUM), sky);
    return MAAGE_SHADER_SATURATE(MAAGE_SHADER_SATURATE((ambient + emission) * MAAGE_RENDER_LIGHT_GAIN + MAAGE_RENDER_LIGHT_MINIMUM) * MAAGE_RENDER_LIGHT_GAIN + MAAGE_RENDER_LIGHT_MINIMUM);
}

bool maage_shader_cutout(float material, float alpha) {
    return material == float(MAAGE_MESH_MATERIAL_CUTOUT) && alpha < MAAGE_RENDER_CUTOUT_ALPHA;
}
MAAGE_SHADER_VEC4 maage_shader_mesh_color(MAAGE_SHADER_VEC4 texel, MAAGE_SHADER_VEC4 color, MAAGE_SHADER_VEC2 level, float material, MAAGE_SHADER_VEC4 lighting) {
    if (material == float(MAAGE_MESH_MATERIAL_MULTIPLY)) return MAAGE_SHADER_VEC4(mix(MAAGE_SHADER_VEC3(0.5), texel.rgb * color.rgb, texel.a), 1.0);
    MAAGE_SHADER_VEC3 light = maage_shader_lighting(lighting.w > 0.0 ? lighting.yz : level, lighting.x);
    float alpha = material == float(MAAGE_MESH_MATERIAL_OPAQUE) ? 1.0 : texel.a * color.a;
    return MAAGE_SHADER_VEC4(texel.rgb * color.rgb * light, alpha);
}
MAAGE_SHADER_VEC4 maage_shader_fog(MAAGE_SHADER_VEC4 color, float depth, float material, MAAGE_SHADER_VEC4 fog) {
    float visibility = exp(-fog.w * max(depth, 0.0));
    MAAGE_SHADER_VEC3 target = material == float(MAAGE_MESH_MATERIAL_MULTIPLY) ? MAAGE_SHADER_VEC3(0.5) : fog.rgb;
    return MAAGE_SHADER_VEC4(mix(target, color.rgb, visibility), color.a);
}
MAAGE_SHADER_VEC4 maage_shader_ui_position(MAAGE_SHADER_VEC2 position, MAAGE_SHADER_VEC2 logical_size, float depth) {
    MAAGE_SHADER_VEC2 point = position / logical_size;
    return MAAGE_SHADER_VEC4((point.x * 2.0 - 1.0) * depth, (1.0 - point.y * 2.0) * depth, 0.0, depth);
}
MAAGE_SHADER_VEC2 maage_shader_fullscreen_uv(int index) {
    return MAAGE_SHADER_VEC2(float((index << 1) & 2), float(index & 2));
}
#endif
