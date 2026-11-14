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

/* The surface table retains its two vec4 layout. Decode the exact packed
 * integer once per vertex; the fragment receives a flat face selector. */
float maage_shader_surface_material(float packed_surface) {
    return float(uint(packed_surface) % uint(MAAGE_MESH_MATERIAL_COUNT));
}
uint maage_shader_surface_rotation(float packed_surface) {
    return uint(packed_surface) / uint(MAAGE_MESH_MATERIAL_COUNT);
}

MAAGE_SHADER_VEC2 maage_shader_tile_uv(MAAGE_SHADER_VEC2 uv, MAAGE_SHADER_VEC4 tile, MAAGE_SHADER_VEC3 world_position, uint rotation_face) {
    if (tile.z > tile.x && tile.w > tile.y) {
        MAAGE_SHADER_VEC2 local_uv = uv - floor(uv);
        if (rotation_face != 0u) {
            /* Recover the owning cell, including at negative coordinates and
             * on positive faces lying exactly on the next cell's boundary. */
            uint face = rotation_face - 1u;
            uint axis = face / 2u;
            world_position[axis] += (face & 1u) != 0u ? -0.5 : 0.5;
            MAAGE_SHADER_VEC3 cell = floor(world_position);
            uint hash = uint(int(cell.x)) * 0x9e3779b1u ^ uint(int(cell.y)) * 0x85ebca77u ^ uint(int(cell.z)) * 0xc2b2ae3du;
            hash ^= hash >> 16u;
            hash *= 0x7feb352du;
            hash ^= hash >> 15u;
            hash *= 0x846ca68bu;
            hash ^= hash >> 16u;
            uint rotation = hash & 3u;
            if (rotation == 1u) local_uv = MAAGE_SHADER_VEC2(1.0 - local_uv.y, local_uv.x);
            else if (rotation == 2u) local_uv = MAAGE_SHADER_VEC2(1.0) - local_uv;
            else if (rotation == 3u) local_uv = MAAGE_SHADER_VEC2(local_uv.y, 1.0 - local_uv.x);
        }
        return tile.xy + local_uv * (tile.zw - tile.xy);
    }
    return uv;
}

MAAGE_SHADER_VEC3 maage_shader_lighting(MAAGE_SHADER_VEC2 level, float daylight) {
    float floor_light = max(-daylight, 0.0);
    daylight = max(daylight, 0.0);
    MAAGE_SHADER_VEC2 normalized = MAAGE_SHADER_SATURATE(level);
    MAAGE_SHADER_VEC2 brightness = normalized / (MAAGE_RENDER_LIGHT_CURVE_BASE - MAAGE_RENDER_LIGHT_CURVE_SCALE * normalized);
    float sky = brightness.x * (daylight * MAAGE_RENDER_SKY_SCALE + MAAGE_RENDER_SKY_MINIMUM);
    float torch = brightness.y * MAAGE_RENDER_TORCH_SCALE;
    MAAGE_SHADER_VEC3 emission = MAAGE_SHADER_VEC3(torch, torch * (torch * MAAGE_RENDER_TORCH_GREEN_SCALE + MAAGE_RENDER_TORCH_GREEN_MINIMUM), torch * (torch * torch * MAAGE_RENDER_TORCH_BLUE_SCALE + MAAGE_RENDER_TORCH_BLUE_MINIMUM));
    MAAGE_SHADER_VEC3 ambient = MAAGE_SHADER_VEC3(sky * (daylight * MAAGE_RENDER_AMBIENT_SCALE + MAAGE_RENDER_AMBIENT_MINIMUM), sky * (daylight * MAAGE_RENDER_AMBIENT_SCALE + MAAGE_RENDER_AMBIENT_MINIMUM), sky);
    return max(MAAGE_SHADER_VEC3(floor_light), MAAGE_SHADER_SATURATE(MAAGE_SHADER_SATURATE((ambient + emission) * MAAGE_RENDER_LIGHT_GAIN + MAAGE_RENDER_LIGHT_MINIMUM) * MAAGE_RENDER_LIGHT_GAIN + MAAGE_RENDER_LIGHT_MINIMUM));
}

bool maage_shader_cutout(float material, float alpha) {
    return material == float(MAAGE_MESH_MATERIAL_CUTOUT) && alpha < MAAGE_RENDER_CUTOUT_ALPHA;
}
MAAGE_SHADER_VEC4 maage_shader_mesh_color(MAAGE_SHADER_VEC4 texel, MAAGE_SHADER_VEC4 color, MAAGE_SHADER_VEC2 level, float material, MAAGE_SHADER_VEC4 lighting, MAAGE_SHADER_VEC4 parameters) {
    if (material == float(MAAGE_MESH_MATERIAL_MULTIPLY)) return MAAGE_SHADER_VEC4(mix(MAAGE_SHADER_VEC3(0.5), texel.rgb * color.rgb, texel.a), 1.0);
    MAAGE_SHADER_VEC3 light = maage_shader_lighting(lighting.w > 0.0 ? lighting.yz : level, lighting.x);
    float alpha = material == float(MAAGE_MESH_MATERIAL_OPAQUE) ? 1.0 : texel.a * color.a;
    MAAGE_SHADER_VEC3 rgb = mix(texel.rgb * color.rgb * light, parameters.yzw * light, max(lighting.w - 1.0, 0.0));
    return MAAGE_SHADER_VEC4(rgb, alpha);
}
MAAGE_SHADER_VEC4 maage_shader_fog(MAAGE_SHADER_VEC4 color, MAAGE_SHADER_VEC3 position, float material, MAAGE_SHADER_VEC4 fog, MAAGE_SHADER_VEC4 range) {
    float visibility = 1.0;
    if (range.y > range.x) {
        /* Interpolate camera coordinates, then measure radius per fragment;
         * vertex distances would reveal large triangle/quad boundaries. */
        float radius = length(position * MAAGE_SHADER_VEC3(range.zw, 1.0));
        visibility = 1.0 - smoothstep(range.x, range.y, radius);
    } else if (fog.w > 0.0) visibility = exp(-fog.w * max(position.z, 0.0));
    else return color;
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
