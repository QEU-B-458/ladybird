#version 450

layout(push_constant) uniform PushConstants {
    mat4 view_projection;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 v_uv;

void main()
{
    gl_Position = push_constants.view_projection * vec4(in_position, 1.0);
    v_uv = in_uv;
}
