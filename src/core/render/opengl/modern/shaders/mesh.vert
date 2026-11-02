#version 330 core
#include "common.glsl"
#define MAAGE_VERTEX_BYTE4 vec4
#define MAAGE_VERTEX_BYTE2 vec2
#define MAAGE_VERTEX_UINT1 uint
#define MAAGE_VERTEX_FIELD(slot, type, name) layout(location = slot) in type name;
MAAGE_RENDER_GPU_MESH_ATTRIBUTES(MAAGE_VERTEX_FIELD)
#undef MAAGE_VERTEX_FIELD
out vec2 maage_uv;
out vec4 maage_color;
out vec2 maage_light;
flat out float maage_material;
out vec3 maage_fog_position;

uniform samplerBuffer maage_surfaces;
flat out vec4 maage_tile;

void main() {
    gl_Position = maage.matrix * vec4(position, 1.0);
    maage_fog_position = gl_Position.xyw;
    /* Shared depth 0..W becomes OpenGL -W..W; XY and CCW winding stay intact. */
    gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;
    vec4 parameters = texelFetch(maage_surfaces, int(surface) * 2);
    maage_tile = texelFetch(maage_surfaces, int(surface) * 2 + 1);
    maage_uv = uv;
    float animation_offset = maage_shader_animation(parameters.xyz, maage.parameters.x);
    if (maage_tile.z > maage_tile.x) maage_tile.yw += animation_offset;
    else maage_uv.y += animation_offset;
    maage_color = color;
    maage_light = light;
    maage_material = parameters.w;
}
