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
 * integer once per vertex; the fragment receives a flat variation/face selector. */
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
            /* Boundary planes need an inward offset; interior planes already
             * lie inside their owner (including shallow snow and farmland). */
            uint selector = (rotation_face - 1u) % uint(MAAGE_MESH_VARIATION_FACE_RANGE);
            uint face = selector % uint(MAAGE_MESH_ROTATION_FACE_COUNT);
            uint variation = selector / uint(MAAGE_MESH_ROTATION_FACE_COUNT) + 1u;
            uint axis = face / 2u;
            if (rotation_face <= uint(MAAGE_MESH_VARIATION_FACE_RANGE)) world_position[axis] += (face & 1u) != 0u ? -0.5 : 0.5;
            MAAGE_SHADER_VEC3 cell = floor(world_position);
            uint hash = uint(int(cell.x)) * 0x9e3779b1u ^ uint(int(cell.y)) * 0x85ebca77u ^ uint(int(cell.z)) * 0xc2b2ae3du ^ face * 0x27d4eb2du;
            hash ^= hash >> 16u;
            hash *= 0x7feb352du;
            hash ^= hash >> 15u;
            hash *= 0x846ca68bu;
            hash ^= hash >> 16u;
            uint rotation = 0u;
            if (variation == uint(MAAGE_TEXTURE_VARIATION_ROTATE_90) || variation == uint(MAAGE_TEXTURE_VARIATION_ROTATE_90_MIRROR)) rotation = hash & 3u;
            else if (variation == uint(MAAGE_TEXTURE_VARIATION_ROTATE_180) || variation == uint(MAAGE_TEXTURE_VARIATION_ROTATE_180_MIRROR)) rotation = (hash & 1u) * 2u;
            if ((variation == uint(MAAGE_TEXTURE_VARIATION_MIRROR) || variation == uint(MAAGE_TEXTURE_VARIATION_ROTATE_90_MIRROR) || variation == uint(MAAGE_TEXTURE_VARIATION_ROTATE_180_MIRROR)) && (hash & 4u) != 0u) local_uv.x = 1.0 - local_uv.x;
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
/* Recover the world-space eye ray from clip X, Y and W. The three
 * projection rows include camera orientation, aspect, FOV and view bobbing;
 * their translation cancels for a direction. Intersect successive virtual
 * horizontal planes behind the portal, rather than sliding a flat pattern
 * with screen position. Bound the slope at grazing angles to avoid aliasing. */
MAAGE_SHADER_VEC4 maage_shader_starfield(MAAGE_SHADER_VEC3 world, MAAGE_SHADER_VEC3 projected, MAAGE_SHADER_MAT4 matrix, float tick) {
    MAAGE_SHADER_VEC3 color = MAAGE_SHADER_VEC3(0.003, 0.008, 0.012);
    MAAGE_SHADER_VEC3 row_x = MAAGE_SHADER_VEC3(matrix[0].x, matrix[1].x, matrix[2].x);
    MAAGE_SHADER_VEC3 row_y = MAAGE_SHADER_VEC3(matrix[0].y, matrix[1].y, matrix[2].y);
    MAAGE_SHADER_VEC3 row_w = MAAGE_SHADER_VEC3(matrix[0].w, matrix[1].w, matrix[2].w);
    MAAGE_SHADER_VEC3 cofactor_x = cross(row_y, row_w);
    float determinant = dot(row_x, cofactor_x);
    MAAGE_SHADER_VEC2 slope = MAAGE_SHADER_VEC2(0.0);
    if (abs(determinant) > 0.000001) {
        MAAGE_SHADER_VEC3 ray = (cofactor_x * projected.x + cross(row_w, row_x) * projected.y + cross(row_x, row_y) * projected.z) / determinant;
        slope = ray.xz / max(abs(ray.y), max(length(ray) * 0.15, 0.0001));
    }
    for (int layer = 0; layer < 8; ++layer) {
        float depth = float(layer + 1);
        float plane_distance = 0.35 * depth * depth;
        float density = 3.5 + depth * 0.8;
        MAAGE_SHADER_VEC2 p = (world.xz + slope * plane_distance) * density + MAAGE_SHADER_VEC2(sin(tick * 0.0017223644 + depth), cos(tick * 0.0017223644 + depth)) * 0.6;
        MAAGE_SHADER_VEC2 cell = floor(p);
        float hash = sin(dot(cell, MAAGE_SHADER_VEC2(127.1 + depth, 311.7 - depth))) * 43758.5453;
        hash -= floor(hash);
        float radius = length((p - floor(p)) - MAAGE_SHADER_VEC2(0.5));
        float star = (1.0 - smoothstep(0.015, 0.12, radius)) * step(0.82, hash) / (0.8 + depth * 0.22);
        color += MAAGE_SHADER_VEC3(0.18 + hash * 0.25, 0.45 + hash * 0.5, 0.6 + hash * 0.4) * star;
    }
    return MAAGE_SHADER_VEC4(color, 1.0);
}
MAAGE_SHADER_VEC4 maage_shader_mesh_color(MAAGE_SHADER_VEC4 texel, MAAGE_SHADER_VEC4 color, MAAGE_SHADER_VEC2 level, float material, MAAGE_SHADER_VEC4 lighting, MAAGE_SHADER_VEC4 parameters) {
    if (material == float(MAAGE_MESH_MATERIAL_MULTIPLY)) return MAAGE_SHADER_VEC4(mix(MAAGE_SHADER_VEC3(0.5), texel.rgb * color.rgb, texel.a), 1.0);
    MAAGE_SHADER_VEC3 light = maage_shader_lighting(lighting.w > 0.0 ? lighting.yz : level, lighting.x);
    float alpha = material == float(MAAGE_MESH_MATERIAL_OPAQUE) ? 1.0 : texel.a * color.a;
    MAAGE_SHADER_VEC3 rgb = mix(texel.rgb * color.rgb * light, parameters.yzw * light, max(lighting.w - 1.0, 0.0));
    return MAAGE_SHADER_VEC4(rgb, alpha);
}
MAAGE_SHADER_VEC4 maage_shader_fog(MAAGE_SHADER_VEC4 color, MAAGE_SHADER_VEC3 position, float material, MAAGE_SHADER_VEC4 fog, MAAGE_SHADER_VEC4 range, MAAGE_SHADER_VEC4 lighting) {
    float visibility = 1.0;
    if (range.y > range.x) {
        /* Interpolate camera coordinates, then measure radius per fragment;
         * vertex distances would reveal large triangle/quad boundaries. */
        float radius = length(position * MAAGE_SHADER_VEC3(range.zw, 1.0));
        visibility = 1.0 - smoothstep(range.x, range.y, radius);
    } else if (fog.w > 0.0) visibility = exp(-fog.w * max(position.z, 0.0));
    /* Direct meshes reuse lighting.y for extra fog, never instance lighting.
     * Multiply materials approach their neutral color; alpha/depth stay intact. */
    if (lighting.w == 0.0) visibility *= 1.0 - clamp(lighting.y, 0.0, 1.0);
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
