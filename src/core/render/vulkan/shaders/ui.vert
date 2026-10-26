#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../render_shader_defs.h"
layout(location = MAAGE_RENDER_UI_POSITION_LOCATION) in vec2 maage_position;
layout(location = MAAGE_RENDER_UI_UV_LOCATION) in vec2 maage_uv;
layout(location = MAAGE_RENDER_UI_COLOR_LOCATION) in vec4 maage_color;
layout(location = MAAGE_RENDER_UI_DEPTH_LOCATION) in float maage_depth;
layout(push_constant) uniform maage_uniforms {
    mat4 matrix;
    vec4 lighting;
    vec4 parameters;
} maage;
layout(location = 0) out vec2 maage_fragment_uv;
layout(location = 1) out vec4 maage_fragment_color;

void main()
{
    vec2 point = maage_position / maage.parameters.yz;
    float depth = maage.parameters.w > 0.0 ? maage_depth : 1.0;
    gl_Position = vec4((point * 2.0 - 1.0) * depth, 0.0, depth);
    maage_fragment_uv = maage_uv;
    maage_fragment_color = maage_color;
}
