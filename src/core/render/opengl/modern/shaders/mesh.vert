#version 330 core
#include "common.glsl"
#define MAAGE_VERTEX_FIELD(slot, type, name) layout(location = slot) in type name;
MAAGE_RENDER_MESH_ATTRIBUTES(MAAGE_VERTEX_FIELD)
#undef MAAGE_VERTEX_FIELD
out vec2 maage_uv;
out vec4 maage_color;
out vec2 maage_light;
flat out float maage_material;

void main() {
    gl_Position = maage.matrix * vec4(position, 1.0);
    /* Shared depth 0..W becomes OpenGL -W..W; XY and CCW winding stay intact. */
    gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;
    maage_uv = uv;
    maage_uv.y += maage_shader_animation(animation, maage.parameters.x);
    maage_color = color;
    maage_light = light;
    maage_material = material;
}
