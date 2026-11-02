#include <metal_stdlib>
using namespace metal;
#define MAAGE_SHADER_VEC2 float2
#define MAAGE_SHADER_VEC3 float3
#define MAAGE_SHADER_VEC4 float4
#define MAAGE_SHADER_MOD fmod
#define MAAGE_SHADER_SATURATE(value) saturate(value)
#include "../render_shader_common.h"

struct maage_uniforms {
    MAAGE_RENDER_UNIFORM_FIELDS(float4x4, float4)
};
/* Packed vertex and immutable surface entries are each 32 bytes. */
#define MAAGE_VERTEX_VEC1 float
#define MAAGE_VERTEX_VEC2 packed_float2
#define MAAGE_VERTEX_VEC3 packed_float3
#define MAAGE_VERTEX_VEC4 packed_float4
#define MAAGE_VERTEX_BYTE4 uchar4
#define MAAGE_VERTEX_BYTE2 uchar2
#define MAAGE_VERTEX_UINT1 uint
#define MAAGE_VERTEX_FIELD(slot, type, name) type name;
struct maage_vertex {
    packed_float3 position;
    packed_float2 uv;
    uchar4 color;
    uchar2 light;
    uchar2 padding;
    uint surface;
};
struct maage_surface {
    packed_float3 animation;
    float material;
    packed_float4 tile;
};
struct maage_fragment {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float2 light;
    float material [[flat]];
    float3 fog_position;
    float4 tile [[flat]];
};
vertex maage_fragment maage_mesh_vertex_shader(uint index [[vertex_id]], const device maage_vertex *vertices [[buffer(0)]], constant maage_uniforms &uniforms [[buffer(1)]], const device maage_surface *surfaces [[buffer(2)]]) {
    maage_fragment result;
    maage_surface surface = surfaces[vertices[index].surface];
    result.position = uniforms.matrix * float4(vertices[index].position, 1.0);
    result.fog_position = result.position.xyw;
    result.uv = vertices[index].uv;
    result.tile = surface.tile;
    float animation = maage_shader_animation(surface.animation, uniforms.parameters.x);
    if (result.tile.z > result.tile.x) result.tile.yw += animation;
    else result.uv.y += animation;
    result.color = float4(vertices[index].color) / 255.0;
    result.light = float2(vertices[index].light) / 255.0;
    result.material = surface.material;
    return result;
}
fragment float4 maage_mesh_fragment_shader(maage_fragment input [[stage_in]], texture2d<float> atlas [[texture(0)]], constant maage_uniforms &uniforms [[buffer(0)]]) {
    constexpr sampler pixels(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::none);
    float4 texel = atlas.sample(pixels, maage_shader_tile_uv(input.uv, input.tile));
    if (maage_shader_cutout(input.material, texel.a)) discard_fragment();
    return maage_shader_fog(maage_shader_mesh_color(texel, input.color, input.light, input.material, uniforms.lighting), input.fog_position, input.material, uniforms.fog, uniforms.fog_distance);
}

struct maage_ui_vertex {
    MAAGE_RENDER_UI_ATTRIBUTES(MAAGE_VERTEX_FIELD)
    float depth;
};
#undef MAAGE_VERTEX_FIELD
vertex maage_fragment maage_ui_vertex_shader(uint index [[vertex_id]], const device maage_ui_vertex *vertices [[buffer(0)]], constant maage_uniforms &uniforms [[buffer(1)]]) {
    maage_fragment result;
    result.position = maage_shader_ui_position(vertices[index].position, uniforms.parameters.yz, vertices[index].depth);
    result.uv = vertices[index].uv;
    result.color = vertices[index].color;
    result.light = float2(0);
    result.material = 0;
    result.tile = float4(0);
    result.fog_position = float3(0);
    return result;
}
fragment float4 maage_ui_fragment_shader(maage_fragment input [[stage_in]], texture2d<float> atlas [[texture(0)]]) {
    constexpr sampler pixels(coord::normalized, address::clamp_to_edge, filter::nearest, mip_filter::none);
    return atlas.sample(pixels, input.uv) * input.color;
}
vertex maage_fragment maage_fullscreen_vertex_shader(uint index [[vertex_id]]) {
    maage_fragment result;
    result.uv = maage_shader_fullscreen_uv(int(index));
    result.position = maage_shader_ui_position(result.uv, float2(1.0), 1.0);
    result.color = float4(1.0);
    result.light = float2(0.0);
    result.material = 0;
    result.tile = float4(0);
    result.fog_position = float3(0);
    return result;
}
