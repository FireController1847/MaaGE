#version 330 core
#include "common.glsl"
in vec2 maage_uv;
in vec4 maage_color;
out vec4 maage_result;

void main() {
    maage_result = texture(maage_source, maage_uv) * maage_color;
}
