#version 450
#extension GL_EXT_nonuniform_qualifier : enable

// Scene lighting UBO (set 1, binding 0).
layout(set = 1, binding = 0) uniform SceneLightUBO {
    vec4  ambient;              // rgb + intensity in w
    vec4  sun_direction;        // xyz = toward light (normalized, world space), w = unused
    vec4  sun_color;            // rgb + intensity in w
    vec4  eye_position;         // xyz = camera world position, w = unused
    ivec4 light_params;         // x = point_light_count, y = shadow quality (0..9), z = screen_w, w = screen_h
    mat4  shadow_view_projection;
    vec4  shadow_params;        // x = enabled, y = depth bias, zw = shadow texel size
    vec4  cluster_params;       // x = near, y = far, z = log(far/near), w = unused
} scene;

// Clustered lighting SSBOs (set 4).
struct GPUPointLight {
    vec4 position_radius; // xyz = world position, w = influence radius
    vec4 color_intensity; // xyz = linear RGB, w = intensity
    vec4 spot_direction;  // xyz = direction (spot lights), w = type flag (0=point, 1=spot)
    vec4 spot_cone;       // x = cos(inner), y = cos(outer), zw = unused
};

layout(std430, set = 4, binding = 0) readonly buffer PointLightSSBO {
    GPUPointLight lights[];
} point_light_ssbo;

layout(std430, set = 4, binding = 1) readonly buffer ClusterCountSSBO {
    uint data[];
} cluster_count_ssbo;

layout(std430, set = 4, binding = 2) readonly buffer ClusterIndexSSBO {
    uint data[];
} cluster_index_ssbo;

#define CLUSTER_GRID_X          16
#define CLUSTER_GRID_Y           9
#define CLUSTER_GRID_Z          24
#define MAX_LIGHTS_PER_CLUSTER  32

// Bindless texture array (set 0, binding 0)
layout(set = 0, binding = 0) uniform sampler2D global_textures[];

// Material SSBO (set 2, binding 0)
struct MaterialData {
    vec4 factors;    // x=metallic, y=roughness, z=alpha_cutoff, w=alpha_mode
    vec4 emissive;   // rgb, w=unused
    vec4 base_color; // rgba

    int albedo_idx;
    int normal_idx;
    int metallic_roughness_idx;
    int emissive_idx;
    int occlusion_idx;
    int padding[3];
};

layout(std430, set = 2, binding = 0) readonly buffer MaterialSSBO {
    MaterialData materials[];
} material_ssbo;

layout(set = 3, binding = 0) uniform sampler2D shadow_map;

// Combined push constants
layout(push_constant) uniform PC {
    mat4 view_projection;
} pc;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_color;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world_pos;
layout(location = 4) in vec3 v_tangent;
layout(location = 5) in vec3 v_bitangent;
layout(location = 6) flat in uint v_material_index;

layout(location = 0) out vec4 out_color;

layout(constant_id = 0) const bool VARIANT_HAS_NORMAL_MAP = true;
layout(constant_id = 1) const int VARIANT_ALPHA_MODE = 0;

#define M_PI 3.14159265358979

// Fallback texture indices (populated by VulkanPBRManager)
#define FALLBACK_BLACK_IDX 0
#define FLAT_NORMAL_IDX 1

vec4 sample_bindless(int index, vec2 uv, vec4 fallback)
{
    if (index < 0) return fallback;
    return texture(global_textures[nonuniformEXT(index)], uv);
}

float D_GGX(float NdotH, float alpha)
{
    float a  = NdotH * alpha;
    float k  = alpha / (1.0 - NdotH * NdotH + a * a);
    return k * k * (1.0 / M_PI);
}

float V_GGX(float NdotL, float NdotV, float alpha)
{
    return 0.5 / mix(2.0 * NdotL * NdotV, NdotL + NdotV, alpha);
}

vec3 F_Schlick(float cos_theta, vec3 F0, float f90)
{
    float t = 1.0 - cos_theta;
    float t2 = t * t;
    return F0 + (f90 - F0) * (t2 * t2 * t);
}

vec3 F_Schlick_roughness(float cos_theta, vec3 F0, float roughness)
{
    vec3 r1 = max(vec3(1.0 - roughness), F0);
    float t = 1.0 - cos_theta;
    float t2 = t * t;
    return F0 + (r1 - F0) * (t2 * t2 * t);
}

