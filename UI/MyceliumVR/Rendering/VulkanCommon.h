/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Array.h>
#include <AK/Error.h>
#include <AK/Format.h>
#include <AK/StringView.h>
#include <AK/Types.h>

#if defined(USE_VULKAN)
#    include <vulkan/vulkan.h>
#    include <vk_mem_alloc.h>
#endif

namespace Gfx {
class Bitmap;
}

namespace MyceliumVR {

#if defined(USE_VULKAN)

struct Vec3 {
    float x { 0.0f };
    float y { 0.0f };
    float z { 0.0f };
};

static inline Vec3 rotate_by_quaternion(Vec3 point, float qx, float qy, float qz, float qw)
{
    auto tx = 2.0f * (qy * point.z - qz * point.y);
    auto ty = 2.0f * (qz * point.x - qx * point.z);
    auto tz = 2.0f * (qx * point.y - qy * point.x);
    return {
        point.x + qw * tx + (qy * tz - qz * ty),
        point.y + qw * ty + (qz * tx - qx * tz),
        point.z + qw * tz + (qx * ty - qy * tx),
    };
}

static inline Vec3 subtract(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static inline Vec3 normalize(Vec3 value) {
    auto ls = dot(value, value);
    if (ls <= 0.000001f) return { 0.0f, 0.0f, 0.0f };
    auto il = 1.0f / __builtin_sqrtf(ls);
    return { value.x * il, value.y * il, value.z * il };
}

struct Vertex {
    float position[3];
    float normal[3];
    float color[3];
    float uv[2];
    float tangent[4];
};

struct TexturedVertex {
    float position[3];
    float uv[2];
};

struct Mat4 {
    float elements[16] {};
};

struct PushConstants {
    float view_projection[16] {}; // 64 bytes
};

struct GPUMaterialData {
    float metallic_factor { 0.0f };
    float roughness_factor { 0.5f };
    float alpha_cutoff { 0.5f };
    int32_t alpha_mode { 0 };       // 0=Opaque, 1=Clip, 2=Blend, 3=Hash
    float emissive_factor[4] {};    // RGB + padding
    float base_color_factor[4] { 1.0f, 1.0f, 1.0f, 1.0f };
    
    // Bindless texture indices (-1 = use fallback)
    int32_t albedo_idx { -1 };
    int32_t normal_idx { -1 };
    int32_t metallic_roughness_idx { -1 };
    int32_t emissive_idx { -1 };
    int32_t occlusion_idx { -1 };
    int32_t padding[3] {};
};
static_assert(sizeof(GPUMaterialData) == 80);

struct GPUInstanceData {
    float world_matrix[16];
    u32 material_index;
    u32 padding[3];
};
static_assert(sizeof(GPUInstanceData) == 80);

using ShadowQuality = i32;
static constexpr ShadowQuality min_shadow_quality = 0;
static constexpr ShadowQuality max_shadow_quality = 9;
static constexpr ShadowQuality default_shadow_quality = 9;

struct GroupAABBGPU {
    float min_xyz[4];
    float max_xyz[4];
};
static_assert(sizeof(GroupAABBGPU) == 32);

static constexpr int ClusterGridX = 16;
static constexpr int ClusterGridY = 9;
static constexpr int ClusterGridZ = 24;
static constexpr int ClusterTotal = ClusterGridX * ClusterGridY * ClusterGridZ; // 3456
static constexpr int MaxLightsPerCluster = 32;
static constexpr int MaxPointLights = 64;

struct GPUPointLight {
    float position_radius[4] {};
    float color_intensity[4] {};
    float spot_direction[4] {};
    float spot_cone[4] {};
};
static_assert(sizeof(GPUPointLight) == 64);

struct SceneLightUBO {
    float ambient[4] {};
    float sun_direction[4] {};
    float sun_color[4] {};
    float eye_position[4] {};
    int32_t light_params[4] {}; // x=n_lights, y=shadow_quality, z=screen_w, w=screen_h
    float shadow_view_projection[16] {};
    float shadow_params[4] {};
    float cluster_params[4] {}; // x=near, y=far, z=log(far/near), w=unused
};

static inline Mat4 make_identity_matrix() {
    Mat4 m {}; m.elements[0] = 1.0f; m.elements[5] = 1.0f; m.elements[10] = 1.0f; m.elements[15] = 1.0f;
    return m;
}

static inline Mat4 multiply(Mat4 const& l, Mat4 const& r) {
    Mat4 res {};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            res.elements[i * 4 + j] = l.elements[0 * 4 + j] * r.elements[i * 4 + 0] + l.elements[1 * 4 + j] * r.elements[i * 4 + 1] + l.elements[2 * 4 + j] * r.elements[i * 4 + 2] + l.elements[3 * 4 + j] * r.elements[i * 4 + 3];
        }
    }
    return res;
}

static inline Mat4 make_perspective_matrix(float fov, float ar, float n, float /*f*/) {
    auto fl = 1.0f / __builtin_tanf(fov * 0.5f);
    Mat4 m {}; m.elements[0] = fl / ar; m.elements[5] = -fl; m.elements[10] = 0.0f; m.elements[11] = -1.0f; m.elements[14] = n;
    return m;
}

