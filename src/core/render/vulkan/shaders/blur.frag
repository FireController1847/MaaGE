#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../render_shader_defs.h"

layout(constant_id = 0) const uint maage_output_srgb = 0;
layout(set = 0, binding = 0) uniform sampler2D maage_source;
layout(push_constant) uniform maage_uniforms {
    mat4 matrix;
    vec4 lighting;
    vec4 parameters;
} maage;
layout(location = 0) in vec2 maage_fragment_uv;
layout(location = 0) out vec4 maage_result;

void main()
{
    int radius = clamp(int(maage.parameters.w), 0, MAAGE_RENDER_BLUR_RADIUS);
    vec4 color = texture(maage_source, maage_fragment_uv);
    float total = 1.0;
    if (radius > 0) {
        float sigma = max(maage.parameters.z, 0.001);
        float step_weight = exp(-0.5 / (sigma * sigma));
        float ratio = step_weight * step_weight;
        float weight = 1.0;
        for (int index = 1; index <= radius; ++index) {
            weight *= step_weight;
            step_weight *= ratio;
            vec2 offset = maage.parameters.xy * float(index);
            color += (texture(maage_source, maage_fragment_uv + offset) +
                texture(maage_source, maage_fragment_uv - offset)) * weight;
            total += weight * 2.0;
        }
    }
    color /= total;
    /* Scene blending uses UNORM, as Metal does. A final sRGB attachment must
     * encode these display values exactly once, after all composition. */
    if (maage_output_srgb != 0) {
        color.rgb = mix(color.rgb / 12.92,
            pow((color.rgb + 0.055) / 1.055, vec3(2.4)), greaterThan(color.rgb, vec3(0.04045)));
    }
    maage_result = color;
}
