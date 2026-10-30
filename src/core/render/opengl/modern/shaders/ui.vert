#version 330 core
#include "common.glsl"
#define MAAGE_VERTEX_FIELD(slot, type, name) layout(location = slot) in type name;
MAAGE_RENDER_UI_ATTRIBUTES(MAAGE_VERTEX_FIELD)
#undef MAAGE_VERTEX_FIELD
layout(location = MAAGE_RENDER_UI_DEPTH_LOCATION) in float maage_depth;
out vec2 maage_uv;
out vec4 maage_color;

void main() {
    gl_Position = maage_shader_ui_position(position, maage.parameters.yz, maage_depth);
    maage_uv = uv;
    maage_color = color;
}
