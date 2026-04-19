#version 450

layout(push_constant) uniform ShadowPushConstants {
    mat4 light_view_projection;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 4) in vec4 in_model_col0;
layout(location = 5) in vec4 in_model_col1;
layout(location = 6) in vec4 in_model_col2;
layout(location = 7) in vec4 in_model_col3;
layout(location = 8) in uint in_material_index;

void main()
{
    mat4 model = mat4(in_model_col0, in_model_col1, in_model_col2, in_model_col3);
    vec4 world_pos = model * vec4(in_position, 1.0);
    gl_Position = pc.light_view_projection * world_pos;
}
