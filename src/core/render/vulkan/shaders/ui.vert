#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#define MAAGE_VERTEX_FIELD(slot, type, name) layout(location = slot) in type name;
MAAGE_RENDER_UI_ATTRIBUTES(MAAGE_VERTEX_FIELD)
#undef MAAGE_VERTEX_FIELD
layout(location = MAAGE_RENDER_UI_DEPTH_LOCATION) in float maage_depth;
layout(location = 0) out vec2 maage_fragment_uv;
layout(location = 1) out vec4 maage_fragment_color;

void main()
{
    gl_Position = maage_shader_ui_position(position, maage.parameters.yz, maage_depth);
    gl_Position.y = -gl_Position.y;
    maage_fragment_uv = uv;
    maage_fragment_color = color;
}
