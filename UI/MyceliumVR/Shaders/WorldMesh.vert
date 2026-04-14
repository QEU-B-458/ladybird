#version 450

layout(push_constant) uniform WorldPushConstants {
    mat4 view_projection;
} pc;

// Binding 0: per-vertex mesh data (local space, no transform applied).
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;

// Binding 1: per-instance model matrix (four vec4 columns, column-major).
layout(location = 4) in vec4 in_model_col0;
layout(location = 5) in vec4 in_model_col1;
layout(location = 6) in vec4 in_model_col2;
layout(location = 7) in vec4 in_model_col3;

// Tangent (xyz = tangent direction, w = bitangent sign; all zero = no tangent data).
layout(location = 8) in vec4 in_tangent;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_color;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec3 v_world_pos;
layout(location = 4) out vec3 v_tangent;
layout(location = 5) out vec3 v_bitangent;

void main()
{
    mat4 model = mat4(in_model_col0, in_model_col1, in_model_col2, in_model_col3);
    vec4 world_pos4 = model * vec4(in_position, 1.0);
    gl_Position = pc.view_projection * world_pos4;
    v_world_pos = world_pos4.xyz;

    mat3 normal_matrix = mat3(model);
    v_normal = normalize(normal_matrix * in_normal);
    v_color  = in_color;
    v_uv     = in_uv;

    // Transform tangent + bitangent into world space.
    // w component carries the bitangent sign from the mesh (set to 0 when no tangents exist).
    vec3 T = normalize(normal_matrix * in_tangent.xyz);
    vec3 B = normalize(cross(v_normal, T) * in_tangent.w);
    v_tangent   = T;
    v_bitangent = B;
}
