#include <metal_stdlib>
using namespace metal;

/* Packed fields match maage_mesh_vertex without graphics types in the C API. */
struct maage_vertex {
    packed_float3 position;
    packed_float3 normal;
    packed_float2 uv;
    packed_float4 color;
    packed_float2 light;
    float material;
    packed_float3 animation;
};
struct maage_fragment {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float2 light;
    float material [[flat]];
};
vertex maage_fragment maage_mesh_vertex_shader(uint index [[vertex_id]],
    const device maage_vertex *vertices [[buffer(0)]],
    constant float4x4 &view_projection [[buffer(1)]],
    constant float &animation_tick [[buffer(2)]])
{
    maage_fragment result;
    result.position = view_projection * float4(vertices[index].position, 1.0);
    result.uv = vertices[index].uv;
    float3 animation = vertices[index].animation;
    if (abs(animation.y) > 1.0 && animation.z > 0.0) {
        float count = abs(animation.y);
        float period = animation.y < 0.0 ? count * 2.0 - 2.0 : count;
        float frame = fmod(floor(animation_tick / animation.z), period);
        if (frame >= count) frame = period - frame;
        result.uv.y += frame * animation.x;
    }
    result.color = vertices[index].color;
    result.light = vertices[index].light;
    result.material = vertices[index].material;
    return result;
}
fragment float4 maage_mesh_fragment_shader(maage_fragment input [[stage_in]],
    texture2d<float> atlas [[texture(0)]], constant float4 &lighting [[buffer(0)]])
{
    constexpr sampler pixels(coord::normalized, address::clamp_to_edge,
        filter::nearest, mip_filter::none);
    float4 texel = atlas.sample(pixels, input.uv);
    if (input.material == 1.0 && texel.a < 0.1) discard_fragment();
    if (input.material == 3.0)
        return float4(mix(float3(0.5), texel.rgb * input.color.rgb, texel.a), 1.0);
    float2 level = lighting.w > 0.0 ? lighting.yz : input.light;
    /* Nonlinear level response and warm emission, independent of daylight.
     * Evaluate the two static channels without changing chunk light at night. */
    float2 brightness = saturate(level) / (4.0 - 3.0 * saturate(level));
    float sky = brightness.x * (lighting.x * 0.95 + 0.05);
    float torch = brightness.y * 1.5;
    float3 emission = float3(torch, torch * (torch * 0.36 + 0.64),
        torch * (torch * torch * 0.6 + 0.4));
    float3 ambient = float3(sky * (lighting.x * 0.65 + 0.35),
        sky * (lighting.x * 0.65 + 0.35), sky);
    float3 light = saturate((saturate((ambient + emission) * 0.96 + 0.03)) * 0.96 + 0.03);
    float alpha = input.material == 0.0 ? 1.0 : texel.a * input.color.a;
    return float4(texel.rgb * input.color.rgb * light, alpha);
}

struct maage_ui_vertex {
    packed_float2 position;
    packed_float2 uv;
    packed_float4 color;
};
vertex maage_fragment maage_ui_vertex_shader(uint index [[vertex_id]],
    const device maage_ui_vertex *vertices [[buffer(0)]],
    constant float2 &logical_size [[buffer(1)]],
    const device float *projective_depth [[buffer(2)]],
    constant uint &projective [[buffer(3)]])
{
    maage_fragment result;
    float2 point = vertices[index].position / logical_size;
    float depth = projective ? projective_depth[index] : 1.0;
    result.position = float4((point.x * 2.0 - 1.0) * depth,
        (1.0 - point.y * 2.0) * depth, 0.0, depth);
    result.uv = vertices[index].uv;
    result.color = vertices[index].color;
    result.light = float2(0);
    result.material = 0;
    return result;
}
fragment float4 maage_ui_fragment_shader(maage_fragment input [[stage_in]],
    texture2d<float> atlas [[texture(0)]])
{
    constexpr sampler pixels(coord::normalized, address::clamp_to_edge,
        filter::nearest, mip_filter::none);
    return atlas.sample(pixels, input.uv) * input.color;
}
