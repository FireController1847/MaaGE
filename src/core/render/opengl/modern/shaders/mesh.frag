#version 330 core
#include "common.glsl"
in vec2 maage_uv;
in vec4 maage_color;
in vec2 maage_light;
flat in float maage_material;
in vec3 maage_fog_position;
flat in vec4 maage_tile;
in vec3 maage_world_position;
flat in uint maage_rotation_face;
out vec4 maage_result;

void main() {
    if (maage_material == float(MAAGE_MESH_MATERIAL_STARFIELD)) {
        maage_result = maage_shader_fog(maage_shader_starfield(maage_world_position, maage_fog_position, maage.parameters.x), maage_fog_position, maage_material, maage.fog, maage.fog_distance);
        return;
    }
    vec4 texel = texture(maage_source, maage_shader_tile_uv(maage_uv, maage_tile, maage_world_position, maage_rotation_face));
    if (maage_shader_cutout(maage_material, texel.a)) discard;
    maage_result = maage_shader_fog(maage_shader_mesh_color(texel, maage_color, maage_light, maage_material, maage.lighting, maage.parameters), maage_fog_position, maage_material, maage.fog, maage.fog_distance);
}
