#version 450

// Scene lighting UBO (set 1, binding 0).
// std140 layout — must match SceneLightUBO in VulkanRenderer.cpp exactly.
struct PointLight {
    vec4 position_radius;  // xyz = world position, w = influence radius
    vec4 color_intensity;  // xyz = linear RGB color, w = intensity
    vec4 spot_direction;   // xyz = normalized direction the spot points (unused for point lights), w = unused
    vec4 spot_cone;        // x = cos(inner_angle), y = cos(outer_angle), z = type (0=point, 1=spot), w = unused
};

layout(set = 1, binding = 0) uniform SceneLightUBO {
    vec4  ambient;              // rgb + intensity in w
    vec4  sun_direction;        // xyz = toward light (normalized, world space), w = unused
    vec4  sun_color;            // rgb + intensity in w
    vec4  eye_position;         // xyz = camera world position, w = unused
    ivec4 light_params;         // x = point_light_count, yzw = unused
    PointLight point_lights[8];
} scene;

// PBR material set (set 0) — 5 bindings, all filled per draw group.
layout(set = 0, binding = 0) uniform sampler2D albedo_texture;
layout(set = 0, binding = 1) uniform sampler2D normal_map;
layout(set = 0, binding = 2) uniform sampler2D metallic_roughness_texture;  // G = roughness, B = metallic (glTF packed)
layout(set = 0, binding = 3) uniform sampler2D emissive_texture;
layout(set = 0, binding = 4) uniform sampler2D ao_texture;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_color;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world_pos;
layout(location = 4) in vec3 v_tangent;
layout(location = 5) in vec3 v_bitangent;

// Material push constants (fragment stage, offset 64).
// Mirrors MaterialPushConstants in VulkanRenderer.cpp.
layout(push_constant) uniform MaterialPC {
    layout(offset = 64)  float metallic_factor;
    layout(offset = 68)  float roughness_factor;
    layout(offset = 72)  float emissive_r;
    layout(offset = 76)  float emissive_g;
    layout(offset = 80)  float emissive_b;
    layout(offset = 84)  float alpha_cutoff;
    layout(offset = 88)  int   alpha_mode;      // 0=Opaque, 1=Clip, 2=Blend
    layout(offset = 92)  float base_color_r;    // glTF pbrMetallicRoughness.baseColorFactor
    layout(offset = 96)  float base_color_g;
    layout(offset = 100) float base_color_b;
    layout(offset = 104) float base_color_a;
} mat_pc;

layout(location = 0) out vec4 out_color;

// ============================================================
// PBR helper functions — references:
//   Godot Engine (MIT), drivers/gles3/shaders/scene.glsl
//   Lagarde & de Rousiers, "Moving Frostbite to PBR" (SIGGRAPH 2014)
//   Hammon, "PBR Diffuse Lighting for GGX+Smith" (GDC 2017)
//   Filament, https://google.github.io/filament/Filament.md.html
//   Lazarov, "Getting More Physical in Call of Duty: Black Ops II" (GDC 2013)
// ============================================================

#define M_PI 3.14159265358979

// GGX/Trowbridge-Reitz NDF — numerically stable Frostbite form.
// alpha = perceptual_roughness²  (one squaring; NDF internally squares again).
float D_GGX(float NdotH, float alpha)
{
    float a  = NdotH * alpha;
    float k  = alpha / (1.0 - NdotH * NdotH + a * a);
    return k * k * (1.0 / M_PI);
}

// Smith joint masking-shadowing + denominator combined into one term.
// Returns G_smith / (4 · NdotL · NdotV).
// Hammon 2017 approximation: 0.5 / lerp(2·NdotL·NdotV, NdotL+NdotV, alpha).
// alpha = perceptual_roughness².
float V_GGX(float NdotL, float NdotV, float alpha)
{
    return 0.5 / mix(2.0 * NdotL * NdotV, NdotL + NdotV, alpha);
}

// Schlick Fresnel. cos_theta = dot(H, V).
// f90: Filament's occlusion term clamp(50 · F0.g, 0, 1) keeps metallic-free materials
// from producing a stronger grazing highlight than expected.
vec3 F_Schlick(float cos_theta, vec3 F0, float f90)
{
    float t = 1.0 - cos_theta;
    float t2 = t * t;
    return F0 + (f90 - F0) * (t2 * t2 * t);  // (1 - cos)^5
}