static inline Mat4 make_orthographic_matrix(float left, float right, float bottom, float top, float near_plane, float far_plane)
{
    Mat4 m {};
    m.elements[0] = 2.0f / (right - left);
    m.elements[5] = 2.0f / (top - bottom);
    m.elements[10] = -1.0f / (far_plane - near_plane);
    m.elements[12] = -(right + left) / (right - left);
    m.elements[13] = -(top + bottom) / (top - bottom);
    m.elements[14] = -near_plane / (far_plane - near_plane);
    m.elements[15] = 1.0f;
    return m;
}

static inline Mat4 make_look_at_matrix(Vec3 eye, Vec3 center, Vec3 up) {
    auto f = normalize(subtract(center, eye));
    auto s = normalize(cross(f, up));
    auto u = cross(s, f);
    auto m = make_identity_matrix();
    m.elements[0] = s.x; m.elements[1] = u.x; m.elements[2] = -f.x;
    m.elements[4] = s.y; m.elements[5] = u.y; m.elements[6] = -f.y;
    m.elements[8] = s.z; m.elements[9] = u.z; m.elements[10] = -f.z;
    m.elements[12] = -dot(s, eye); m.elements[13] = -dot(u, eye); m.elements[14] = dot(f, eye);
    return m;
}

static inline ErrorOr<void> check_vulkan_result(VkResult result, StringView message) {
    if (result != VK_SUCCESS) {
        warnln("{}: VkResult {}", message, to_underlying(result));
        return Error::from_string_view(message);
    }
    return {};
}

static inline VkFormat find_depth_format(VkPhysicalDevice pd) {
    Array<VkFormat, 3> cs { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM };
    for (auto f : cs) {
        VkFormatProperties p {}; vkGetPhysicalDeviceFormatProperties(pd, f, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return f;
    }
    return VK_FORMAT_D32_SFLOAT;
}

struct CameraState {
    float position[3] { 0.0f, 0.0f, 6.0f };
    float yaw_degrees { 0.0f };
    float pitch_degrees { 0.0f };
};

struct SceneLightData {
    float ambient_rgb[3] { 0.15f, 0.18f, 0.25f };
    float ambient_intensity { 1.0f };
    float light_to_xyz[3] { 0.408f, 0.816f, 0.408f };
    float light_intensity { 1.0f };
    float light_rgb[3] { 1.0f, 0.93f, 0.80f };

    struct PointLight {
        float position[3] {};
        float radius { 5.0f };
        float color[3] { 1.0f, 1.0f, 1.0f };
        float intensity { 1.0f };
        float direction[3] {};
        float unused0 { 0.0f };
        float cone_inner_cos { 1.0f };
        float cone_outer_cos { 1.0f };
        int   type { 0 };
        float unused1 { 0.0f };
    };
    PointLight point_lights[MaxPointLights] {}; // MaxPointLights = 64 (global constant)
    int point_light_count { 0 };
};

static inline Mat4 make_view_matrix(CameraState const& camera_state)
{
    auto yaw_radians   = camera_state.yaw_degrees   * (3.14159265f / 180.0f);
    auto pitch_radians = camera_state.pitch_degrees * (3.14159265f / 180.0f);
    auto forward = normalize(Vec3 {
        __builtin_cosf(pitch_radians) * __builtin_sinf(yaw_radians),
        __builtin_sinf(pitch_radians),
        -__builtin_cosf(pitch_radians) * __builtin_cosf(yaw_radians),
    });
    auto eye    = Vec3 { camera_state.position[0], camera_state.position[1], camera_state.position[2] };
    auto center = Vec3 { eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
    return make_look_at_matrix(eye, center, Vec3 { 0.0f, 1.0f, 0.0f });
}

static inline PushConstants make_view_projection_constants(VkExtent2D extent, CameraState const& camera_state)
{
    auto safe_height = extent.height > 0 ? extent.height : 1u;
    auto aspect_ratio = static_cast<float>(extent.width) / static_cast<float>(safe_height);
    auto projection = make_perspective_matrix(1.0471976f, aspect_ratio, 0.1f, 100.0f);
    auto yaw_radians = camera_state.yaw_degrees * (3.14159265f / 180.0f);
    auto pitch_radians = camera_state.pitch_degrees * (3.14159265f / 180.0f);
    auto forward = normalize(Vec3 {
        __builtin_cosf(pitch_radians) * __builtin_sinf(yaw_radians),
        __builtin_sinf(pitch_radians),
        -__builtin_cosf(pitch_radians) * __builtin_cosf(yaw_radians),
    });
    auto eye = Vec3 { camera_state.position[0], camera_state.position[1], camera_state.position[2] };
    auto center = Vec3 { eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
    auto view_mat = make_look_at_matrix(eye, center, Vec3 { 0.0f, 1.0f, 0.0f });
    auto view_projection = multiply(projection, view_mat);

    PushConstants constants {};
    for (size_t i = 0; i < 16; ++i)
        constants.view_projection[i] = view_projection.elements[i];
    return constants;
}

#endif

}
