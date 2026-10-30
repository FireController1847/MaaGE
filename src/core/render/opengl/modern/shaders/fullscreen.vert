#version 330 core
#include "common.glsl"
out vec2 maage_uv;

void main() {
    vec2 corner = maage_shader_fullscreen_uv(gl_VertexID);
    gl_Position = maage_shader_ui_position(corner, vec2(1.0), 1.0);
    /* Render-target rows have native bottom-left UVs. Uploaded atlases keep
     * their caller-provided top-first rows and never use this conversion. */
    maage_uv = vec2(corner.x, 1.0 - corner.y);
}
