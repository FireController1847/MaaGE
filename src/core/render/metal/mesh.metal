#include <metal_stdlib>
using namespace metal;

/* Packed fields match maage_mesh_vertex without graphics types in the C API. */
struct maage_vertex {
    packed_float3 position;
    packed_float3 normal;
    packed_float2 uv;
    packed_float4 color;
    packed_float2 light;
};
struct maage_fragment {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float2 light;
};
vertex maage_fragment maage_mesh_vertex_shader(uint index [[vertex_id]],
    const device maage_vertex *vertices [[buffer(0)]],
    constant float4x4 &view_projection [[buffer(1)]])
{
    maage_fragment result;
    result.position = view_projection * float4(vertices[index].position, 1.0);
    result.uv = vertices[index].uv;
    result.color = vertices[index].color;
    result.light = vertices[index].light;
    return result;
}
fragment float4 maage_mesh_fragment_shader(maage_fragment input [[stage_in]],
    texture2d<float> atlas [[texture(0)]], constant float &sky_light [[buffer(0)]])
{
    constexpr sampler pixels(coord::normalized, address::clamp_to_edge,
        filter::nearest, mip_filter::none);
    float4 texel = atlas.sample(pixels, input.uv);
    if (texel.a < 0.5) discard_fragment();
    float brightness = 0.12 + 0.88 * max(input.light.x * sky_light, input.light.y);
    return float4(texel.rgb * input.color.rgb * brightness, texel.a * input.color.a);
}

struct maage_ui_vertex {
    packed_float2 position;
    packed_float2 uv;
    packed_float4 color;
};
vertex maage_fragment maage_ui_vertex_shader(uint index [[vertex_id]],
    const device maage_ui_vertex *vertices [[buffer(0)]],
    constant float2 &logical_size [[buffer(1)]])
{
    maage_fragment result;
    float2 point = vertices[index].position / logical_size;
    result.position = float4(point.x * 2.0 - 1.0, 1.0 - point.y * 2.0, 0.0, 1.0);
    result.uv = vertices[index].uv;
    result.color = vertices[index].color;
    result.light = float2(0);
    return result;
}
fragment float4 maage_ui_fragment_shader(maage_fragment input [[stage_in]],
    texture2d<float> atlas [[texture(0)]])
{
    constexpr sampler pixels(coord::normalized, address::clamp_to_edge,
        filter::nearest, mip_filter::none);
    return atlas.sample(pixels, input.uv) * input.color;
}
