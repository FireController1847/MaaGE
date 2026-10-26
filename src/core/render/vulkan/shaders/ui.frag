#version 450
layout(set = 0, binding = 0) uniform sampler2D maage_atlas;
layout(location = 0) in vec2 maage_fragment_uv;
layout(location = 1) in vec4 maage_fragment_color;
layout(location = 0) out vec4 maage_result;

void main()
{
    maage_result = texture(maage_atlas, maage_fragment_uv) * maage_fragment_color;
}
