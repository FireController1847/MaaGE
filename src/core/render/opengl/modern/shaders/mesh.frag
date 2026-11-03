#version 330 core
#include "common.glsl"
in vec2 maage_uv;
in vec4 maage_color;
in vec2 maage_light;
flat in float maage_material;
in vec3 maage_fog_position;
flat in vec4 maage_tile;
out vec4 maage_result;

void main() {
    vec4 texel = texture(maage_source, maage_shader_tile_uv(maage_uv, maage_tile));
    if (maage_shader_cutout(maage_material, texel.a)) discard;
    maage_result = maage_shader_fog(maage_shader_mesh_color(texel, maage_color, maage_light, maage_material, maage.lighting, maage.parameters), maage_fog_position, maage_material, maage.fog, maage.fog_distance);
}
