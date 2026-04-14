#version 450

layout(set = 0, binding = 0) uniform sampler2D panel_texture;

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(panel_texture, v_uv);
}