// Roughness-adjusted Schlick Fresnel for the ambient split.
// Prevents ambient specular from going to 1 on rough surfaces.
vec3 F_Schlick_roughness(float cos_theta, vec3 F0, float roughness)
{
    vec3 r1 = max(vec3(1.0 - roughness), F0);
    float t = 1.0 - cos_theta;
    float t2 = t * t;
    return F0 + (r1 - F0) * (t2 * t2 * t);
}

// Lazarov 2013 polynomial — approximates the split-sum DFG BRDF integral.
// Returns vec2(scale, bias) such that:
//   specular_ambient = (scale * F0 + bias * f90) * irradiance
// Avoids a 2D LUT texture lookup entirely.
// Reference: Lazarov, "Getting More Physical in Call of Duty: Black Ops II" (GDC 2013)
vec2 env_dfg_lazarov(float roughness, float NdotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  r    = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// ============================================================
// Cook-Torrance specular BRDF for one light.
// V_GGX already incorporates 1 / (4·NdotL·NdotV), so the
// specular term is just: D · V · F  (without an extra divide).
// Accumulates the full BRDF contribution into Lo.
// ============================================================
void cook_torrance(
    vec3  N, vec3 V, vec3 L,
    vec3  radiance,
    vec3  albedo, float metallic, float roughness, vec3 F0,
    inout vec3 Lo)
{
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0)
        return;

    vec3  H     = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float LdotH = clamp(dot(L, H), 0.0, 1.0);  // HdotV = LdotH by symmetry

    float alpha = roughness * roughness;  // perceptual → linear α

    float D = D_GGX(NdotH, alpha);
    float V_term = V_GGX(NdotL, NdotV, alpha);

    // Filament f90: clamp(50 · F0.g, 0, 1).
    // For typical dielectrics F0 = 0.04 → f90 = 1.0; no visual change there.
    // For exotic low-F0 materials it prevents over-bright grazing highlights.
    float f90 = clamp(50.0 * F0.g, 0.0, 1.0);
    vec3  F   = F_Schlick(LdotH, F0, f90);

    // Specular BRDF: D · V · F   (V already includes the 4·NdotL·NdotV denominator).
    vec3 specular = D * V_term * F;

    // Diffuse: Lambertian.
    // kS = F (specular fraction); kD = (1 - kS) · (1 - metallic).
    // Metallic surfaces have no diffuse; their color is entirely in F0.
    vec3 kD      = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo * (1.0 / M_PI);

    Lo += (diffuse + specular) * radiance * NdotL;
}