vec2 env_dfg_lazarov(float roughness, float NdotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  r    = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

void cook_torrance(
    vec3  N, vec3 V, vec3 L,
    vec3  radiance,
    vec3  albedo, float metallic, float roughness, vec3 F0,
    inout vec3 Lo)
{
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return;
    vec3  H     = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float LdotH = clamp(dot(L, H), 0.0, 1.0);
    float alpha = roughness * roughness;
    float D = D_GGX(NdotH, alpha);
    float V_term = V_GGX(NdotL, NdotV, alpha);
    float f90 = clamp(50.0 * F0.g, 0.0, 1.0);
    vec3  F   = F_Schlick(LdotH, F0, f90);
    vec3 specular = D * V_term * F;
    vec3 kD      = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo * (1.0 / M_PI);
    Lo += (diffuse + specular) * radiance * NdotL;
}

float sample_shadow(vec3 world_pos, vec3 normal, vec3 light_dir)
{
    int shadow_quality = scene.light_params.y;
    if (scene.shadow_params.x < 0.5 || shadow_quality <= 0)
        return 1.0;

    vec4 shadow_clip = scene.shadow_view_projection * vec4(world_pos, 1.0);
    vec3 shadow_ndc = shadow_clip.xyz / max(shadow_clip.w, 1e-5);
    vec2 uv = shadow_ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || shadow_ndc.z < 0.0 || shadow_ndc.z > 1.0)
        return 1.0;

    float bias = max(scene.shadow_params.y * (1.0 - max(dot(normal, light_dir), 0.0)), scene.shadow_params.y * 0.5);
    vec2 texel = vec2(scene.shadow_params.z, scene.shadow_params.w);
    float visibility = 0.0;
    int kernel_radius = shadow_quality - 1;
    int kernel_width = kernel_radius * 2 + 1;
    float sample_count = float(kernel_width * kernel_width);
    for (int y = -kernel_radius; y <= kernel_radius; ++y) {
        for (int x = -kernel_radius; x <= kernel_radius; ++x) {
            float shadow_depth = texture(shadow_map, uv + vec2(x, y) * texel).r;
            visibility += (shadow_ndc.z - bias) <= shadow_depth ? 1.0 : 0.0;
        }
    }
    return visibility / sample_count;
}

