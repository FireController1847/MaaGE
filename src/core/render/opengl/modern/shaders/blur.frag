#version 330 core
#include "common.glsl"
in vec2 maage_uv;
out vec4 maage_result;

void main() {
    int radius = clamp(int(maage.parameters.w), 0, MAAGE_RENDER_BLUR_RADIUS);
    vec4 color = texture(maage_source, maage_uv);
    float total = 1.0;
    float sigma = max(maage.parameters.z, 0.001);
    float step_weight = exp(-0.5 / (sigma * sigma));
    float ratio = step_weight * step_weight;
    float weight = 1.0;
    for (int index = 1; index <= radius; ++index) {
        weight *= step_weight;
        step_weight *= ratio;
        vec2 offset = maage.parameters.xy * float(index);
        color += (texture(maage_source, maage_uv + offset) + texture(maage_source, maage_uv - offset)) * weight;
        total += weight * 2.0;
    }
    maage_result = color / total;
}
