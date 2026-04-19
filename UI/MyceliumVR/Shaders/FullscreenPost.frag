#version 450

layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(set = 0, binding = 1) uniform sampler2D aux_texture;

layout(push_constant) uniform PostPushConstants {
    float inv_width;
    float inv_height;
    float threshold;
    float bloom_strength;
    uint mode;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

vec3 sample_source(vec2 uv)
{
    return texture(source_texture, uv).rgb;
}

void main()
{
    if (pc.mode == 0u) {
        vec3 color = sample_source(v_uv);
        float brightness = max(max(color.r, color.g), color.b);
        out_color = brightness > pc.threshold ? vec4(color, 1.0) : vec4(0.0);
        return;
    }

    vec2 texel = vec2(pc.inv_width, pc.inv_height);
    vec2 axis = pc.mode == 1u ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    if (pc.mode == 1u || pc.mode == 2u) {
        vec3 color = sample_source(v_uv) * 0.227027;
        color += sample_source(v_uv + axis * 1.384615) * 0.316216;
        color += sample_source(v_uv - axis * 1.384615) * 0.316216;
        color += sample_source(v_uv + axis * 3.230769) * 0.070270;
        color += sample_source(v_uv - axis * 3.230769) * 0.070270;
        out_color = vec4(color, 1.0);
        return;
    }

    vec3 scene = sample_source(v_uv);
    vec3 bloom = texture(aux_texture, v_uv).rgb;
    out_color = vec4(scene + bloom * pc.bloom_strength, 1.0);
}