void main()
{
    MaterialData mat = material_ssbo.materials[v_material_index];

    vec3 N;
    if (VARIANT_HAS_NORMAL_MAP && mat.normal_idx >= 0 && dot(v_tangent, v_tangent) > 0.01) {
        vec3 T  = normalize(v_tangent);
        vec3 B  = normalize(v_bitangent);
        vec3 Ng = normalize(v_normal);
        vec3 nm = texture(global_textures[nonuniformEXT(mat.normal_idx)], v_uv).xyz * 2.0 - 1.0;
        N = normalize(T * nm.x + B * nm.y + Ng * nm.z);
    } else {
        N = normalize(v_normal);
    }

    vec4 base_color    = mat.base_color;
    vec4 albedo_sample = sample_bindless(mat.albedo_idx, v_uv, vec4(1.0)) * vec4(v_color, 1.0) * base_color;
    vec3 albedo        = albedo_sample.rgb;

    if (VARIANT_ALPHA_MODE == 1 && albedo_sample.a < mat.factors.z)
        discard;

    if (VARIANT_ALPHA_MODE == 3) {
        float px_scale = max(length(dFdx(v_world_pos)), length(dFdy(v_world_pos)));
        px_scale = clamp(px_scale, 1e-6, 1.0);
        float lod = log2(px_scale);
        float lod_floor = floor(lod);
        float lod_fract = lod - lod_floor;
        float scale_a = exp2(lod_floor);
        float scale_b = scale_a * 2.0;
        #define ALPHA_HASH(pos) (fract(sin(dot(fract((pos) * 0.3183099 + 0.1), vec3(13.8, 41.1, 29.7))) * 43758.55))
        float h_a = ALPHA_HASH(v_world_pos / scale_a);
        float h_b = ALPHA_HASH(v_world_pos / scale_b);
        float threshold = mix(h_a, h_b, lod_fract);
        #undef ALPHA_HASH
        if (albedo_sample.a < threshold) discard;
    }

    vec2  mr_sample = sample_bindless(mat.metallic_roughness_idx, v_uv, vec4(0.0, 1.0, 0.0, 0.0)).gb;
    float roughness = clamp(mr_sample.x * mat.factors.y, 0.04, 1.0);
    float metallic  = clamp(mr_sample.y * mat.factors.x, 0.0,  1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 V = normalize(scene.eye_position.xyz - v_world_pos);
    vec3 Lo = vec3(0.0);
    {
        vec3 L_sun    = normalize(scene.sun_direction.xyz);
        vec3 radiance = scene.sun_color.rgb * scene.sun_color.w;
        radiance *= sample_shadow(v_world_pos, N, L_sun);
        cook_torrance(N, V, L_sun, radiance, albedo, metallic, roughness, F0, Lo);
    }
    int n_lights = scene.light_params.x;
    if (n_lights > 0) {
        // Derive the cluster index for this fragment.
        float near           = scene.cluster_params.x;
        float log_far_near   = scene.cluster_params.z;
        float depth          = near / gl_FragCoord.z; // reverse-Z → positive view-space distance
        uint  slice_x = uint(clamp(gl_FragCoord.x / float(scene.light_params.z) * float(CLUSTER_GRID_X), 0.0, float(CLUSTER_GRID_X - 1)));
        uint  slice_y = uint(clamp(gl_FragCoord.y / float(scene.light_params.w) * float(CLUSTER_GRID_Y), 0.0, float(CLUSTER_GRID_Y - 1)));
        uint  slice_z = uint(clamp(float(CLUSTER_GRID_Z) * log(max(depth, near) / near) / log_far_near, 0.0, float(CLUSTER_GRID_Z - 1)));
        uint  cluster_idx = slice_z * uint(CLUSTER_GRID_X * CLUSTER_GRID_Y) + slice_y * uint(CLUSTER_GRID_X) + slice_x;

        uint n_cluster_lights = cluster_count_ssbo.data[cluster_idx];
        for (uint ci = 0; ci < n_cluster_lights; ++ci) {
            uint li = cluster_index_ssbo.data[cluster_idx * uint(MAX_LIGHTS_PER_CLUSTER) + ci];
            vec3  lpos   = point_light_ssbo.lights[li].position_radius.xyz;
            float radius = point_light_ssbo.lights[li].position_radius.w;
            vec3  lcolor = point_light_ssbo.lights[li].color_intensity.xyz;
            float lint   = point_light_ssbo.lights[li].color_intensity.w;
            vec3  Lv     = lpos - v_world_pos;
            float dist   = length(Lv);
            if (dist < 0.001 || dist > radius) continue;
            vec3  L      = Lv / dist;
            float nd     = dist / radius;
            float window = max(1.0 - nd * nd * nd * nd, 0.0);
            float atten  = (window * window) / max(dist * dist, 0.0001);
            if (point_light_ssbo.lights[li].spot_direction.w > 0.5) {
                vec3  spot_dir  = normalize(point_light_ssbo.lights[li].spot_direction.xyz);
                float cos_theta = dot(-L, spot_dir);
                float cos_inner = point_light_ssbo.lights[li].spot_cone.x;
                float cos_outer = point_light_ssbo.lights[li].spot_cone.y;
                atten *= smoothstep(cos_outer, cos_inner, cos_theta);
                if (atten <= 0.0) continue;
            }
            vec3 radiance = lcolor * lint * atten;
            cook_torrance(N, V, L, radiance, albedo, metallic, roughness, F0, Lo);
        }
    }
    float NdotV = max(dot(N, V), 0.0);
    vec3  irrad = scene.ambient.rgb * scene.ambient.w;
    vec2  dfg     = env_dfg_lazarov(roughness, NdotV);
    float f90_env = clamp(50.0 * F0.g, metallic, 1.0);
    vec3 specular_ambient = (dfg.x * F0 + dfg.y * f90_env) * irrad;
    vec3 F_env   = F_Schlick_roughness(NdotV, F0, roughness);
    vec3 kD_env  = (1.0 - F_env) * (1.0 - metallic);
    vec3 diffuse_ambient = kD_env * albedo * irrad;
    float ao = mat.occlusion_idx >= 0 ? texture(global_textures[nonuniformEXT(mat.occlusion_idx)], v_uv).r : 1.0;
    vec3 emissive = vec3(0.0);
    if (mat.emissive_idx >= 0)
        emissive = texture(global_textures[nonuniformEXT(mat.emissive_idx)], v_uv).rgb * mat.emissive.rgb;
    vec3 color = (diffuse_ambient + specular_ambient) * ao + Lo + emissive;
    color = color * (2.51 * color + 0.03) / (color * (2.43 * color + 0.59) + 0.14);
    color = clamp(color, 0.0, 1.0);
    out_color = vec4(color, albedo_sample.a);
}
