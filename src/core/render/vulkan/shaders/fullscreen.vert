#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) out vec2 maage_fragment_uv;

void main()
{
    /* One oversized triangle covers the target without a diagonal seam. */
    vec2 corner = maage_shader_fullscreen_uv(gl_VertexIndex);
    gl_Position = maage_shader_ui_position(corner, vec2(1.0), 1.0);
    gl_Position.y = -gl_Position.y;
    maage_fragment_uv = corner;
}