// ============================================================
void main()
{
    // --- Tangent-space normal mapping ---
    // When in_tangent == vec4(0) (no tangent data from the mesh), the vertex shader
    // outputs a near-zero v_tangent — fall back to geometric normal.
    vec3 N;
    if (dot(v_tangent, v_tangent) > 0.01) {
        vec3 T  = normalize(v_tangent);
        vec3 B  = normalize(v_bitangent);
        vec3 Ng = normalize(v_normal);
        vec3 nm = texture(normal_map, v_uv).xyz * 2.0 - 1.0;
        N = normalize(T * nm.x + B * nm.y + Ng * nm.z);
    } else {
        N = normalize(v_normal);
    }

    // --- Material parameter fetch ---
    // base_color_factor: glTF pbrMetallicRoughness.baseColorFactor — tints the albedo texture.
    // Defaults to (1,1,1,1) when no material is set, so vertex color still works as before.
    vec4 base_color    = vec4(mat_pc.base_color_r, mat_pc.base_color_g, mat_pc.base_color_b, mat_pc.base_color_a);
    vec4 albedo_sample = texture(albedo_texture, v_uv) * vec4(v_color, 1.0) * base_color;
    vec3 albedo        = albedo_sample.rgb;

    // Alpha clip (Cutout mode): discard fragments below the cutoff threshold.
    // Gives hard-edge transparency for foliage, fences, masked decals.
    // Only active when alpha_mode == 1 (Clip); Opaque and Blend skip this.
    if (mat_pc.alpha_mode == 1 && albedo_sample.a < mat_pc.alpha_cutoff)
        discard;

    // Alpha hashing (mode 3): stochastic per-fragment threshold derived from world position.
    // Avoids the sort required by Blend mode — use for foliage, hair, dense vegetation.
    // Each fragment independently decides to keep or discard based on a spatial hash, so
    // the average coverage over many frames converges to the correct alpha value.
    //
    // Technique:
    //   1. Scale world position by a hash cell size derived from screen-space derivatives
    //      (mipmap-aware): larger cells at distance → smoother LOD transitions.
    //   2. Hash the scaled position to a uniform scalar in [0, 1].
    //   3. Discard if albedo.a < hash — statistically correct transparency without sorting.
    //   4. Blend two cell scales (current and 2× finer) weighted by the fractional LOD level,
    //      following Golus 2018 to reduce structured aliasing at mip transitions.
    //
    // Reference: Golus, "Hashed Alpha Testing" (GDC 2018);
    //            Godot Engine scene_forward_aa_inc.glsl compute_alpha_hash_threshold.
    if (mat_pc.alpha_mode == 3) {
        // Screen-space derivative of world position → cell size proportional to pixel footprint.
        float px_scale = max(length(dFdx(v_world_pos)), length(dFdy(v_world_pos)));
        // Clamp to avoid degenerate values at silhouette edges.
        px_scale = clamp(px_scale, 1e-6, 1.0);

        // log2 of the scale gives the mip level; fractional part blends between two scales.
        float lod        = log2(px_scale);
        float lod_floor  = floor(lod);
        float lod_fract  = lod - lod_floor;
        float scale_a    = exp2(lod_floor);       // coarser cell
        float scale_b    = scale_a * 2.0;         // finer cell (one mip up)

        // 3D spatial hash: fast, low-instruction hash from Golus / Godot.
        // Input: world position scaled to hash cell size.
        // Output: pseudo-random scalar in [0, 1].
        #define ALPHA_HASH(pos) (fract(sin(dot(fract((pos) * 0.3183099 + 0.1), vec3(13.8, 41.1, 29.7))) * 43758.55))

        float h_a = ALPHA_HASH(v_world_pos / scale_a);
        float h_b = ALPHA_HASH(v_world_pos / scale_b);
        // Blend between the two hash values — smooth transition at mip boundaries.
        float threshold = mix(h_a, h_b, lod_fract);

        #undef ALPHA_HASH

        if (albedo_sample.a < threshold)
            discard;
    }

    // glTF metallic-roughness packing: G channel = roughness, B channel = metallic.
    vec2  mr_sample = texture(metallic_roughness_texture, v_uv).gb;
    // Clamp perceptual roughness to ≥ 0.04: keeps GGX NDF finite for near-mirror surfaces.
    float roughness = clamp(mr_sample.x * mat_pc.roughness_factor, 0.04, 1.0);
    float metallic  = clamp(mr_sample.y * mat_pc.metallic_factor,  0.0,  1.0);

    // F0: base specular reflectance at normal incidence.
    // Dielectrics: 0.04 (water/plastic baseline). Metals: their albedo color.
    // Godot uses 0.16 * specular² where specular defaults to 0.5 → 0.04.
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 V = normalize(scene.eye_position.xyz - v_world_pos);

    // --- Reflectance equation: accumulate outgoing radiance ---
    vec3 Lo = vec3(0.0);

    // Directional sun.
    {
        vec3 L_sun    = normalize(scene.sun_direction.xyz);
        vec3 radiance = scene.sun_color.rgb * scene.sun_color.w;
        cook_torrance(N, V, L_sun, radiance, albedo, metallic, roughness, F0, Lo);
    }

    // Point and spot lights.
    int n_lights = scene.light_params.x;
    for (int i = 0; i < n_lights; ++i) {
        vec3  lpos   = scene.point_lights[i].position_radius.xyz;
        float radius = scene.point_lights[i].position_radius.w;
        vec3  lcolor = scene.point_lights[i].color_intensity.xyz;
        float lint   = scene.point_lights[i].color_intensity.w;

        vec3  Lv   = lpos - v_world_pos;
        float dist = length(Lv);
        if (dist < 0.001 || dist > radius)
            continue;
        vec3 L = Lv / dist;

        // Physically-based attenuation: (1 - (d/r)^4)^2 · d^(-2).
        // The (1-(d/r)^4)^2 window clamps to zero at the radius boundary.
        // The d^(-2) term gives inverse-square falloff inside the radius.
        // Reference: Lagarde & de Rousiers, "Moving Frostbite to PBR" (SIGGRAPH 2014);
        //            Godot Engine, scene_forward_lights_inc.glsl get_omni_attenuation().
        float nd     = dist / radius;
        float window = max(1.0 - nd * nd * nd * nd, 0.0);
        float atten  = (window * window) / max(dist * dist, 0.0001);

        // Spot cone attenuation.
        if (scene.point_lights[i].spot_cone.z > 0.5) {
            vec3  spot_dir  = normalize(scene.point_lights[i].spot_direction.xyz);
            float cos_theta = dot(-L, spot_dir);
            float cos_inner = scene.point_lights[i].spot_cone.x;
            float cos_outer = scene.point_lights[i].spot_cone.y;
            atten *= smoothstep(cos_outer, cos_inner, cos_theta);
            if (atten <= 0.0)
                continue;
        }

        vec3 radiance = lcolor * lint * atten;
        cook_torrance(N, V, L, radiance, albedo, metallic, roughness, F0, Lo);
    }

    // --- Ambient: diffuse + specular via Lazarov DFG polynomial ---
    // Full split-sum IBL (prefiltered env map + BRDF LUT) is deferred to Milestone 2.8.
    // For now the scene ambient color acts as a flat, uniform irradiance proxy.
    float NdotV = max(dot(N, V), 0.0);
    vec3  irrad = scene.ambient.rgb * scene.ambient.w;

    // Lazarov polynomial: approximates the BRDF DFG lookup table without needing
    // a precomputed texture. dfg.x = specular scale, dfg.y = specular bias.
    vec2  dfg     = env_dfg_lazarov(roughness, NdotV);
    // Godot f90 lower-bound: clamp(50·F0.g, metallic, 1). Prevents metallic surfaces
    // with low-luminance F0 from having an incorrectly weak grazing specular.
    float f90_env = clamp(50.0 * F0.g, metallic, 1.0);

    // Specular ambient: Lazarov DFG split applied to flat irradiance.
    // In 2.8 this irrad term is replaced with textureLod(prefiltered_env, R, roughness_lod).
    vec3 specular_ambient = (dfg.x * F0 + dfg.y * f90_env) * irrad;

    // Diffuse ambient: roughness-adjusted Fresnel kD prevents double-counting
    // energy that already went to the specular term.
    vec3 F_env   = F_Schlick_roughness(NdotV, F0, roughness);
    vec3 kD_env  = (1.0 - F_env) * (1.0 - metallic);
    vec3 diffuse_ambient = kD_env * albedo * irrad;

    // Energy compensation (multi-scatter correction) — TODO Milestone 2.8.
    // Requires BRDF LUT: energy_comp = 1 + F0 * (1/dfg.y - 1).
    // Without it rough metallic surfaces lose ~10-20% brightness per bounce;
    // acceptable until the full IBL pass is in place.

    // --- Ambient occlusion ---
    // AO texture R channel multiplies the ambient (diffuse + specular) terms only —
    // never the direct lights, which have their own shadowing geometry.
    float ao = texture(ao_texture, v_uv).r;

    // --- Emissive ---
    // Sampled and multiplied by emissive_factor; added after all lighting so it glows
    // at full brightness regardless of scene lights or occlusion.
    vec3 emissive_factor = vec3(mat_pc.emissive_r, mat_pc.emissive_g, mat_pc.emissive_b);
    vec3 emissive = texture(emissive_texture, v_uv).rgb * emissive_factor;

    vec3 color = (diffuse_ambient + specular_ambient) * ao + Lo + emissive;

    // --- Tone mapping: ACES filmic ---
    // Maps unbounded HDR linear color to [0, 1] display range.
    // Prevents bright surfaces from clipping to solid white and gives a natural
    // film-like shoulder. Constants from Krzysztof Narkowicz's ACES approximation.
    // Reference: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
    color = color * (2.51 * color + 0.03) / (color * (2.43 * color + 0.59) + 0.14);
    color = clamp(color, 0.0, 1.0);

    out_color = vec4(color, albedo_sample.a);
}
