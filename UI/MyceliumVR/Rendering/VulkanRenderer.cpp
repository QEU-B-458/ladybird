/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanRenderer.h"

#include "../Support/MaterialLibrary.h"
#include "../Support/MeshLibrary.h"
#include "../Support/ShaderCompiler.h"
#include "../Support/VirtualFileSystem.h"
#include "TextureLibrary.h"

#include <AK/Array.h>
#include <AK/ByteString.h>
#include <AK/Format.h>
#include <AK/InsertionSort.h>
#include <AK/Math.h>
#include <AK/Vector.h>
#include <LibGfx/Bitmap.h>
#include <math.h>
#include <SDL3/SDL_vulkan.h>

#if defined(USE_VULKAN)
#    include <vulkan/vulkan.h>
#endif

#if !defined(MYCELIUMVR_SHADER_DIRECTORY)
#    define MYCELIUMVR_SHADER_DIRECTORY "."
#endif

#if !defined(MYCELIUMVR_SHADER_SOURCE_DIRECTORY)
#    define MYCELIUMVR_SHADER_SOURCE_DIRECTORY "."
#endif

namespace MyceliumVR {

#if defined(USE_VULKAN)
struct DrawGroup {
    String material;
    String normal_map;  // virtual path; empty = use flat normal fallback
    u32 first_vertex { 0 };
    u32 vertex_count { 0 };
    u32 first_instance { 0 };
    u32 instance_count { 0 };
    float centroid_z { 0.0f }; // average world-Z of mesh vertices; used for blend back-to-front sort
    // Local-space mesh AABB (set at group creation time from MeshAsset::aabb_min/max).
    // Used when expanding the world AABB as new instances are added to the group.
    float aabb_local_min[3] { 0.0f, 0.0f, 0.0f };
    float aabb_local_max[3] { 0.0f, 0.0f, 0.0f };
    // World-space AABB: union of all instance-transformed local AABBs.
    // Uploaded to aabb_world_buffer and tested by the frustum culling compute shader.
    float aabb_world_min[3] { 1e30f, 1e30f, 1e30f };   // start at +∞ (no instances yet)
    float aabb_world_max[3] { -1e30f, -1e30f, -1e30f }; // start at -∞
    // Entity IDs for each instance in draw order — used by rebuild_transforms_only.
    Vector<EntityId> instance_entity_ids;
};

struct VulkanRenderer::Impl {
    struct BufferResource {
        VkBuffer buffer { VK_NULL_HANDLE };
        VkDeviceMemory memory { VK_NULL_HANDLE };
        VkDeviceSize capacity { 0 };
    };

    struct TextureResource {
        VkImage image { VK_NULL_HANDLE };
        VkDeviceMemory image_memory { VK_NULL_HANDLE };
        VkImageView image_view { VK_NULL_HANDLE };
        VkSampler sampler { VK_NULL_HANDLE };
        VkDescriptorPool descriptor_pool { VK_NULL_HANDLE };
        VkDescriptorSet descriptor_set { VK_NULL_HANDLE };
        u32 width { 0 };
        u32 height { 0 };
        bool has_uploaded_contents { false };
    };

    VkInstance instance { VK_NULL_HANDLE };
    VkSurfaceKHR surface { VK_NULL_HANDLE };
    VkPhysicalDevice physical_device { VK_NULL_HANDLE };
    VkDevice device { VK_NULL_HANDLE };
    VkQueue graphics_queue { VK_NULL_HANDLE };
    u32 graphics_queue_family { 0 };
    ShaderBytecode vertex_shader_bytecode;
    ShaderBytecode fragment_shader_bytecode;
    Optional<ShaderBytecode> panel_vertex_shader_bytecode;
    Optional<ShaderBytecode> panel_fragment_shader_bytecode;
    bool shader_bytecode_loaded { false };
    bool swapchain_dirty { true };
    u32 drawable_width { 1280 };
    u32 drawable_height { 720 };
    VkSurfaceFormatKHR surface_format {};
    VkExtent2D extent {};
    VkSwapchainKHR swapchain { VK_NULL_HANDLE };
    Vector<VkImage> swapchain_images;
    Vector<VkImageView> swapchain_image_views;
    VkImage depth_image { VK_NULL_HANDLE };
    VkDeviceMemory depth_image_memory { VK_NULL_HANDLE };
    VkImageView depth_image_view { VK_NULL_HANDLE };
    VkFormat depth_format { VK_FORMAT_D32_SFLOAT };
    VkRenderPass render_pass { VK_NULL_HANDLE };
    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    VkPipeline pipeline { VK_NULL_HANDLE };        // opaque + clip draws
    VkPipeline blend_pipeline { VK_NULL_HANDLE };  // alpha-blend draws (depth write off, SRC_ALPHA blending)
    VkDescriptorSetLayout world_descriptor_set_layout { VK_NULL_HANDLE }; // 1-binding layout for TextureLibrary internal use
    VkDescriptorSetLayout pbr_descriptor_set_layout { VK_NULL_HANDLE };   // set=0: 5-binding PBR material layout
    VkDescriptorPool pbr_descriptor_pool { VK_NULL_HANDLE };              // shared pool for per-group PBR descriptor sets
    Vector<VkDescriptorSet> pbr_descriptor_sets;                          // index 0 = probe fallback, 1..N = per draw group
    TextureResource flat_normal_texture;
    TextureResource fallback_black_texture;
    VkDescriptorSetLayout light_ubo_descriptor_set_layout { VK_NULL_HANDLE };
    VkDescriptorPool light_ubo_descriptor_pool { VK_NULL_HANDLE };
    VkDescriptorSet light_ubo_descriptor_set { VK_NULL_HANDLE };
    BufferResource light_ubo_buffer;
    TextureResource albedo_texture;
    VkDescriptorSetLayout panel_descriptor_set_layout { VK_NULL_HANDLE };
    VkPipelineLayout panel_pipeline_layout { VK_NULL_HANDLE };
    VkPipeline panel_pipeline { VK_NULL_HANDLE };
    Vector<VkFramebuffer> framebuffers;
    VkCommandPool command_pool { VK_NULL_HANDLE };
    VkCommandBuffer command_buffer { VK_NULL_HANDLE };
    VkSemaphore image_available { VK_NULL_HANDLE };
    VkSemaphore render_finished { VK_NULL_HANDLE };
    VkFence in_flight { VK_NULL_HANDLE };
    BufferResource world_vertex_buffer;
    BufferResource panel_vertex_buffer;
    BufferResource overlay_vertex_buffer;
    u32 world_vertex_count { 0 };
    u32 panel_vertex_count { 0 };
    u32 overlay_vertex_count { 0 };
    TextureResource panel_texture;
    TextureResource overlay_texture;
    bool panel_texture_dirty { false };
    bool overlay_texture_dirty { false };
    BitmapView panel_bitmap_view;
    BitmapView overlay_bitmap_view;
    Vector<u8> panel_pixels;
    u32 panel_width { 0 };
    u32 panel_height { 0 };
    OverlayView overlay_view;
    CameraState camera_state;
    SceneLightData scene_light;
    MaterialLibrary material_library;
    Optional<MeshLibrary> mesh_library;
    Optional<TextureLibrary> texture_library; // loads/caches textures for PBR slots and backward-compat albedo
    Vector<DrawGroup> world_draw_groups;
    BufferResource world_instance_buffer;
    u32 world_instance_count { 0 };
    // One VkDrawIndirectCommand per draw group, indexed by group index.
    // indirect_ref_buffer: CPU-uploaded reference (never modified by GPU); compute shader reads from here.
    // indirect_draw_buffer: GPU compute writes here each frame (instanceCount=0 for culled groups);
    //                        vkCmdDrawIndirect reads from here.
    BufferResource indirect_ref_buffer;
    BufferResource indirect_draw_buffer;
    u32 indirect_draw_count { 0 };
    // Per-group world-space AABBs (GroupAABB = two vec4s, 32 bytes each).
    // CPU-uploaded when geometry changes; read by the frustum culling compute shader.
    BufferResource aabb_world_buffer;
    // Frustum culling compute pipeline.
    ShaderBytecode cull_shader_bytecode;
    VkPipeline cull_pipeline { VK_NULL_HANDLE };
    VkPipelineLayout cull_pipeline_layout { VK_NULL_HANDLE };
    VkDescriptorSetLayout cull_descriptor_set_layout { VK_NULL_HANDLE };
    VkDescriptorPool cull_descriptor_pool { VK_NULL_HANDLE };
    VkDescriptorSet cull_descriptor_set { VK_NULL_HANDLE };
    VirtualFileSystem const* file_system { nullptr };
};

struct Vec3 {
    float x { 0.0f };
    float y { 0.0f };
    float z { 0.0f };
};

struct Vertex {
    float position[3];
    float normal[3];
    float color[3];
    float uv[2];
    float tangent[4]; // xyz = tangent direction, w = bitangent sign; all zero = no tangent data
};

struct TexturedVertex {
    float position[3];
    float uv[2];
};

struct Mat4 {
    float elements[16] {};
};

// Push constants: VP matrix only (vertex stage, 64 bytes). Used for both world and panel pipelines.
struct PushConstants {
    float view_projection[16] {};
};

// Material push constants (fragment stage, offset 64, 44 bytes).
// Passed once per draw group immediately before vkCmdDraw.
// Total push constant range: 64 (VP matrix) + 44 (material) = 108 bytes (within 128-byte minimum).
//
// Default values intentionally match a plain dielectric (non-metallic) surface so that
// cubes and other geometry with no material assigned look correct. glTF materials that
// specify metallic_factor will override these via the per-group push.
// The metallic_roughness fallback texture is white (R=G=B=1). That gives roughness=1*factor
// and metallic=1*factor from its G/B channels — so factor defaults must be non-metallic.
struct MaterialPushConstants {
    float metallic_factor    { 0.0f };  // 0 = dielectric; 1 = fully metallic
    float roughness_factor   { 0.5f };  // 0.5 = medium roughness for plain geometry
    float emissive_factor[3] { 0.0f, 0.0f, 0.0f };
    float alpha_cutoff       { 0.5f };
    int   alpha_mode         { 0 };   // 0=Opaque, 1=Clip, 2=Blend
    float base_color_factor[4] { 1.0f, 1.0f, 1.0f, 1.0f }; // glTF pbrMetallicRoughness.baseColorFactor
};
static_assert(sizeof(MaterialPushConstants) == 44);

// Scene lighting UBO uploaded to descriptor set 1 each frame.
// std140 layout — must match the uniform block in WorldMesh.frag exactly.
// Total size: 5 × vec4 (80 bytes) + 8 × PointLightGPU (256 bytes) = 336 bytes.
struct SceneLightUBO {
    float ambient[4] {};           // rgb + intensity in [3]
    float sun_direction[4] {};     // xyz normalized toward light, [3] unused
    float sun_color[4] {};         // rgb + intensity in [3]
    float eye_position[4] {};      // xyz camera world position, [3] unused
    int32_t light_params[4] {};    // [0] = point_light_count, [1-3] unused
    struct PointLightGPU {
        float position_radius[4] {}; // xyz position, [3] = radius
        float color_intensity[4] {}; // rgb color, [3] = intensity
        float spot_direction[4] {}; // xyz direction (normalized), [3] = unused
        float spot_cone[4] {};      // [0]=cos(inner), [1]=cos(outer), [2]=type(0=point,1=spot), [3]=unused
    } point_lights[8] {};
};

static Vec3 rotate_by_quaternion(Vec3 point, float qx, float qy, float qz, float qw)
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

static Vec3 subtract(Vec3 a, Vec3 b)
{
    return {
        a.x - b.x,
        a.y - b.y,
        a.z - b.z,
    };
}

static float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

static Vec3 normalize(Vec3 value)
{
    auto length_squared = dot(value, value);
    if (length_squared <= 0.000001f)
        return { 0.0f, 0.0f, 0.0f };
    auto inverse_length = 1.0f / __builtin_sqrtf(length_squared);
    return {
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
    };
}

static VkFormat find_depth_format(VkPhysicalDevice physical_device)
{
    // Prefer 32-bit float depth; fall back to packed 24-bit or 16-bit.
    Array<VkFormat, 3> candidates { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM };
    for (auto format : candidates) {
        VkFormatProperties props {};
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return format;
    }
    return VK_FORMAT_D32_SFLOAT; // assume supported — virtually universal on modern hardware
}

static Mat4 make_identity_matrix()
{
    Mat4 matrix {};
    matrix.elements[0] = 1.0f;
    matrix.elements[5] = 1.0f;
    matrix.elements[10] = 1.0f;
    matrix.elements[15] = 1.0f;
    return matrix;
}

static Mat4 multiply(Mat4 const& left, Mat4 const& right)
{
    Mat4 result {};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result.elements[column * 4 + row]
                = left.elements[0 * 4 + row] * right.elements[column * 4 + 0]
                + left.elements[1 * 4 + row] * right.elements[column * 4 + 1]
                + left.elements[2 * 4 + row] * right.elements[column * 4 + 2]
                + left.elements[3 * 4 + row] * right.elements[column * 4 + 3];
        }
    }
    return result;
}

// Reversed-Z infinite projection (near→1, far→0).
// Gives uniform depth precision across the full depth range — no far-plane clipping.
// Requires: depthCompareOp = GREATER, depth clear = 0.0.
static Mat4 make_perspective_matrix(float vertical_field_of_view_radians, float aspect_ratio, float near_plane, float /*far_plane*/)
{
    auto focal_length = 1.0f / __builtin_tanf(vertical_field_of_view_radians * 0.5f);
    Mat4 matrix {};
    matrix.elements[0]  = focal_length / aspect_ratio;
    matrix.elements[5]  = -focal_length;   // Flip Y for Vulkan NDC
    matrix.elements[10] = 0.0f;            // Reversed-Z: maps far→0
    matrix.elements[11] = -1.0f;
    matrix.elements[14] = near_plane;      // Reversed-Z: maps near→1
    return matrix;
}

static Mat4 make_look_at_matrix(Vec3 eye, Vec3 center, Vec3 up)
{
    auto forward = normalize(subtract(center, eye));
    auto side = normalize(cross(forward, up));
    auto adjusted_up = cross(side, forward);

    auto matrix = make_identity_matrix();
    matrix.elements[0] = side.x;
    matrix.elements[1] = adjusted_up.x;
    matrix.elements[2] = -forward.x;
    matrix.elements[4] = side.y;
    matrix.elements[5] = adjusted_up.y;
    matrix.elements[6] = -forward.y;
    matrix.elements[8] = side.z;
    matrix.elements[9] = adjusted_up.z;
    matrix.elements[10] = -forward.z;
    matrix.elements[12] = -dot(side, eye);
    matrix.elements[13] = -dot(adjusted_up, eye);
    matrix.elements[14] = dot(forward, eye);
    return matrix;
}

// Returns a column-major TRS model matrix as 16 floats, matching the GLSL mat4 instance layout.
static Array<float, 16> make_trs_matrix(Transform const& t)
{
    float qx = t.rotation[0], qy = t.rotation[1], qz = t.rotation[2], qw = t.rotation[3];
    float sx = t.scale[0], sy = t.scale[1], sz = t.scale[2];
    float tx = t.position[0], ty = t.position[1], tz = t.position[2];
    // Rotation matrix from quaternion.
    float r00 = 1.0f - 2.0f * (qy * qy + qz * qz);
    float r10 = 2.0f * (qx * qy + qz * qw);
    float r20 = 2.0f * (qx * qz - qy * qw);
    float r01 = 2.0f * (qx * qy - qz * qw);
    float r11 = 1.0f - 2.0f * (qx * qx + qz * qz);
    float r21 = 2.0f * (qy * qz + qx * qw);
    float r02 = 2.0f * (qx * qz + qy * qw);
    float r12 = 2.0f * (qy * qz - qx * qw);
    float r22 = 1.0f - 2.0f * (qx * qx + qy * qy);
    // Column-major mat4: elements[col * 4 + row].
    Array<float, 16> m {};
    m[0]  = sx * r00;  m[1]  = sx * r10;  m[2]  = sx * r20;  m[3]  = 0.0f;
    m[4]  = sy * r01;  m[5]  = sy * r11;  m[6]  = sy * r21;  m[7]  = 0.0f;
    m[8]  = sz * r02;  m[9]  = sz * r12;  m[10] = sz * r22;  m[11] = 0.0f;
    m[12] = tx;        m[13] = ty;        m[14] = tz;        m[15] = 1.0f;
    return m;
}

static PushConstants make_view_projection_constants(VkExtent2D extent, VulkanRenderer::CameraState const& camera_state)
{
    auto safe_height = extent.height > 0 ? extent.height : 1u;
    auto aspect_ratio = static_cast<float>(extent.width) / static_cast<float>(safe_height);
    auto projection = make_perspective_matrix(1.0471976f, aspect_ratio, 0.1f, 100.0f);
    auto yaw_radians = camera_state.yaw_degrees * (AK::Pi<float> / 180.0f);
    auto pitch_radians = camera_state.pitch_degrees * (AK::Pi<float> / 180.0f);
    auto forward = normalize(Vec3 {
        __builtin_cosf(pitch_radians) * __builtin_sinf(yaw_radians),
        __builtin_sinf(pitch_radians),
        -__builtin_cosf(pitch_radians) * __builtin_cosf(yaw_radians),
    });
    auto eye = Vec3 { camera_state.position[0], camera_state.position[1], camera_state.position[2] };
    auto center = Vec3 { eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
    auto view = make_look_at_matrix(eye, center, Vec3 { 0.0f, 1.0f, 0.0f });
    auto view_projection = multiply(projection, view);

    PushConstants constants {};
    for (size_t i = 0; i < 16; ++i)
        constants.view_projection[i] = view_projection.elements[i];
    return constants;
}

static PushConstants make_identity_constants()
{
    auto identity = make_identity_matrix();
    PushConstants constants {};
    for (size_t i = 0; i < 16; ++i)
        constants.view_projection[i] = identity.elements[i];
    return constants;
}


// Expands group.aabb_world_min/max to include all 8 corners of the local AABB
// after applying the given entity TRS transform.
static void expand_group_aabb(DrawGroup& group, Transform const& transform)
{
    float const* bmin = group.aabb_local_min;
    float const* bmax = group.aabb_local_max;
    for (int mask = 0; mask < 8; ++mask) {
        Vec3 corner {
            (mask & 1) ? bmax[0] : bmin[0],
            (mask & 2) ? bmax[1] : bmin[1],
            (mask & 4) ? bmax[2] : bmin[2],
        };
        corner.x *= transform.scale[0];
        corner.y *= transform.scale[1];
        corner.z *= transform.scale[2];
        corner = rotate_by_quaternion(corner,
            transform.rotation[0], transform.rotation[1],
            transform.rotation[2], transform.rotation[3]);
        corner.x += transform.position[0];
        corner.y += transform.position[1];
        corner.z += transform.position[2];
        if (corner.x < group.aabb_world_min[0]) group.aabb_world_min[0] = corner.x;
        if (corner.y < group.aabb_world_min[1]) group.aabb_world_min[1] = corner.y;
        if (corner.z < group.aabb_world_min[2]) group.aabb_world_min[2] = corner.z;
        if (corner.x > group.aabb_world_max[0]) group.aabb_world_max[0] = corner.x;
        if (corner.y > group.aabb_world_max[1]) group.aabb_world_max[1] = corner.y;
        if (corner.z > group.aabb_world_max[2]) group.aabb_world_max[2] = corner.z;
    }
}

// Rebuild only instance matrices and world AABBs from current entity transforms.
// The vertex buffer and draw groups are reused unchanged — only call when layout_dirty is false.
static void rebuild_transforms_only(World const& world, Vector<DrawGroup>& groups, Vector<float>& out_instance_data)
{
    out_instance_data.clear();
    for (auto& group : groups) {
        // Reset world AABB for this group.
        group.aabb_world_min[0] = group.aabb_world_min[1] = group.aabb_world_min[2] = 1e30f;
        group.aabb_world_max[0] = group.aabb_world_max[1] = group.aabb_world_max[2] = -1e30f;

        group.first_instance = static_cast<u32>(out_instance_data.size()) / 16;
        for (auto entity_id : group.instance_entity_ids) {
            auto const* entity = world.entity(entity_id);
            if (!entity || !entity->alive) {
                // Entity was destroyed — push identity so instance count stays consistent.
                static constexpr Array<float, 16> identity {
                    1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
                };
                for (auto f : identity)
                    out_instance_data.append(f);
                continue;
            }
            for (auto f : make_trs_matrix(entity->transform))
                out_instance_data.append(f);
            expand_group_aabb(group, entity->transform);
        }
    }
}

static Array<float, 3> color_for_material(String const& material)
{
    if (material == "accent"_string)
        return { 1.0f, 0.73f, 0.29f };
    if (material == "example-accent"_string)
        return { 0.31f, 0.86f, 1.0f };
    return { 1.0f, 1.0f, 1.0f };
}

static Vector<Vertex> build_world_vertices(World const& world, MeshLibrary& mesh_library, Vector<DrawGroup>& out_ranges)
{
    Vector<Vertex> vertices;
    for (auto const& entity : world.entities()) {
        if (!entity.alive)
            continue;
        if (entity.panel.has_value())
            continue;

        auto color = color_for_material(entity.mesh_renderer.material);
        auto mesh_result = mesh_library.resolve_mesh(entity.mesh_renderer.mesh);
        if (mesh_result.is_error()) {
            warnln("Failed to resolve mesh '{}': {}", entity.mesh_renderer.mesh, mesh_result.error());
            mesh_result = mesh_library.resolve_mesh("cube"_string);
            if (mesh_result.is_error())
                continue;
        }
        auto const* mesh_asset = mesh_result.release_value();
        auto const& positions = mesh_asset->positions;
        bool const has_smooth_normals = mesh_asset->normals.size() == positions.size();

        // positions is a flat xyz array already expanded by index in MeshLibrary.
        // Every 9 floats = one triangle (3 vertices × 3 components).
        auto vertex_count = positions.size() / 3;
        if ((vertex_count % 3) != 0) {
            warnln("Mesh '{}' produced {} vertices, which is not divisible by 3", entity.mesh_renderer.mesh, vertex_count);
            continue;
        }

        auto range_first = static_cast<u32>(vertices.size());

        for (size_t tri = 0; tri < vertex_count; tri += 3) {
            // Transform all 3 vertices of this triangle into world space.
            Vec3 world_verts[3];
            for (int v = 0; v < 3; ++v) {
                auto base = (tri + static_cast<size_t>(v)) * 3;
                Vec3 p { positions[base], positions[base + 1], positions[base + 2] };
                p.x *= entity.transform.scale[0];
                p.y *= entity.transform.scale[1];
                p.z *= entity.transform.scale[2];
                p = rotate_by_quaternion(p, entity.transform.rotation[0], entity.transform.rotation[1], entity.transform.rotation[2], entity.transform.rotation[3]);
                p.x += entity.transform.position[0];
                p.y += entity.transform.position[1];
                p.z += entity.transform.position[2];
                world_verts[v] = p;
            }

            for (int v = 0; v < 3; ++v) {
                Vec3 normal;
                if (has_smooth_normals) {
                    // Use per-vertex normal from the mesh asset; rotate into world space (no scale — normals use inverse-transpose, but for uniform scale rotation is sufficient).
                    auto base = (tri + static_cast<size_t>(v)) * 3;
                    Vec3 n { mesh_asset->normals[base], mesh_asset->normals[base + 1], mesh_asset->normals[base + 2] };
                    normal = normalize(rotate_by_quaternion(n, entity.transform.rotation[0], entity.transform.rotation[1], entity.transform.rotation[2], entity.transform.rotation[3]));
                } else {
                    // Fall back to flat face normal computed from world-space triangle edges.
                    auto edge0 = subtract(world_verts[1], world_verts[0]);
                    auto edge1 = subtract(world_verts[2], world_verts[0]);
                    normal = normalize(cross(edge0, edge1));
                }

                float uv_u = 0.0f, uv_v = 0.0f;
                if (!mesh_asset->texcoords.is_empty()) {
                    auto uv_base = (tri + static_cast<size_t>(v)) * 2;
                    if (uv_base + 1 < mesh_asset->texcoords.size()) {
                        uv_u = mesh_asset->texcoords[uv_base];
                        uv_v = mesh_asset->texcoords[uv_base + 1];
                    }
                }
                float tan_x = 0.0f, tan_y = 0.0f, tan_z = 0.0f, tan_w = 0.0f;
                if (!mesh_asset->tangents.is_empty()) {
                    auto tan_base = (tri + static_cast<size_t>(v)) * 4;
                    if (tan_base + 3 < mesh_asset->tangents.size()) {
                        tan_x = mesh_asset->tangents[tan_base];
                        tan_y = mesh_asset->tangents[tan_base + 1];
                        tan_z = mesh_asset->tangents[tan_base + 2];
                        tan_w = mesh_asset->tangents[tan_base + 3];
                    }
                }
                vertices.append(Vertex {
                    .position { world_verts[v].x, world_verts[v].y, world_verts[v].z },
                    .normal { normal.x, normal.y, normal.z },
                    .color { color[0], color[1], color[2] },
                    .uv { uv_u, uv_v },
                    .tangent { tan_x, tan_y, tan_z, tan_w },
                });
            }
        }

        auto range_count = static_cast<u32>(vertices.size()) - range_first;
        if (range_count > 0)
            out_ranges.append({ .material = entity.mesh_renderer.material, .normal_map = {}, .first_vertex = range_first, .vertex_count = range_count, .instance_entity_ids = {} });
    }

    return vertices;
}

static Vector<Vertex> build_probe_vertices()
{
    Vector<Vertex> vertices;
    // Probe triangle faces the camera (+Z), so normal = (0, 0, 1).
    vertices.ensure_capacity(3);
    vertices.append(Vertex { .position { -0.5f, -0.5f, 0.0f }, .normal { 0.0f, 0.0f, 1.0f }, .color { 0.20f, 0.80f, 0.95f }, .uv { 0.0f, 1.0f }, .tangent {} });
    vertices.append(Vertex { .position { 0.5f, -0.5f, 0.0f }, .normal { 0.0f, 0.0f, 1.0f }, .color { 0.20f, 0.80f, 0.95f }, .uv { 1.0f, 1.0f }, .tangent {} });
    vertices.append(Vertex { .position { 0.0f, 0.5f, 0.0f }, .normal { 0.0f, 0.0f, 1.0f }, .color { 0.20f, 0.80f, 0.95f }, .uv { 0.5f, 0.0f }, .tangent {} });
    return vertices;
}

// Build world geometry grouped by (mesh, material) for GPU instancing.
// Returns mesh vertices in local space (no transform applied), per-instance transform data
// (16 floats per instance, column-major mat4), and per-group draw metadata.
// Instances for each group are contiguous in out_instance_data.
static void build_world_instanced(
    World const& world,
    MeshLibrary& mesh_library,
    Vector<Vertex>& out_mesh_vertices,
    Vector<float>& out_instance_data,
    Vector<DrawGroup>& out_groups)
{
    HashMap<String, u32> group_index_by_key;
    Vector<Vector<Array<float, 16>>> group_instances; // per-group instance transform lists

    for (auto const& entity : world.entities()) {
        if (!entity.alive || entity.panel.has_value())
            continue;

        auto const& mesh_name = entity.mesh_renderer.mesh;
        auto const& normal_map = entity.mesh_renderer.normal_map;
        // For glTF meshes with no explicit setMaterial() call (material is empty or "default"),
        // use the mesh path as the material key. MeshLibrary registers an alias from the
        // mesh path to the first glTF material name at load time.
        auto const& raw_material = entity.mesh_renderer.material;
        auto sv = mesh_name.bytes_as_string_view();
        bool const is_gltf = sv.ends_with(".glb"sv) || sv.ends_with(".gltf"sv);
        bool const is_default_material = raw_material.is_empty() || raw_material == "default"_string;
        String material = (is_gltf && is_default_material) ? mesh_name : raw_material;
        auto key = MUST(String::formatted("{}:{}:{}", mesh_name, material, normal_map));

        u32 group_idx = 0;
        auto key_it = group_index_by_key.find(key);
        if (key_it != group_index_by_key.end()) {
            group_idx = key_it->value;
        } else {
            // Resolve mesh; fall back to cube on error.
            auto mesh_result = mesh_library.resolve_mesh(mesh_name);
            if (mesh_result.is_error())
                mesh_result = mesh_library.resolve_mesh("cube"_string);
            if (mesh_result.is_error())
                continue;
            auto const* mesh_asset = mesh_result.release_value();

            auto color = color_for_material(material);
            auto const& positions = mesh_asset->positions;
            auto const& normals_arr = mesh_asset->normals;
            auto const& texcoords = mesh_asset->texcoords;
            auto const& tangents_arr = mesh_asset->tangents;
            bool const has_normals = normals_arr.size() == positions.size();
            bool const has_texcoords = !texcoords.is_empty();
            bool const has_tangents = tangents_arr.size() == (positions.size() / 3) * 4;
            auto const vertex_count = positions.size() / 3;

            DrawGroup group;
            group.material = material;
            group.normal_map = normal_map;
            group.first_vertex = static_cast<u32>(out_mesh_vertices.size());

            for (size_t tri = 0; tri < vertex_count; tri += 3) {
                Vec3 face_normal { 0.0f, 0.0f, 1.0f };
                if (!has_normals) {
                    auto b0 = tri * 3, b1 = (tri + 1) * 3, b2 = (tri + 2) * 3;
                    Vec3 v0 { positions[b0], positions[b0 + 1], positions[b0 + 2] };
                    Vec3 v1 { positions[b1], positions[b1 + 1], positions[b1 + 2] };
                    Vec3 v2 { positions[b2], positions[b2 + 1], positions[b2 + 2] };
                    face_normal = normalize(cross(subtract(v1, v0), subtract(v2, v0)));
                }
                for (int v = 0; v < 3; ++v) {
                    auto bi3 = (tri + static_cast<size_t>(v)) * 3;
                    auto bi2 = (tri + static_cast<size_t>(v)) * 2;
                    auto bi4 = (tri + static_cast<size_t>(v)) * 4;
                    Vec3 normal = face_normal;
                    if (has_normals)
                        normal = { normals_arr[bi3], normals_arr[bi3 + 1], normals_arr[bi3 + 2] };
                    float uv_u = 0.0f, uv_v = 0.0f;
                    if (has_texcoords && bi2 + 1 < texcoords.size()) {
                        uv_u = texcoords[bi2];
                        uv_v = texcoords[bi2 + 1];
                    }
                    float tan_x = 0.0f, tan_y = 0.0f, tan_z = 0.0f, tan_w = 0.0f;
                    if (has_tangents && bi4 + 3 < tangents_arr.size()) {
                        tan_x = tangents_arr[bi4];
                        tan_y = tangents_arr[bi4 + 1];
                        tan_z = tangents_arr[bi4 + 2];
                        tan_w = tangents_arr[bi4 + 3];
                    }
                    out_mesh_vertices.append(Vertex {
                        .position { positions[bi3], positions[bi3 + 1], positions[bi3 + 2] },
                        .normal { normal.x, normal.y, normal.z },
                        .color { color[0], color[1], color[2] },
                        .uv { uv_u, uv_v },
                        .tangent { tan_x, tan_y, tan_z, tan_w },
                    });
                }
            }

            group.vertex_count = static_cast<u32>(out_mesh_vertices.size()) - group.first_vertex;
            // Centroid Z for back-to-front sort of blend groups.
            if (group.vertex_count > 0) {
                float z_sum = 0.0f;
                for (u32 vi = group.first_vertex; vi < group.first_vertex + group.vertex_count; ++vi)
                    z_sum += out_mesh_vertices[vi].position[2];
                group.centroid_z = z_sum / static_cast<float>(group.vertex_count);
            }
            // Store local AABB so expand_group_aabb can use it for future instances.
            group.aabb_local_min[0] = mesh_asset->aabb_min[0];
            group.aabb_local_min[1] = mesh_asset->aabb_min[1];
            group.aabb_local_min[2] = mesh_asset->aabb_min[2];
            group.aabb_local_max[0] = mesh_asset->aabb_max[0];
            group.aabb_local_max[1] = mesh_asset->aabb_max[1];
            group.aabb_local_max[2] = mesh_asset->aabb_max[2];
            group_idx = static_cast<u32>(out_groups.size());
            out_groups.append(move(group));
            group_instances.append({});
            group_index_by_key.set(move(key), group_idx);
        }

        group_instances[group_idx].append(make_trs_matrix(entity.transform));
        out_groups[group_idx].instance_entity_ids.append(entity.id);
        // Expand this group's world-space AABB to include this instance.
        expand_group_aabb(out_groups[group_idx], entity.transform);
    }

    // Build instance buffer in group order so each group's instances are contiguous.
    for (u32 i = 0; i < out_groups.size(); ++i) {
        out_groups[i].first_instance = static_cast<u32>(out_instance_data.size()) / 16;
        out_groups[i].instance_count = static_cast<u32>(group_instances[i].size());
        for (auto const& mat : group_instances[i]) {
            for (auto f : mat)
                out_instance_data.append(f);
        }
    }
}

static Vector<TexturedVertex> build_panel_vertices(World const& world)
{
    Vector<TexturedVertex> vertices;

    for (auto const& entity : world.entities()) {
        if (!entity.alive || !entity.panel.has_value())
            continue;

        auto half_width = entity.panel->width * entity.transform.scale[0] * 0.5f;
        auto half_height = entity.panel->height * entity.transform.scale[1] * 0.5f;

        Array<Vec3, 4> corners {
            Vec3 { -half_width, -half_height, 0.0f },
            Vec3 { half_width, -half_height, 0.0f },
            Vec3 { half_width, half_height, 0.0f },
            Vec3 { -half_width, half_height, 0.0f },
        };

        for (auto& corner : corners) {
            corner = rotate_by_quaternion(corner, entity.transform.rotation[0], entity.transform.rotation[1], entity.transform.rotation[2], entity.transform.rotation[3]);
            corner.x += entity.transform.position[0];
            corner.y += entity.transform.position[1];
            corner.z += entity.transform.position[2];
        }

        vertices.append(TexturedVertex { .position { corners[0].x, corners[0].y, corners[0].z }, .uv { 0.0f, 1.0f } });
        vertices.append(TexturedVertex { .position { corners[1].x, corners[1].y, corners[1].z }, .uv { 1.0f, 1.0f } });
        vertices.append(TexturedVertex { .position { corners[2].x, corners[2].y, corners[2].z }, .uv { 1.0f, 0.0f } });
        vertices.append(TexturedVertex { .position { corners[0].x, corners[0].y, corners[0].z }, .uv { 0.0f, 1.0f } });
        vertices.append(TexturedVertex { .position { corners[2].x, corners[2].y, corners[2].z }, .uv { 1.0f, 0.0f } });
        vertices.append(TexturedVertex { .position { corners[3].x, corners[3].y, corners[3].z }, .uv { 0.0f, 0.0f } });
    }

    return vertices;
}

static Vector<TexturedVertex> build_overlay_vertices(VulkanRenderer::OverlayView const& overlay_view)
{
    if (overlay_view.width == 0 || overlay_view.height == 0)
        return {};

    // z = 0.999 so the overlay always passes the reversed-Z depth test (compare=GREATER, clear=0.0).
    // z=0.0 fails (0>0 is false); z=0.999 passes against all scene geometry which has depth < 1.0.
    static constexpr float kOverlayDepth = 0.999f;
    Vector<TexturedVertex> vertices;
    vertices.ensure_capacity(6);
    vertices.append(TexturedVertex { .position { -1.0f,  1.0f, kOverlayDepth }, .uv { 0.0f, 1.0f } });
    vertices.append(TexturedVertex { .position {  1.0f,  1.0f, kOverlayDepth }, .uv { 1.0f, 1.0f } });
    vertices.append(TexturedVertex { .position {  1.0f, -1.0f, kOverlayDepth }, .uv { 1.0f, 0.0f } });
    vertices.append(TexturedVertex { .position { -1.0f,  1.0f, kOverlayDepth }, .uv { 0.0f, 1.0f } });
    vertices.append(TexturedVertex { .position {  1.0f, -1.0f, kOverlayDepth }, .uv { 1.0f, 0.0f } });
    vertices.append(TexturedVertex { .position { -1.0f, -1.0f, kOverlayDepth }, .uv { 0.0f, 0.0f } });
    return vertices;
}

static ErrorOr<VkPhysicalDevice> pick_physical_device(VkInstance instance, VkSurfaceKHR surface, u32& graphics_queue_family)
{
    u32 device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (device_count == 0)
        return Error::from_string_literal("No Vulkan physical devices available");

    Vector<VkPhysicalDevice> devices;
    devices.resize(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

    for (auto device : devices) {
        u32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        Vector<VkQueueFamilyProperties> queue_families;
        queue_families.resize(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

        for (u32 i = 0; i < queue_families.size(); ++i) {
            VkBool32 present_supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_supported);
            if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_supported) {
                graphics_queue_family = i;
                return device;
            }
        }
    }

    return Error::from_string_literal("No Vulkan device has a graphics queue that can present to the SDL surface");
}

static ErrorOr<VkDevice> create_logical_device(VkPhysicalDevice physical_device, u32 graphics_queue_family)
{
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = graphics_queue_family;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    Array<char const*, 1> device_extensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo device_create_info {};
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.queueCreateInfoCount = 1;
    device_create_info.pQueueCreateInfos = &queue_create_info;
    device_create_info.enabledExtensionCount = device_extensions.size();
    device_create_info.ppEnabledExtensionNames = device_extensions.data();

    VkDevice device { VK_NULL_HANDLE };
    auto result = vkCreateDevice(physical_device, &device_create_info, nullptr, &device);
    if (result != VK_SUCCESS) {
        warnln("vkCreateDevice failed with VkResult {}", to_underlying(result));
        return Error::from_string_literal("vkCreateDevice failed");
    }
    return device;
}

static VkSurfaceFormatKHR choose_surface_format(Vector<VkSurfaceFormatKHR> const& formats)
{
    for (auto const& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return format;
    }
    return formats.first();
}

static VkPresentModeKHR choose_present_mode(Vector<VkPresentModeKHR> const& present_modes)
{
    for (auto mode : present_modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static ErrorOr<VkShaderModule> create_shader_module(VkDevice device, ShaderBytecode const& bytecode)
{
    VkShaderModuleCreateInfo create_info {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = bytecode.byte_size();
    create_info.pCode = bytecode.data();

    VkShaderModule shader_module { VK_NULL_HANDLE };
    auto result = vkCreateShaderModule(device, &create_info, nullptr, &shader_module);
    if (result != VK_SUCCESS) {
        warnln("vkCreateShaderModule failed with VkResult {}", to_underlying(result));
        return Error::from_string_literal("vkCreateShaderModule failed");
    }
    return shader_module;
}

static ErrorOr<u32> find_memory_type(VkPhysicalDevice physical_device, u32 type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    for (u32 i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return Error::from_string_literal("No suitable Vulkan memory type found");
}

static ErrorOr<void> create_vertex_buffer(VkPhysicalDevice physical_device, VkDevice device, Vector<Vertex> const& vertices, VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo buffer_create_info {};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = vertices.size() * sizeof(Vertex);
    buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    auto result = vkCreateBuffer(device, &buffer_create_info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        warnln("vkCreateBuffer failed with VkResult {}", to_underlying(result));
        return Error::from_string_literal("vkCreateBuffer failed");
    }

    VkMemoryRequirements memory_requirements {};
    vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);

    VkMemoryAllocateInfo allocate_info {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = memory_requirements.size;
    allocate_info.memoryTypeIndex = TRY(find_memory_type(physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

    result = vkAllocateMemory(device, &allocate_info, nullptr, &memory);
    if (result != VK_SUCCESS) {
        warnln("vkAllocateMemory failed with VkResult {}", to_underlying(result));
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return Error::from_string_literal("vkAllocateMemory failed");
    }

    void* mapped_memory = nullptr;
    result = vkMapMemory(device, memory, 0, buffer_create_info.size, 0, &mapped_memory);
    if (result != VK_SUCCESS) {
        warnln("vkMapMemory failed with VkResult {}", to_underlying(result));
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return Error::from_string_literal("vkMapMemory failed");
    }

    __builtin_memcpy(mapped_memory, vertices.data(), vertices.size() * sizeof(Vertex));
    vkUnmapMemory(device, memory);

    result = vkBindBufferMemory(device, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        warnln("vkBindBufferMemory failed with VkResult {}", to_underlying(result));
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return Error::from_string_literal("vkBindBufferMemory failed");
    }

    return {};
}

static ErrorOr<void> create_textured_vertex_buffer(VkPhysicalDevice physical_device, VkDevice device, Vector<TexturedVertex> const& vertices, VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo buffer_create_info {};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = vertices.size() * sizeof(TexturedVertex);
    buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    auto result = vkCreateBuffer(device, &buffer_create_info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        warnln("vkCreateBuffer failed with VkResult {}", to_underlying(result));
        return Error::from_string_literal("vkCreateBuffer failed");
    }

    VkMemoryRequirements memory_requirements {};
    vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);

    VkMemoryAllocateInfo allocate_info {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = memory_requirements.size;
    allocate_info.memoryTypeIndex = TRY(find_memory_type(physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

    result = vkAllocateMemory(device, &allocate_info, nullptr, &memory);
    if (result != VK_SUCCESS) {
        warnln("vkAllocateMemory failed with VkResult {}", to_underlying(result));
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return Error::from_string_literal("vkAllocateMemory failed");
    }

    void* mapped_memory = nullptr;
    result = vkMapMemory(device, memory, 0, buffer_create_info.size, 0, &mapped_memory);
    if (result != VK_SUCCESS) {
        warnln("vkMapMemory failed with VkResult {}", to_underlying(result));
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return Error::from_string_literal("vkMapMemory failed");
    }

    __builtin_memcpy(mapped_memory, vertices.data(), vertices.size() * sizeof(TexturedVertex));
    vkUnmapMemory(device, memory);

    result = vkBindBufferMemory(device, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        warnln("vkBindBufferMemory failed with VkResult {}", to_underlying(result));
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return Error::from_string_literal("vkBindBufferMemory failed");
    }

    return {};
}

static ErrorOr<void> create_buffer(VkPhysicalDevice physical_device, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo buffer_create_info {};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = size;
    buffer_create_info.usage = usage;
    buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    auto result = vkCreateBuffer(device, &buffer_create_info, nullptr, &buffer);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkCreateBuffer failed");

    VkMemoryRequirements memory_requirements {};
    vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);

    VkMemoryAllocateInfo allocate_info {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = memory_requirements.size;
    allocate_info.memoryTypeIndex = TRY(find_memory_type(physical_device, memory_requirements.memoryTypeBits, properties));

    result = vkAllocateMemory(device, &allocate_info, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return Error::from_string_literal("vkAllocateMemory failed");
    }

    result = vkBindBufferMemory(device, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return Error::from_string_literal("vkBindBufferMemory failed");
    }

    return {};
}

static ErrorOr<void> create_image(VkPhysicalDevice physical_device, VkDevice device, u32 width, u32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& memory)
{
    VkImageCreateInfo image_create_info {};
    image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.extent = { width, height, 1 };
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.format = format;
    image_create_info.tiling = tiling;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.usage = usage;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    auto result = vkCreateImage(device, &image_create_info, nullptr, &image);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkCreateImage failed");

    VkMemoryRequirements memory_requirements {};
    vkGetImageMemoryRequirements(device, image, &memory_requirements);

    VkMemoryAllocateInfo allocate_info {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = memory_requirements.size;
    allocate_info.memoryTypeIndex = TRY(find_memory_type(physical_device, memory_requirements.memoryTypeBits, properties));

    result = vkAllocateMemory(device, &allocate_info, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
        return Error::from_string_literal("vkAllocateMemory failed");
    }

    result = vkBindImageMemory(device, image, memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
        return Error::from_string_literal("vkBindImageMemory failed");
    }

    return {};
}

static void destroy_buffer_resource(VkDevice device, VulkanRenderer::Impl::BufferResource& resource)
{
    if (resource.buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, resource.buffer, nullptr);
    if (resource.memory != VK_NULL_HANDLE)
        vkFreeMemory(device, resource.memory, nullptr);
    resource.buffer = VK_NULL_HANDLE;
    resource.memory = VK_NULL_HANDLE;
    resource.capacity = 0;
}

template<typename VertexType>
static ErrorOr<void> upload_vertices_to_buffer(VkPhysicalDevice physical_device, VkDevice device, Vector<VertexType> const& vertices, VulkanRenderer::Impl::BufferResource& resource)
{
    auto required_size = static_cast<VkDeviceSize>(vertices.size() * sizeof(VertexType));
    if (required_size == 0) {
        destroy_buffer_resource(device, resource);
        return {};
    }

    if (resource.buffer == VK_NULL_HANDLE || resource.capacity < required_size) {
        destroy_buffer_resource(device, resource);
        TRY(create_buffer(physical_device, device, required_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, resource.buffer, resource.memory));
        resource.capacity = required_size;
    }

    void* mapped_memory = nullptr;
    auto result = vkMapMemory(device, resource.memory, 0, required_size, 0, &mapped_memory);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkMapMemory for persistent vertex buffer failed");
    __builtin_memcpy(mapped_memory, vertices.data(), static_cast<size_t>(required_size));
    vkUnmapMemory(device, resource.memory);
    return {};
}

// Copy srcBuffer -> dstBuffer via a one-shot command buffer. Used for staging → device-local uploads.
static ErrorOr<void> copy_buffer(VkDevice device, VkCommandPool command_pool, VkQueue queue, VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;
    VkCommandBuffer cmd { VK_NULL_HANDLE };
    if (vkAllocateCommandBuffers(device, &alloc_info, &cmd) != VK_SUCCESS)
        return Error::from_string_literal("vkAllocateCommandBuffers for copy_buffer failed");

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkBufferCopy copy_region { .srcOffset = 0, .dstOffset = 0, .size = size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &copy_region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, command_pool, 1, &cmd);
    return {};
}

// Upload arbitrary data to a device-local VkBuffer via a staging buffer.
// usage: full set of VkBufferUsageFlags for the device-local buffer (caller must include TRANSFER_DST_BIT).
// Must be called after vkWaitForFences — uses impl.command_pool for the transfer command.
template<typename ElementType>
static ErrorOr<void> upload_buffer_device_local(VulkanRenderer::Impl& impl, Vector<ElementType> const& data, VulkanRenderer::Impl::BufferResource& resource, VkBufferUsageFlags usage)
{
    auto size = static_cast<VkDeviceSize>(data.size() * sizeof(ElementType));
    if (size == 0) {
        destroy_buffer_resource(impl.device, resource);
        return {};
    }

    // Re-allocate device-local buffer only if size grew.
    if (resource.buffer == VK_NULL_HANDLE || resource.capacity < size) {
        destroy_buffer_resource(impl.device, resource);
        TRY(create_buffer(impl.physical_device, impl.device, size, usage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            resource.buffer, resource.memory));
        resource.capacity = size;
    }

    // Staging buffer (host-visible, short-lived).
    VkBuffer staging_buffer { VK_NULL_HANDLE };
    VkDeviceMemory staging_memory { VK_NULL_HANDLE };
    TRY(create_buffer(impl.physical_device, impl.device, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_buffer, staging_memory));

    void* mapped = nullptr;
    if (vkMapMemory(impl.device, staging_memory, 0, size, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(impl.device, staging_buffer, nullptr);
        vkFreeMemory(impl.device, staging_memory, nullptr);
        return Error::from_string_literal("vkMapMemory for staging buffer failed");
    }
    __builtin_memcpy(mapped, data.data(), static_cast<size_t>(size));
    vkUnmapMemory(impl.device, staging_memory);

    auto copy_result = copy_buffer(impl.device, impl.command_pool, impl.graphics_queue, staging_buffer, resource.buffer, size);
    vkDestroyBuffer(impl.device, staging_buffer, nullptr);
    vkFreeMemory(impl.device, staging_memory, nullptr);
    return copy_result;
}

// Convenience wrapper: upload vertex/instance data (VERTEX_BUFFER_BIT usage).
template<typename VertexType>
static ErrorOr<void> upload_vertices_device_local(VulkanRenderer::Impl& impl, Vector<VertexType> const& vertices, VulkanRenderer::Impl::BufferResource& resource)
{
    return upload_buffer_device_local(impl, vertices, resource,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
}


static void destroy_texture_resource(VkDevice device, VulkanRenderer::Impl::TextureResource& resource)
{
    if (resource.descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, resource.descriptor_pool, nullptr);
    if (resource.sampler != VK_NULL_HANDLE)
        vkDestroySampler(device, resource.sampler, nullptr);
    if (resource.image_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, resource.image_view, nullptr);
    if (resource.image != VK_NULL_HANDLE)
        vkDestroyImage(device, resource.image, nullptr);
    if (resource.image_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, resource.image_memory, nullptr);
    resource = {};
}

static ErrorOr<void> ensure_texture_resource(VulkanRenderer::Impl& impl, VulkanRenderer::Impl::TextureResource& resource, u32 width, u32 height, VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE)
{
    if (width == 0 || height == 0)
        return {};

    if (resource.image != VK_NULL_HANDLE && resource.width == width && resource.height == height)
        return {};

    destroy_texture_resource(impl.device, resource);

    TRY(create_image(impl.physical_device, impl.device, width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, resource.image, resource.image_memory));

    VkImageViewCreateInfo image_view_create_info {};
    image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_create_info.image = resource.image;
    image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_create_info.subresourceRange.baseMipLevel = 0;
    image_view_create_info.subresourceRange.levelCount = 1;
    image_view_create_info.subresourceRange.baseArrayLayer = 0;
    image_view_create_info.subresourceRange.layerCount = 1;
    auto result = vkCreateImageView(impl.device, &image_view_create_info, nullptr, &resource.image_view);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkCreateImageView for persistent texture failed");

    VkSamplerCreateInfo sampler_create_info {};
    sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_create_info.magFilter = VK_FILTER_LINEAR;
    sampler_create_info.minFilter = VK_FILTER_LINEAR;
    sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_create_info.maxLod = 1.0f;
    result = vkCreateSampler(impl.device, &sampler_create_info, nullptr, &resource.sampler);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkCreateSampler for persistent texture failed");

    VkDescriptorPoolSize descriptor_pool_size {};
    descriptor_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo descriptor_pool_create_info {};
    descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_create_info.poolSizeCount = 1;
    descriptor_pool_create_info.pPoolSizes = &descriptor_pool_size;
    descriptor_pool_create_info.maxSets = 1;
    result = vkCreateDescriptorPool(impl.device, &descriptor_pool_create_info, nullptr, &resource.descriptor_pool);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkCreateDescriptorPool for persistent texture failed");

    VkDescriptorSetAllocateInfo descriptor_set_allocate_info {};
    descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_set_allocate_info.descriptorPool = resource.descriptor_pool;
    descriptor_set_allocate_info.descriptorSetCount = 1;
    auto effective_layout = descriptor_set_layout != VK_NULL_HANDLE ? descriptor_set_layout : impl.panel_descriptor_set_layout;
    descriptor_set_allocate_info.pSetLayouts = &effective_layout;
    result = vkAllocateDescriptorSets(impl.device, &descriptor_set_allocate_info, &resource.descriptor_set);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkAllocateDescriptorSets for persistent texture failed");

    VkDescriptorImageInfo descriptor_image_info {};
    descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptor_image_info.imageView = resource.image_view;
    descriptor_image_info.sampler = resource.sampler;
    VkWriteDescriptorSet descriptor_write {};
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstSet = resource.descriptor_set;
    descriptor_write.dstBinding = 0;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_write.descriptorCount = 1;
    descriptor_write.pImageInfo = &descriptor_image_info;
    vkUpdateDescriptorSets(impl.device, 1, &descriptor_write, 0, nullptr);

    resource.width = width;
    resource.height = height;
    resource.has_uploaded_contents = false;
    return {};
}

static ErrorOr<void> copy_bitmap_to_mapped_staging(Gfx::Bitmap const& bitmap, void* mapped_memory, u32 width, u32 height)
{
    auto expected_width = static_cast<int>(width);
    auto expected_height = static_cast<int>(height);
    if (bitmap.width() != expected_width || bitmap.height() != expected_height)
        return Error::from_string_literal("Bitmap size mismatch for staging upload");

    auto row_bytes = static_cast<size_t>(width) * 4;
    auto* destination = static_cast<u8*>(mapped_memory);
    for (u32 y = 0; y < height; ++y) {
        auto const* source = bitmap.scanline_u8(static_cast<int>(y));
        __builtin_memcpy(destination + (static_cast<size_t>(y) * row_bytes), source, row_bytes);
    }
    return {};
}

static void transition_image_layout(VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

static void copy_buffer_to_image(VkCommandBuffer command_buffer, VkBuffer buffer, VkImage image, u32 width, u32 height)
{
    VkBufferImageCopy region {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

static void destroy_swapchain_resources(VkDevice device, VulkanRenderer::Impl& impl)
{
    if (device == VK_NULL_HANDLE)
        return;

    destroy_buffer_resource(device, impl.world_vertex_buffer);
    destroy_buffer_resource(device, impl.world_instance_buffer);
    destroy_buffer_resource(device, impl.indirect_ref_buffer);
    destroy_buffer_resource(device, impl.indirect_draw_buffer);
    destroy_buffer_resource(device, impl.aabb_world_buffer);
    destroy_buffer_resource(device, impl.panel_vertex_buffer);
    destroy_buffer_resource(device, impl.overlay_vertex_buffer);
    impl.world_vertex_count = 0;
    impl.world_instance_count = 0;
    impl.indirect_draw_count = 0;
    impl.panel_vertex_count = 0;
    impl.overlay_vertex_count = 0;
    // Cull pipeline.
    if (impl.cull_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, impl.cull_pipeline, nullptr);
    if (impl.cull_pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, impl.cull_pipeline_layout, nullptr);
    if (impl.cull_descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, impl.cull_descriptor_pool, nullptr);
    if (impl.cull_descriptor_set_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, impl.cull_descriptor_set_layout, nullptr);
    impl.cull_pipeline = VK_NULL_HANDLE;
    impl.cull_pipeline_layout = VK_NULL_HANDLE;
    impl.cull_descriptor_pool = VK_NULL_HANDLE;
    impl.cull_descriptor_set = VK_NULL_HANDLE;
    impl.cull_descriptor_set_layout = VK_NULL_HANDLE;
    destroy_texture_resource(device, impl.panel_texture);
    destroy_texture_resource(device, impl.overlay_texture);

    if (impl.in_flight != VK_NULL_HANDLE)
        vkDestroyFence(device, impl.in_flight, nullptr);
    if (impl.render_finished != VK_NULL_HANDLE)
        vkDestroySemaphore(device, impl.render_finished, nullptr);
    if (impl.image_available != VK_NULL_HANDLE)
        vkDestroySemaphore(device, impl.image_available, nullptr);
    if (impl.command_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, impl.command_pool, nullptr);
    for (auto framebuffer : impl.framebuffers) {
        if (framebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    impl.framebuffers.clear();
    if (impl.panel_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, impl.panel_pipeline, nullptr);
    if (impl.panel_pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, impl.panel_pipeline_layout, nullptr);
    if (impl.panel_descriptor_set_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, impl.panel_descriptor_set_layout, nullptr);
    if (impl.texture_library.has_value()) {
        impl.texture_library->destroy();
        impl.texture_library.clear();
    }
    destroy_texture_resource(device, impl.albedo_texture);
    destroy_texture_resource(device, impl.flat_normal_texture);
    destroy_texture_resource(device, impl.fallback_black_texture);
    // Destroy PBR descriptor pool (implicitly frees all per-group descriptor sets).
    if (impl.pbr_descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, impl.pbr_descriptor_pool, nullptr);
    impl.pbr_descriptor_pool = VK_NULL_HANDLE;
    impl.pbr_descriptor_sets.clear();
    if (impl.blend_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, impl.blend_pipeline, nullptr);
    impl.blend_pipeline = VK_NULL_HANDLE;
    if (impl.pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, impl.pipeline, nullptr);
    if (impl.pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, impl.pipeline_layout, nullptr);
    if (impl.light_ubo_descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, impl.light_ubo_descriptor_pool, nullptr);
    if (impl.light_ubo_descriptor_set_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, impl.light_ubo_descriptor_set_layout, nullptr);
    destroy_buffer_resource(device, impl.light_ubo_buffer);
    impl.light_ubo_descriptor_pool = VK_NULL_HANDLE;
    impl.light_ubo_descriptor_set = VK_NULL_HANDLE;
    impl.light_ubo_descriptor_set_layout = VK_NULL_HANDLE;
    if (impl.pbr_descriptor_set_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, impl.pbr_descriptor_set_layout, nullptr);
    impl.pbr_descriptor_set_layout = VK_NULL_HANDLE;
    if (impl.world_descriptor_set_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, impl.world_descriptor_set_layout, nullptr);
    impl.world_descriptor_set_layout = VK_NULL_HANDLE;
    if (impl.depth_image_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, impl.depth_image_view, nullptr);
    if (impl.depth_image != VK_NULL_HANDLE)
        vkDestroyImage(device, impl.depth_image, nullptr);
    if (impl.depth_image_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, impl.depth_image_memory, nullptr);
    impl.depth_image_view = VK_NULL_HANDLE;
    impl.depth_image = VK_NULL_HANDLE;
    impl.depth_image_memory = VK_NULL_HANDLE;
    if (impl.render_pass != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, impl.render_pass, nullptr);
    for (auto image_view : impl.swapchain_image_views) {
        if (image_view != VK_NULL_HANDLE)
            vkDestroyImageView(device, image_view, nullptr);
    }
    impl.swapchain_image_views.clear();
    impl.swapchain_images.clear();
    if (impl.swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, impl.swapchain, nullptr);

    impl.swapchain = VK_NULL_HANDLE;
    impl.render_pass = VK_NULL_HANDLE;
    impl.pipeline_layout = VK_NULL_HANDLE;
    impl.pipeline = VK_NULL_HANDLE;
    impl.blend_pipeline = VK_NULL_HANDLE;
    impl.panel_descriptor_set_layout = VK_NULL_HANDLE;
    impl.panel_pipeline_layout = VK_NULL_HANDLE;
    impl.panel_pipeline = VK_NULL_HANDLE;
    impl.command_pool = VK_NULL_HANDLE;
    impl.command_buffer = VK_NULL_HANDLE;
    impl.image_available = VK_NULL_HANDLE;
    impl.render_finished = VK_NULL_HANDLE;
    impl.in_flight = VK_NULL_HANDLE;
}

static ErrorOr<void> ensure_shader_bytecode_loaded(VulkanRenderer::Impl& impl)
{
    if (impl.shader_bytecode_loaded)
        return {};

    VirtualFileSystem shader_file_system;
    TRY(shader_file_system.mount_directory("mycelium://shaders/"sv, ByteString::formatted("{}", MYCELIUMVR_SHADER_DIRECTORY)));
    TRY(shader_file_system.mount_directory("mycelium://shader-source/"sv, ByteString::formatted("{}", MYCELIUMVR_SHADER_SOURCE_DIRECTORY)));
    TRY(shader_file_system.mount_directory("mycelium://shader-cache/"sv, ByteString::formatted("{}", MYCELIUMVR_SHADER_DIRECTORY), MountPermissions::ReadWrite));

    ShaderCompiler shader_compiler(ShaderCompilerBackend::ShaderC);
    if (shader_compiler.supports_runtime_glsl_compilation()) {
        auto world_vertex_source   = TRY(shader_file_system.read_file("mycelium://shader-source/WorldMesh.vert"sv));
        auto world_fragment_source = TRY(shader_file_system.read_file("mycelium://shader-source/WorldMesh.frag"sv));
        auto panel_vertex_shader_source   = TRY(shader_file_system.read_file("mycelium://shader-source/TexturedQuad.vert"sv));
        auto panel_fragment_shader_source = TRY(shader_file_system.read_file("mycelium://shader-source/TexturedQuad.frag"sv));
        auto cull_compute_source = TRY(shader_file_system.read_file("mycelium://shader-source/FrustumCull.comp"sv));
        impl.vertex_shader_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, {
            .stage = ShaderStage::Vertex,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/WorldMesh.vert"sv,
            .source = StringView { world_vertex_source },
        }, "mycelium://shader-cache/"sv));
        impl.fragment_shader_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, {
            .stage = ShaderStage::Fragment,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/WorldMesh.frag"sv,
            .source = StringView { world_fragment_source },
        }, "mycelium://shader-cache/"sv));
        impl.panel_vertex_shader_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, {
            .stage = ShaderStage::Vertex,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/TexturedQuad.vert"sv,
            .source = StringView { panel_vertex_shader_source },
        }, "mycelium://shader-cache/"sv));
        impl.panel_fragment_shader_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, {
            .stage = ShaderStage::Fragment,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/TexturedQuad.frag"sv,
            .source = StringView { panel_fragment_shader_source },
        }, "mycelium://shader-cache/"sv));
        impl.cull_shader_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, {
            .stage = ShaderStage::Compute,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/FrustumCull.comp"sv,
            .source = StringView { cull_compute_source },
        }, "mycelium://shader-cache/"sv));
        static bool did_log_runtime_shader_resolution = false;
        if (!did_log_runtime_shader_resolution) {
            outln("  Runtime GLSL shaders resolved through {} cache", shader_compiler_backend_name(shader_compiler.backend()));
            did_log_runtime_shader_resolution = true;
        }
    } else {
        // Fallback: no runtime GLSL compiler available, use precompiled probe shaders (no lighting).
        impl.vertex_shader_bytecode = TRY(shader_compiler.load_spirv(shader_file_system, "mycelium://shaders/ProbeTriangle.vert.spv"sv));
        impl.fragment_shader_bytecode = TRY(shader_compiler.load_spirv(shader_file_system, "mycelium://shaders/ProbeTriangle.frag.spv"sv));
        static bool did_log_precompiled_shader_resolution = false;
        if (!did_log_precompiled_shader_resolution) {
            outln("  Runtime GLSL compiler unavailable; loaded precompiled SPIR-V shaders (lighting disabled)");
            did_log_precompiled_shader_resolution = true;
        }
    }

    impl.shader_bytecode_loaded = true;
    return {};
}

static ErrorOr<void> ensure_swapchain_resources(VulkanRenderer::Impl& impl)
{
    TRY(ensure_shader_bytecode_loaded(impl));

    VkSurfaceCapabilitiesKHR surface_capabilities {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(impl.physical_device, impl.surface, &surface_capabilities);

    u32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(impl.physical_device, impl.surface, &format_count, nullptr);
    if (format_count == 0)
        return Error::from_string_literal("No Vulkan surface formats available");
    Vector<VkSurfaceFormatKHR> surface_formats;
    surface_formats.resize(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(impl.physical_device, impl.surface, &format_count, surface_formats.data());

    u32 present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(impl.physical_device, impl.surface, &present_mode_count, nullptr);
    Vector<VkPresentModeKHR> present_modes;
    present_modes.resize(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(impl.physical_device, impl.surface, &present_mode_count, present_modes.data());

    auto surface_format = choose_surface_format(surface_formats);
    auto present_mode = choose_present_mode(present_modes);
    auto extent = surface_capabilities.currentExtent;
    if (extent.width == NumericLimits<u32>::max()) {
        extent.width = impl.drawable_width;
        extent.height = impl.drawable_height;
    }

    if (!impl.swapchain_dirty && impl.swapchain != VK_NULL_HANDLE
        && impl.extent.width == extent.width && impl.extent.height == extent.height
        && impl.surface_format.format == surface_format.format
        && impl.surface_format.colorSpace == surface_format.colorSpace)
        return {};

    if (impl.device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(impl.device);
    destroy_swapchain_resources(impl.device, impl);

    impl.surface_format = surface_format;
    impl.extent = extent;

    auto image_count = surface_capabilities.minImageCount + 1;
    if (surface_capabilities.maxImageCount > 0 && image_count > surface_capabilities.maxImageCount)
        image_count = surface_capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR swapchain_create_info {};
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface = impl.surface;
    swapchain_create_info.minImageCount = image_count;
    swapchain_create_info.imageFormat = surface_format.format;
    swapchain_create_info.imageColorSpace = surface_format.colorSpace;
    swapchain_create_info.imageExtent = extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.preTransform = surface_capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = present_mode;
    swapchain_create_info.clipped = VK_TRUE;

    auto result = vkCreateSwapchainKHR(impl.device, &swapchain_create_info, nullptr, &impl.swapchain);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkCreateSwapchainKHR failed");

    u32 swapchain_image_count = 0;
    vkGetSwapchainImagesKHR(impl.device, impl.swapchain, &swapchain_image_count, nullptr);
    impl.swapchain_images.resize(swapchain_image_count);
    vkGetSwapchainImagesKHR(impl.device, impl.swapchain, &swapchain_image_count, impl.swapchain_images.data());

    impl.swapchain_image_views.resize(swapchain_image_count);
    for (u32 i = 0; i < swapchain_image_count; ++i) {
        VkImageViewCreateInfo image_view_create_info {};
        image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.image = impl.swapchain_images[i];
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = surface_format.format;
        image_view_create_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        result = vkCreateImageView(impl.device, &image_view_create_info, nullptr, &impl.swapchain_image_views[i]);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateImageView failed");
    }

    // Depth image — one shared depth buffer for the whole swapchain.
    impl.depth_format = find_depth_format(impl.physical_device);
    TRY(create_image(impl.physical_device, impl.device,
        extent.width, extent.height,
        impl.depth_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        impl.depth_image, impl.depth_image_memory));
    {
        VkImageViewCreateInfo depth_view_info {};
        depth_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depth_view_info.image = impl.depth_image;
        depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depth_view_info.format = impl.depth_format;
        depth_view_info.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        result = vkCreateImageView(impl.device, &depth_view_info, nullptr, &impl.depth_image_view);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateImageView for depth failed");
    }

    VkAttachmentDescription color_attachment {};
    color_attachment.format = surface_format.format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment {};
    depth_attachment.format = impl.depth_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_attachment_reference {};
    color_attachment_reference.attachment = 0;
    color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_reference {};
    depth_attachment_reference.attachment = 1;
    depth_attachment_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_reference;
    subpass.pDepthStencilAttachment = &depth_attachment_reference;

    Array<VkAttachmentDescription, 2> attachments { color_attachment, depth_attachment };
    VkRenderPassCreateInfo render_pass_create_info {};
    render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_create_info.attachmentCount = attachments.size();
    render_pass_create_info.pAttachments = attachments.data();
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;
    result = vkCreateRenderPass(impl.device, &render_pass_create_info, nullptr, &impl.render_pass);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkCreateRenderPass failed");

    auto vertex_shader = TRY(create_shader_module(impl.device, impl.vertex_shader_bytecode));
    auto fragment_shader = TRY(create_shader_module(impl.device, impl.fragment_shader_bytecode));

    VkPipelineShaderStageCreateInfo vertex_shader_stage {};
    vertex_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertex_shader_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_shader_stage.module = vertex_shader;
    vertex_shader_stage.pName = "main";
    VkPipelineShaderStageCreateInfo fragment_shader_stage {};
    fragment_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragment_shader_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragment_shader_stage.module = fragment_shader;
    fragment_shader_stage.pName = "main";
    Array<VkPipelineShaderStageCreateInfo, 2> shader_stages { vertex_shader_stage, fragment_shader_stage };

    VkPipelineVertexInputStateCreateInfo vertex_input_state {};
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    // Binding 0: per-vertex mesh data.
    // Binding 1: per-instance model matrix (VK_VERTEX_INPUT_RATE_INSTANCE, 64 bytes).
    Array<VkVertexInputBindingDescription, 2> vertex_binding_descriptions {};
    vertex_binding_descriptions[0].binding = 0;
    vertex_binding_descriptions[0].stride = sizeof(Vertex);
    vertex_binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertex_binding_descriptions[1].binding = 1;
    vertex_binding_descriptions[1].stride = sizeof(float) * 16; // mat4
    vertex_binding_descriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    // Attributes: 5 from mesh data (locations 0-3, 8) + 4 mat4 columns from instance data (locations 4-7).
    Array<VkVertexInputAttributeDescription, 9> vertex_attribute_descriptions {};
    vertex_attribute_descriptions[0].location = 0;
    vertex_attribute_descriptions[0].binding = 0;
    vertex_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_descriptions[0].offset = __builtin_offsetof(Vertex, position);
    vertex_attribute_descriptions[1].location = 1;
    vertex_attribute_descriptions[1].binding = 0;
    vertex_attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_descriptions[1].offset = __builtin_offsetof(Vertex, normal);
    vertex_attribute_descriptions[2].location = 2;
    vertex_attribute_descriptions[2].binding = 0;
    vertex_attribute_descriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_descriptions[2].offset = __builtin_offsetof(Vertex, color);
    vertex_attribute_descriptions[3].location = 3;
    vertex_attribute_descriptions[3].binding = 0;
    vertex_attribute_descriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
    vertex_attribute_descriptions[3].offset = __builtin_offsetof(Vertex, uv);
    // mat4 model (binding 1): four vec4 columns at locations 4-7.
    for (int col = 0; col < 4; ++col) {
        vertex_attribute_descriptions[4 + col].location = static_cast<u32>(4 + col);
        vertex_attribute_descriptions[4 + col].binding = 1;
        vertex_attribute_descriptions[4 + col].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vertex_attribute_descriptions[4 + col].offset = static_cast<u32>(col * 16);
    }
    // Tangent (binding 0): vec4 at location 8.
    vertex_attribute_descriptions[8].location = 8;
    vertex_attribute_descriptions[8].binding = 0;
    vertex_attribute_descriptions[8].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertex_attribute_descriptions[8].offset = __builtin_offsetof(Vertex, tangent);
    vertex_input_state.vertexBindingDescriptionCount = vertex_binding_descriptions.size();
    vertex_input_state.pVertexBindingDescriptions = vertex_binding_descriptions.data();
    vertex_input_state.vertexAttributeDescriptionCount = vertex_attribute_descriptions.size();
    vertex_input_state.pVertexAttributeDescriptions = vertex_attribute_descriptions.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state {};
    input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor {};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    VkPipelineViewportStateCreateInfo viewport_state {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rasterization_state {};
    rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode = VK_CULL_MODE_NONE;
    rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization_state.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample_state {};
    multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState color_blend_attachment {};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend_state {};
    color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &color_blend_attachment;

    // World mesh pipeline: two push constant ranges.
    //   [0] Vertex stage: VP matrix (offset 0, 64 bytes).
    //   [1] Fragment stage: material factors (offset 64, 28 bytes).
    VkPushConstantRange world_push_constant_ranges[2] {};
    world_push_constant_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    world_push_constant_ranges[0].offset = 0;
    world_push_constant_ranges[0].size = sizeof(PushConstants);
    world_push_constant_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    world_push_constant_ranges[1].offset = sizeof(PushConstants);
    world_push_constant_ranges[1].size = sizeof(MaterialPushConstants);
    // Panel pipeline: same VP-only push constant, vertex stage.
    VkPushConstantRange push_constant_range {};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstants);

    // World pipeline descriptor set layout: set 0, binding 0 = albedo texture sampler (fragment stage).
    {
        auto binding = VkDescriptorSetLayoutBinding {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };
        VkDescriptorSetLayoutCreateInfo layout_info {};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        result = vkCreateDescriptorSetLayout(impl.device, &layout_info, nullptr, &impl.world_descriptor_set_layout);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateDescriptorSetLayout for world pipeline failed");
    }

    // Scene light UBO descriptor set layout: set 1, binding 0 = uniform buffer (fragment stage).
    {
        auto binding = VkDescriptorSetLayoutBinding {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };
        VkDescriptorSetLayoutCreateInfo layout_info {};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        result = vkCreateDescriptorSetLayout(impl.device, &layout_info, nullptr, &impl.light_ubo_descriptor_set_layout);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateDescriptorSetLayout for light UBO failed");
    }

    // PBR material descriptor set layout: set 0 — 5 COMBINED_IMAGE_SAMPLER bindings (all fragment stage).
    // binding 0 = albedo, 1 = normal, 2 = metallic_roughness, 3 = emissive, 4 = AO.
    {
        Array<VkDescriptorSetLayoutBinding, 5> bindings {};
        for (u32 b = 0; b < 5; ++b) {
            bindings[b].binding = b;
            bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[b].descriptorCount = 1;
            bindings[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[b].pImmutableSamplers = nullptr;
        }
        VkDescriptorSetLayoutCreateInfo layout_info {};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = bindings.size();
        layout_info.pBindings = bindings.data();
        result = vkCreateDescriptorSetLayout(impl.device, &layout_info, nullptr, &impl.pbr_descriptor_set_layout);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateDescriptorSetLayout for PBR material failed");
    }

    // Create the host-visible UBO buffer for scene light data.
    TRY(create_buffer(impl.physical_device, impl.device, sizeof(SceneLightUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        impl.light_ubo_buffer.buffer, impl.light_ubo_buffer.memory));
    impl.light_ubo_buffer.capacity = sizeof(SceneLightUBO);

    // Allocate a descriptor set for the light UBO and bind it to the buffer.
    {
        VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = 1;
        result = vkCreateDescriptorPool(impl.device, &pool_info, nullptr, &impl.light_ubo_descriptor_pool);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateDescriptorPool for light UBO failed");

        VkDescriptorSetAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = impl.light_ubo_descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &impl.light_ubo_descriptor_set_layout;
        result = vkAllocateDescriptorSets(impl.device, &alloc_info, &impl.light_ubo_descriptor_set);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkAllocateDescriptorSets for light UBO failed");

        VkDescriptorBufferInfo buf_info { impl.light_ubo_buffer.buffer, 0, sizeof(SceneLightUBO) };
        VkWriteDescriptorSet write {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = impl.light_ubo_descriptor_set;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &buf_info;
        vkUpdateDescriptorSets(impl.device, 1, &write, 0, nullptr);
    }

    // World pipeline layout: set 0 = PBR material (5 samplers), set 1 = scene light UBO.
    Array<VkDescriptorSetLayout, 2> world_set_layouts { impl.pbr_descriptor_set_layout, impl.light_ubo_descriptor_set_layout };
    VkPipelineLayoutCreateInfo pipeline_layout_create_info {};
    pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = world_set_layouts.size();
    pipeline_layout_create_info.pSetLayouts = world_set_layouts.data();
    pipeline_layout_create_info.pushConstantRangeCount = 2;
    pipeline_layout_create_info.pPushConstantRanges = world_push_constant_ranges;
    result = vkCreatePipelineLayout(impl.device, &pipeline_layout_create_info, nullptr, &impl.pipeline_layout);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkCreatePipelineLayout failed");

    VkGraphicsPipelineCreateInfo pipeline_create_info {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.stageCount = shader_stages.size();
    pipeline_create_info.pStages = shader_stages.data();
    pipeline_create_info.pVertexInputState = &vertex_input_state;
    pipeline_create_info.pInputAssemblyState = &input_assembly_state;
    pipeline_create_info.pViewportState = &viewport_state;
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state {};
    depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthTestEnable = VK_TRUE;
    depth_stencil_state.depthWriteEnable = VK_TRUE;
    depth_stencil_state.depthCompareOp = VK_COMPARE_OP_GREATER;
    depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state.stencilTestEnable = VK_FALSE;
    pipeline_create_info.pRasterizationState = &rasterization_state;
    pipeline_create_info.pMultisampleState = &multisample_state;
    pipeline_create_info.pColorBlendState = &color_blend_state;
    pipeline_create_info.pDepthStencilState = &depth_stencil_state;
    pipeline_create_info.layout = impl.pipeline_layout;
    pipeline_create_info.renderPass = impl.render_pass;
    pipeline_create_info.subpass = 0;
    result = vkCreateGraphicsPipelines(impl.device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &impl.pipeline);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkCreateGraphicsPipelines failed");

    // Alpha-blend pipeline variant: same as opaque but depth writes off and SRC_ALPHA blending.
    // Used for Blend-mode draw groups after all opaque/clip draws are done.
    {
        VkPipelineColorBlendAttachmentState blend_attachment {};
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
        blend_attachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend_state {};
        blend_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend_state.attachmentCount = 1;
        blend_state.pAttachments    = &blend_attachment;

        VkPipelineDepthStencilStateCreateInfo blend_depth {};
        blend_depth.sType              = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        blend_depth.depthTestEnable    = VK_TRUE;
        blend_depth.depthWriteEnable   = VK_FALSE;   // depth writes off for transparency
        blend_depth.depthCompareOp     = VK_COMPARE_OP_GREATER;
        blend_depth.depthBoundsTestEnable = VK_FALSE;
        blend_depth.stencilTestEnable  = VK_FALSE;

        pipeline_create_info.pColorBlendState  = &blend_state;
        pipeline_create_info.pDepthStencilState = &blend_depth;
        result = vkCreateGraphicsPipelines(impl.device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &impl.blend_pipeline);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateGraphicsPipelines for blend pipeline failed");
    }

    vkDestroyShaderModule(impl.device, fragment_shader, nullptr);
    vkDestroyShaderModule(impl.device, vertex_shader, nullptr);

    if (impl.panel_vertex_shader_bytecode.has_value() && impl.panel_fragment_shader_bytecode.has_value()) {
        auto descriptor_set_layout_binding = VkDescriptorSetLayoutBinding {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };
        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info {};
        descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptor_set_layout_create_info.bindingCount = 1;
        descriptor_set_layout_create_info.pBindings = &descriptor_set_layout_binding;
        result = vkCreateDescriptorSetLayout(impl.device, &descriptor_set_layout_create_info, nullptr, &impl.panel_descriptor_set_layout);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateDescriptorSetLayout failed");

        VkPipelineLayoutCreateInfo panel_pipeline_layout_create_info {};
        panel_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        panel_pipeline_layout_create_info.setLayoutCount = 1;
        panel_pipeline_layout_create_info.pSetLayouts = &impl.panel_descriptor_set_layout;
        panel_pipeline_layout_create_info.pushConstantRangeCount = 1;
        panel_pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
        result = vkCreatePipelineLayout(impl.device, &panel_pipeline_layout_create_info, nullptr, &impl.panel_pipeline_layout);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreatePipelineLayout for panel failed");

        auto panel_vertex_shader = TRY(create_shader_module(impl.device, *impl.panel_vertex_shader_bytecode));
        auto panel_fragment_shader = TRY(create_shader_module(impl.device, *impl.panel_fragment_shader_bytecode));

        VkPipelineShaderStageCreateInfo panel_vertex_shader_stage {};
        panel_vertex_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        panel_vertex_shader_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        panel_vertex_shader_stage.module = panel_vertex_shader;
        panel_vertex_shader_stage.pName = "main";
        VkPipelineShaderStageCreateInfo panel_fragment_shader_stage {};
        panel_fragment_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        panel_fragment_shader_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        panel_fragment_shader_stage.module = panel_fragment_shader;
        panel_fragment_shader_stage.pName = "main";
        Array<VkPipelineShaderStageCreateInfo, 2> panel_shader_stages { panel_vertex_shader_stage, panel_fragment_shader_stage };

        VkVertexInputBindingDescription panel_vertex_binding_description {};
        panel_vertex_binding_description.binding = 0;
        panel_vertex_binding_description.stride = sizeof(TexturedVertex);
        panel_vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        Array<VkVertexInputAttributeDescription, 2> panel_vertex_attribute_descriptions;
        panel_vertex_attribute_descriptions[0].location = 0;
        panel_vertex_attribute_descriptions[0].binding = 0;
        panel_vertex_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        panel_vertex_attribute_descriptions[0].offset = __builtin_offsetof(TexturedVertex, position);
        panel_vertex_attribute_descriptions[1].location = 1;
        panel_vertex_attribute_descriptions[1].binding = 0;
        panel_vertex_attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        panel_vertex_attribute_descriptions[1].offset = __builtin_offsetof(TexturedVertex, uv);
        VkPipelineVertexInputStateCreateInfo panel_vertex_input_state {};
        panel_vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        panel_vertex_input_state.vertexBindingDescriptionCount = 1;
        panel_vertex_input_state.pVertexBindingDescriptions = &panel_vertex_binding_description;
        panel_vertex_input_state.vertexAttributeDescriptionCount = panel_vertex_attribute_descriptions.size();
        panel_vertex_input_state.pVertexAttributeDescriptions = panel_vertex_attribute_descriptions.data();

        auto panel_color_blend_attachment = color_blend_attachment;
        panel_color_blend_attachment.blendEnable = VK_TRUE;
        panel_color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        panel_color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        panel_color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        panel_color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        panel_color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        panel_color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo panel_color_blend_state {};
        panel_color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        panel_color_blend_state.attachmentCount = 1;
        panel_color_blend_state.pAttachments = &panel_color_blend_attachment;

        VkGraphicsPipelineCreateInfo panel_pipeline_create_info {};
        panel_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        panel_pipeline_create_info.stageCount = panel_shader_stages.size();
        panel_pipeline_create_info.pStages = panel_shader_stages.data();
        panel_pipeline_create_info.pVertexInputState = &panel_vertex_input_state;
        panel_pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        panel_pipeline_create_info.pViewportState = &viewport_state;
        // Panels write to depth so they occlude world geometry; depth test enabled.
        VkPipelineDepthStencilStateCreateInfo panel_depth_stencil_state {};
        panel_depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        panel_depth_stencil_state.depthTestEnable = VK_TRUE;
        panel_depth_stencil_state.depthWriteEnable = VK_TRUE;
        panel_depth_stencil_state.depthCompareOp = VK_COMPARE_OP_GREATER;
        panel_depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
        panel_depth_stencil_state.stencilTestEnable = VK_FALSE;
        panel_pipeline_create_info.pRasterizationState = &rasterization_state;
        panel_pipeline_create_info.pMultisampleState = &multisample_state;
        panel_pipeline_create_info.pColorBlendState = &panel_color_blend_state;
        panel_pipeline_create_info.pDepthStencilState = &panel_depth_stencil_state;
        panel_pipeline_create_info.layout = impl.panel_pipeline_layout;
        panel_pipeline_create_info.renderPass = impl.render_pass;
        panel_pipeline_create_info.subpass = 0;
        result = vkCreateGraphicsPipelines(impl.device, VK_NULL_HANDLE, 1, &panel_pipeline_create_info, nullptr, &impl.panel_pipeline);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateGraphicsPipelines for panel failed");

        vkDestroyShaderModule(impl.device, panel_fragment_shader, nullptr);
        vkDestroyShaderModule(impl.device, panel_vertex_shader, nullptr);
    }

    // ── Frustum culling compute pipeline ──────────────────────────────────────────────────────
    if (impl.cull_pipeline == VK_NULL_HANDLE && !impl.cull_shader_bytecode.words.is_empty()) {
        // Descriptor set layout: 3 SSBOs (ref_draw, draw, aabb).
        Array<VkDescriptorSetLayoutBinding, 3> cull_bindings {};
        for (u32 b = 0; b < 3; ++b) {
            cull_bindings[b].binding = b;
            cull_bindings[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            cull_bindings[b].descriptorCount = 1;
            cull_bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo cull_dsl_info {};
        cull_dsl_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        cull_dsl_info.bindingCount = cull_bindings.size();
        cull_dsl_info.pBindings = cull_bindings.data();
        result = vkCreateDescriptorSetLayout(impl.device, &cull_dsl_info, nullptr, &impl.cull_descriptor_set_layout);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateDescriptorSetLayout for cull failed");

        // Push constant: mat4 view_proj (64 bytes) + uint group_count (4 bytes) = 68 bytes.
        VkPushConstantRange cull_pc_range {};
        cull_pc_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        cull_pc_range.offset = 0;
        cull_pc_range.size = 68; // mat4 + uint, no padding needed (68 <= 128-byte minimum)

        VkPipelineLayoutCreateInfo cull_layout_info {};
        cull_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        cull_layout_info.setLayoutCount = 1;
        cull_layout_info.pSetLayouts = &impl.cull_descriptor_set_layout;
        cull_layout_info.pushConstantRangeCount = 1;
        cull_layout_info.pPushConstantRanges = &cull_pc_range;
        result = vkCreatePipelineLayout(impl.device, &cull_layout_info, nullptr, &impl.cull_pipeline_layout);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreatePipelineLayout for cull failed");

        // Descriptor pool: one set with 3 storage buffer descriptors.
        VkDescriptorPoolSize cull_pool_size {};
        cull_pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        cull_pool_size.descriptorCount = 3;
        VkDescriptorPoolCreateInfo cull_pool_info {};
        cull_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        cull_pool_info.maxSets = 1;
        cull_pool_info.poolSizeCount = 1;
        cull_pool_info.pPoolSizes = &cull_pool_size;
        result = vkCreateDescriptorPool(impl.device, &cull_pool_info, nullptr, &impl.cull_descriptor_pool);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateDescriptorPool for cull failed");

        VkDescriptorSetAllocateInfo cull_alloc_info {};
        cull_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        cull_alloc_info.descriptorPool = impl.cull_descriptor_pool;
        cull_alloc_info.descriptorSetCount = 1;
        cull_alloc_info.pSetLayouts = &impl.cull_descriptor_set_layout;
        result = vkAllocateDescriptorSets(impl.device, &cull_alloc_info, &impl.cull_descriptor_set);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkAllocateDescriptorSets for cull failed");

        // Compute pipeline.
        VkShaderModule cull_module { VK_NULL_HANDLE };
        auto cull_module_result = create_shader_module(impl.device, impl.cull_shader_bytecode);
        if (cull_module_result.is_error())
            return Error::from_string_literal("Failed to create frustum cull compute shader module");
        cull_module = cull_module_result.release_value();

        VkPipelineShaderStageCreateInfo cull_stage {};
        cull_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cull_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cull_stage.module = cull_module;
        cull_stage.pName = "main";

        VkComputePipelineCreateInfo cull_pipeline_info {};
        cull_pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cull_pipeline_info.stage = cull_stage;
        cull_pipeline_info.layout = impl.cull_pipeline_layout;
        result = vkCreateComputePipelines(impl.device, VK_NULL_HANDLE, 1, &cull_pipeline_info, nullptr, &impl.cull_pipeline);
        vkDestroyShaderModule(impl.device, cull_module, nullptr);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateComputePipelines for frustum cull failed");
    }
    // ── End frustum culling compute pipeline ──────────────────────────────────────────────────

    impl.framebuffers.resize(swapchain_image_count);
    for (u32 i = 0; i < swapchain_image_count; ++i) {
        Array<VkImageView, 2> fb_attachments { impl.swapchain_image_views[i], impl.depth_image_view };
        VkFramebufferCreateInfo framebuffer_create_info {};
        framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_create_info.renderPass = impl.render_pass;
        framebuffer_create_info.attachmentCount = fb_attachments.size();
        framebuffer_create_info.pAttachments = fb_attachments.data();
        framebuffer_create_info.width = extent.width;
        framebuffer_create_info.height = extent.height;
        framebuffer_create_info.layers = 1;
        result = vkCreateFramebuffer(impl.device, &framebuffer_create_info, nullptr, &impl.framebuffers[i]);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateFramebuffer failed");
    }

    VkCommandPoolCreateInfo command_pool_info {};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = impl.graphics_queue_family;
    result = vkCreateCommandPool(impl.device, &command_pool_info, nullptr, &impl.command_pool);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkCreateCommandPool failed");

    VkCommandBufferAllocateInfo command_buffer_allocate_info {};
    command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocate_info.commandPool = impl.command_pool;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_allocate_info.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(impl.device, &command_buffer_allocate_info, &impl.command_buffer);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkAllocateCommandBuffers failed");

    VkSemaphoreCreateInfo semaphore_create_info {};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence_create_info {};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    result = vkCreateSemaphore(impl.device, &semaphore_create_info, nullptr, &impl.image_available);
    if (result == VK_SUCCESS)
        result = vkCreateSemaphore(impl.device, &semaphore_create_info, nullptr, &impl.render_finished);
    if (result == VK_SUCCESS)
        result = vkCreateFence(impl.device, &fence_create_info, nullptr, &impl.in_flight);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("Vulkan sync object creation failed");

    impl.swapchain_dirty = false;
    return {};
}

[[maybe_unused]] static ErrorOr<void> draw_one_swapchain_triangle(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface, VkQueue graphics_queue, u32 graphics_queue_family, VulkanRenderer::CameraState const& camera_state, World const* world = nullptr, Vector<u8> const* panel_pixels = nullptr, u32 panel_width = 0, u32 panel_height = 0, VulkanRenderer::OverlayView const* overlay_view = nullptr)
{
    VirtualFileSystem shader_file_system;
    TRY(shader_file_system.mount_directory("mycelium://shaders/"sv, ByteString::formatted("{}", MYCELIUMVR_SHADER_DIRECTORY)));
    TRY(shader_file_system.mount_directory("mycelium://shader-source/"sv, ByteString::formatted("{}", MYCELIUMVR_SHADER_SOURCE_DIRECTORY)));
    TRY(shader_file_system.mount_directory("mycelium://shader-cache/"sv, ByteString::formatted("{}", MYCELIUMVR_SHADER_DIRECTORY), MountPermissions::ReadWrite));

    ShaderCompiler shader_compiler(ShaderCompilerBackend::ShaderC);
    ShaderBytecode vertex_shader_bytecode;
    ShaderBytecode fragment_shader_bytecode;
    Optional<ShaderBytecode> panel_vertex_shader_bytecode;
    Optional<ShaderBytecode> panel_fragment_shader_bytecode;
    if (shader_compiler.supports_runtime_glsl_compilation()) {
        auto vertex_shader_source = TRY(shader_file_system.read_file("mycelium://shader-source/ProbeTriangle.vert"sv));
        auto fragment_shader_source = TRY(shader_file_system.read_file("mycelium://shader-source/ProbeTriangle.frag"sv));
        auto panel_vertex_shader_source = TRY(shader_file_system.read_file("mycelium://shader-source/TexturedQuad.vert"sv));
        auto panel_fragment_shader_source = TRY(shader_file_system.read_file("mycelium://shader-source/TexturedQuad.frag"sv));
        vertex_shader_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, {
            .stage = ShaderStage::Vertex,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/ProbeTriangle.vert"sv,
            .source = StringView { vertex_shader_source },
        }, "mycelium://shader-cache/"sv));
        fragment_shader_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, {
            .stage = ShaderStage::Fragment,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/ProbeTriangle.frag"sv,
            .source = StringView { fragment_shader_source },
        }, "mycelium://shader-cache/"sv));
        panel_vertex_shader_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, {
            .stage = ShaderStage::Vertex,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/TexturedQuad.vert"sv,
            .source = StringView { panel_vertex_shader_source },
        }, "mycelium://shader-cache/"sv));
        panel_fragment_shader_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, {
            .stage = ShaderStage::Fragment,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/TexturedQuad.frag"sv,
            .source = StringView { panel_fragment_shader_source },
        }, "mycelium://shader-cache/"sv));
        static bool did_log_runtime_shader_resolution = false;
        if (!did_log_runtime_shader_resolution) {
            outln("  Runtime GLSL shaders resolved through {} cache", shader_compiler_backend_name(shader_compiler.backend()));
            did_log_runtime_shader_resolution = true;
        }
    } else {
        vertex_shader_bytecode = TRY(shader_compiler.load_spirv(shader_file_system, "mycelium://shaders/ProbeTriangle.vert.spv"sv));
        fragment_shader_bytecode = TRY(shader_compiler.load_spirv(shader_file_system, "mycelium://shaders/ProbeTriangle.frag.spv"sv));
        static bool did_log_precompiled_shader_resolution = false;
        if (!did_log_precompiled_shader_resolution) {
            outln("  Runtime GLSL compiler unavailable; loaded precompiled SPIR-V shaders");
            did_log_precompiled_shader_resolution = true;
        }
    }

    VkSurfaceCapabilitiesKHR surface_capabilities {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities);

    u32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
    if (format_count == 0)
        return Error::from_string_literal("No Vulkan surface formats available");
    Vector<VkSurfaceFormatKHR> surface_formats;
    surface_formats.resize(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, surface_formats.data());

    u32 present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, nullptr);
    Vector<VkPresentModeKHR> present_modes;
    present_modes.resize(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes.data());

    auto surface_format = choose_surface_format(surface_formats);
    auto present_mode = choose_present_mode(present_modes);
    auto extent = surface_capabilities.currentExtent;
    if (extent.width == NumericLimits<u32>::max()) {
        extent.width = 1280;
        extent.height = 720;
    }

    Vector<TexturedVertex> panel_vertices;
    if (world)
        panel_vertices = build_panel_vertices(*world);
    Vector<TexturedVertex> overlay_vertices;
    if (overlay_view && !overlay_view->pixels.is_empty() && overlay_view->width > 0 && overlay_view->height > 0)
        overlay_vertices = build_overlay_vertices(*overlay_view);

    auto image_count = surface_capabilities.minImageCount + 1;
    if (surface_capabilities.maxImageCount > 0 && image_count > surface_capabilities.maxImageCount)
        image_count = surface_capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR swapchain_create_info {};
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface = surface;
    swapchain_create_info.minImageCount = image_count;
    swapchain_create_info.imageFormat = surface_format.format;
    swapchain_create_info.imageColorSpace = surface_format.colorSpace;
    swapchain_create_info.imageExtent = extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.preTransform = surface_capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = present_mode;
    swapchain_create_info.clipped = VK_TRUE;

    VkSwapchainKHR swapchain { VK_NULL_HANDLE };
    auto result = vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain);
    if (result != VK_SUCCESS) {
        warnln("vkCreateSwapchainKHR failed with VkResult {}", to_underlying(result));
        return Error::from_string_literal("vkCreateSwapchainKHR failed");
    }

    u32 swapchain_image_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr);
    Vector<VkImage> swapchain_images;
    swapchain_images.resize(swapchain_image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images.data());

    Vector<VkImageView> swapchain_image_views;
    swapchain_image_views.resize(swapchain_image_count);
    for (u32 i = 0; i < swapchain_image_count; ++i) {
        VkImageViewCreateInfo image_view_create_info {};
        image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.image = swapchain_images[i];
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = surface_format.format;
        image_view_create_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        result = vkCreateImageView(device, &image_view_create_info, nullptr, &swapchain_image_views[i]);
        if (result != VK_SUCCESS) {
            warnln("vkCreateImageView failed with VkResult {}", to_underlying(result));
            for (auto image_view : swapchain_image_views) {
                if (image_view != VK_NULL_HANDLE)
                    vkDestroyImageView(device, image_view, nullptr);
            }
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            return Error::from_string_literal("vkCreateImageView failed");
        }
    }

    VkAttachmentDescription color_attachment {};
    color_attachment.format = surface_format.format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_reference {};
    color_attachment_reference.attachment = 0;
    color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_reference;

    VkRenderPassCreateInfo render_pass_create_info {};
    render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_create_info.attachmentCount = 1;
    render_pass_create_info.pAttachments = &color_attachment;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass;

    VkRenderPass render_pass { VK_NULL_HANDLE };
    result = vkCreateRenderPass(device, &render_pass_create_info, nullptr, &render_pass);
    if (result != VK_SUCCESS) {
        for (auto image_view : swapchain_image_views)
            vkDestroyImageView(device, image_view, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        warnln("vkCreateRenderPass failed with VkResult {}", to_underlying(result));
        return Error::from_string_literal("vkCreateRenderPass failed");
    }

    auto vertex_shader = TRY(create_shader_module(device, vertex_shader_bytecode));
    auto fragment_shader = TRY(create_shader_module(device, fragment_shader_bytecode));

    VkPipelineShaderStageCreateInfo vertex_shader_stage {};
    vertex_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertex_shader_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_shader_stage.module = vertex_shader;
    vertex_shader_stage.pName = "main";

    VkPipelineShaderStageCreateInfo fragment_shader_stage {};
    fragment_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragment_shader_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragment_shader_stage.module = fragment_shader;
    fragment_shader_stage.pName = "main";

    Array<VkPipelineShaderStageCreateInfo, 2> shader_stages { vertex_shader_stage, fragment_shader_stage };

    VkPipelineVertexInputStateCreateInfo vertex_input_state {};
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkVertexInputBindingDescription vertex_binding_description {};
    vertex_binding_description.binding = 0;
    vertex_binding_description.stride = sizeof(Vertex);
    vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    Array<VkVertexInputAttributeDescription, 2> vertex_attribute_descriptions;
    vertex_attribute_descriptions[0].location = 0;
    vertex_attribute_descriptions[0].binding = 0;
    vertex_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_descriptions[0].offset = __builtin_offsetof(Vertex, position);
    vertex_attribute_descriptions[1].location = 1;
    vertex_attribute_descriptions[1].binding = 0;
    vertex_attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_attribute_descriptions[1].offset = __builtin_offsetof(Vertex, color);
    vertex_input_state.vertexBindingDescriptionCount = 1;
    vertex_input_state.pVertexBindingDescriptions = &vertex_binding_description;
    vertex_input_state.vertexAttributeDescriptionCount = vertex_attribute_descriptions.size();
    vertex_input_state.pVertexAttributeDescriptions = vertex_attribute_descriptions.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state {};
    input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor {};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewport_state {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterization_state {};
    rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state.cullMode = VK_CULL_MODE_NONE;
    rasterization_state.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization_state.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample_state {};
    multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment {};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state {};
    color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &color_blend_attachment;

    VkPipelineLayoutCreateInfo pipeline_layout_create_info {};
    pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkPushConstantRange push_constant_range {};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstants);
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    result = vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipeline_layout);
    if (result != VK_SUCCESS) {
        vkDestroyShaderModule(device, fragment_shader, nullptr);
        vkDestroyShaderModule(device, vertex_shader, nullptr);
        vkDestroyRenderPass(device, render_pass, nullptr);
        for (auto image_view : swapchain_image_views)
            vkDestroyImageView(device, image_view, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        return Error::from_string_literal("vkCreatePipelineLayout failed");
    }

    VkGraphicsPipelineCreateInfo pipeline_create_info {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_create_info.stageCount = shader_stages.size();
    pipeline_create_info.pStages = shader_stages.data();
    pipeline_create_info.pVertexInputState = &vertex_input_state;
    pipeline_create_info.pInputAssemblyState = &input_assembly_state;
    pipeline_create_info.pViewportState = &viewport_state;
    pipeline_create_info.pRasterizationState = &rasterization_state;
    pipeline_create_info.pMultisampleState = &multisample_state;
    pipeline_create_info.pColorBlendState = &color_blend_state;
    pipeline_create_info.layout = pipeline_layout;
    pipeline_create_info.renderPass = render_pass;
    pipeline_create_info.subpass = 0;

    VkPipeline pipeline { VK_NULL_HANDLE };
    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        warnln("vkCreateGraphicsPipelines failed with VkResult {}", to_underlying(result));
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        vkDestroyShaderModule(device, fragment_shader, nullptr);
        vkDestroyShaderModule(device, vertex_shader, nullptr);
        vkDestroyRenderPass(device, render_pass, nullptr);
        for (auto image_view : swapchain_image_views)
            vkDestroyImageView(device, image_view, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        return Error::from_string_literal("vkCreateGraphicsPipelines failed");
    }

    VkDescriptorSetLayout panel_descriptor_set_layout { VK_NULL_HANDLE };
    VkDescriptorPool panel_descriptor_pool { VK_NULL_HANDLE };
    VkDescriptorSet panel_descriptor_set { VK_NULL_HANDLE };
    VkPipelineLayout panel_pipeline_layout { VK_NULL_HANDLE };
    VkPipeline panel_pipeline { VK_NULL_HANDLE };
    VkShaderModule panel_vertex_shader { VK_NULL_HANDLE };
    VkShaderModule panel_fragment_shader { VK_NULL_HANDLE };

    if ((!panel_vertices.is_empty() || !overlay_vertices.is_empty()) && panel_vertex_shader_bytecode.has_value() && panel_fragment_shader_bytecode.has_value()) {
        auto descriptor_set_layout_binding = VkDescriptorSetLayoutBinding {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info {};
        descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptor_set_layout_create_info.bindingCount = 1;
        descriptor_set_layout_create_info.pBindings = &descriptor_set_layout_binding;

        result = vkCreateDescriptorSetLayout(device, &descriptor_set_layout_create_info, nullptr, &panel_descriptor_set_layout);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateDescriptorSetLayout failed");

        auto panel_push_constant_range = VkPushConstantRange {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(PushConstants),
        };
        VkPipelineLayoutCreateInfo panel_pipeline_layout_create_info {};
        panel_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        panel_pipeline_layout_create_info.setLayoutCount = 1;
        panel_pipeline_layout_create_info.pSetLayouts = &panel_descriptor_set_layout;
        panel_pipeline_layout_create_info.pushConstantRangeCount = 1;
        panel_pipeline_layout_create_info.pPushConstantRanges = &panel_push_constant_range;

        result = vkCreatePipelineLayout(device, &panel_pipeline_layout_create_info, nullptr, &panel_pipeline_layout);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreatePipelineLayout for panel failed");

        panel_vertex_shader = TRY(create_shader_module(device, *panel_vertex_shader_bytecode));
        panel_fragment_shader = TRY(create_shader_module(device, *panel_fragment_shader_bytecode));

        VkPipelineShaderStageCreateInfo panel_vertex_shader_stage {};
        panel_vertex_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        panel_vertex_shader_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        panel_vertex_shader_stage.module = panel_vertex_shader;
        panel_vertex_shader_stage.pName = "main";

        VkPipelineShaderStageCreateInfo panel_fragment_shader_stage {};
        panel_fragment_shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        panel_fragment_shader_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        panel_fragment_shader_stage.module = panel_fragment_shader;
        panel_fragment_shader_stage.pName = "main";

        Array<VkPipelineShaderStageCreateInfo, 2> panel_shader_stages { panel_vertex_shader_stage, panel_fragment_shader_stage };

        VkVertexInputBindingDescription panel_vertex_binding_description {};
        panel_vertex_binding_description.binding = 0;
        panel_vertex_binding_description.stride = sizeof(TexturedVertex);
        panel_vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        Array<VkVertexInputAttributeDescription, 2> panel_vertex_attribute_descriptions;
        panel_vertex_attribute_descriptions[0].location = 0;
        panel_vertex_attribute_descriptions[0].binding = 0;
        panel_vertex_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        panel_vertex_attribute_descriptions[0].offset = __builtin_offsetof(TexturedVertex, position);
        panel_vertex_attribute_descriptions[1].location = 1;
        panel_vertex_attribute_descriptions[1].binding = 0;
        panel_vertex_attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        panel_vertex_attribute_descriptions[1].offset = __builtin_offsetof(TexturedVertex, uv);

        VkPipelineVertexInputStateCreateInfo panel_vertex_input_state {};
        panel_vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        panel_vertex_input_state.vertexBindingDescriptionCount = 1;
        panel_vertex_input_state.pVertexBindingDescriptions = &panel_vertex_binding_description;
        panel_vertex_input_state.vertexAttributeDescriptionCount = panel_vertex_attribute_descriptions.size();
        panel_vertex_input_state.pVertexAttributeDescriptions = panel_vertex_attribute_descriptions.data();

        auto panel_input_assembly_state = input_assembly_state;
        auto panel_viewport_state = viewport_state;
        auto panel_rasterization_state = rasterization_state;
        auto panel_multisample_state = multisample_state;
        auto panel_color_blend_attachment = color_blend_attachment;
        panel_color_blend_attachment.blendEnable = VK_TRUE;
        panel_color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        panel_color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        panel_color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        panel_color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        panel_color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        panel_color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo panel_color_blend_state {};
        panel_color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        panel_color_blend_state.attachmentCount = 1;
        panel_color_blend_state.pAttachments = &panel_color_blend_attachment;

        VkGraphicsPipelineCreateInfo panel_pipeline_create_info {};
        panel_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        panel_pipeline_create_info.stageCount = panel_shader_stages.size();
        panel_pipeline_create_info.pStages = panel_shader_stages.data();
        panel_pipeline_create_info.pVertexInputState = &panel_vertex_input_state;
        panel_pipeline_create_info.pInputAssemblyState = &panel_input_assembly_state;
        panel_pipeline_create_info.pViewportState = &panel_viewport_state;
        panel_pipeline_create_info.pRasterizationState = &panel_rasterization_state;
        panel_pipeline_create_info.pMultisampleState = &panel_multisample_state;
        panel_pipeline_create_info.pColorBlendState = &panel_color_blend_state;
        panel_pipeline_create_info.layout = panel_pipeline_layout;
        panel_pipeline_create_info.renderPass = render_pass;
        panel_pipeline_create_info.subpass = 0;

        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &panel_pipeline_create_info, nullptr, &panel_pipeline);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateGraphicsPipelines for panel failed");
    }

    Vector<VkFramebuffer> framebuffers;
    framebuffers.resize(swapchain_image_count);
    for (u32 i = 0; i < swapchain_image_count; ++i) {
        VkFramebufferCreateInfo framebuffer_create_info {};
        framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_create_info.renderPass = render_pass;
        framebuffer_create_info.attachmentCount = 1;
        framebuffer_create_info.pAttachments = &swapchain_image_views[i];
        framebuffer_create_info.width = extent.width;
        framebuffer_create_info.height = extent.height;
        framebuffer_create_info.layers = 1;
        result = vkCreateFramebuffer(device, &framebuffer_create_info, nullptr, &framebuffers[i]);
        if (result != VK_SUCCESS) {
            warnln("vkCreateFramebuffer failed with VkResult {}", to_underlying(result));
            for (auto framebuffer : framebuffers) {
                if (framebuffer != VK_NULL_HANDLE)
                    vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            vkDestroyShaderModule(device, fragment_shader, nullptr);
            vkDestroyShaderModule(device, vertex_shader, nullptr);
            vkDestroyRenderPass(device, render_pass, nullptr);
            for (auto image_view : swapchain_image_views)
                vkDestroyImageView(device, image_view, nullptr);
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            return Error::from_string_literal("vkCreateFramebuffer failed");
        }
    }

    VkCommandPoolCreateInfo command_pool_info {};
    command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_info.queueFamilyIndex = graphics_queue_family;

    VkCommandPool command_pool { VK_NULL_HANDLE };
    result = vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool);
    if (result != VK_SUCCESS) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        warnln("vkCreateCommandPool failed with VkResult {}", to_underlying(result));
        return Error::from_string_literal("vkCreateCommandPool failed");
    }

    VkCommandBufferAllocateInfo command_buffer_allocate_info {};
    command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_allocate_info.commandPool = command_pool;
    command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_allocate_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer { VK_NULL_HANDLE };
    result = vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &command_buffer);
    if (result != VK_SUCCESS) {
        vkDestroyCommandPool(device, command_pool, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        warnln("vkAllocateCommandBuffers failed with VkResult {}", to_underlying(result));
        return Error::from_string_literal("vkAllocateCommandBuffers failed");
    }

    VkSemaphore image_available { VK_NULL_HANDLE };
    VkSemaphore render_finished { VK_NULL_HANDLE };
    VkFence in_flight { VK_NULL_HANDLE };
    VkSemaphoreCreateInfo semaphore_create_info {};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence_create_info {};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    result = vkCreateSemaphore(device, &semaphore_create_info, nullptr, &image_available);
    if (result == VK_SUCCESS)
        result = vkCreateSemaphore(device, &semaphore_create_info, nullptr, &render_finished);
    if (result == VK_SUCCESS)
        result = vkCreateFence(device, &fence_create_info, nullptr, &in_flight);
    if (result != VK_SUCCESS) {
        if (in_flight != VK_NULL_HANDLE)
            vkDestroyFence(device, in_flight, nullptr);
        if (render_finished != VK_NULL_HANDLE)
            vkDestroySemaphore(device, render_finished, nullptr);
        if (image_available != VK_NULL_HANDLE)
            vkDestroySemaphore(device, image_available, nullptr);
        vkDestroyCommandPool(device, command_pool, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        return Error::from_string_literal("Vulkan sync object creation failed");
    }

    Vector<Vertex> draw_vertices;
    VkBuffer vertex_buffer { VK_NULL_HANDLE };
    VkDeviceMemory vertex_buffer_memory { VK_NULL_HANDLE };
    if (world) {
        MeshLibrary mesh_library;
        Vector<DrawGroup> ignored_groups;
        draw_vertices = build_world_vertices(*world, mesh_library, ignored_groups);
    }
    if (draw_vertices.is_empty())
        draw_vertices = build_probe_vertices();
    TRY(create_vertex_buffer(physical_device, device, draw_vertices, vertex_buffer, vertex_buffer_memory));
    auto push_constants = make_view_projection_constants(extent, camera_state);

    VkBuffer panel_vertex_buffer { VK_NULL_HANDLE };
    VkDeviceMemory panel_vertex_buffer_memory { VK_NULL_HANDLE };
    VkBuffer panel_staging_buffer { VK_NULL_HANDLE };
    VkDeviceMemory panel_staging_buffer_memory { VK_NULL_HANDLE };
    VkImage panel_texture_image { VK_NULL_HANDLE };
    VkDeviceMemory panel_texture_image_memory { VK_NULL_HANDLE };
    VkImageView panel_texture_image_view { VK_NULL_HANDLE };
    VkSampler panel_texture_sampler { VK_NULL_HANDLE };
    VkDescriptorPool overlay_descriptor_pool { VK_NULL_HANDLE };
    VkDescriptorSet overlay_descriptor_set { VK_NULL_HANDLE };
    VkBuffer overlay_vertex_buffer { VK_NULL_HANDLE };
    VkDeviceMemory overlay_vertex_buffer_memory { VK_NULL_HANDLE };
    VkBuffer overlay_staging_buffer { VK_NULL_HANDLE };
    VkDeviceMemory overlay_staging_buffer_memory { VK_NULL_HANDLE };
    VkImage overlay_texture_image { VK_NULL_HANDLE };
    VkDeviceMemory overlay_texture_image_memory { VK_NULL_HANDLE };
    VkImageView overlay_texture_image_view { VK_NULL_HANDLE };
    VkSampler overlay_texture_sampler { VK_NULL_HANDLE };

    if (panel_pipeline != VK_NULL_HANDLE && !panel_vertices.is_empty()) {
        TRY(create_textured_vertex_buffer(physical_device, device, panel_vertices, panel_vertex_buffer, panel_vertex_buffer_memory));

        static constexpr Array<u8, 16> checkerboard_pixels {
            255, 255, 255, 255,
            48, 48, 48, 255,
            48, 48, 48, 255,
            255, 255, 255, 255,
        };

        auto const* texture_pixels = checkerboard_pixels.data();
        size_t texture_pixel_count = checkerboard_pixels.size();
        u32 texture_width = 2;
        u32 texture_height = 2;
        if (panel_pixels && !panel_pixels->is_empty() && panel_width > 0 && panel_height > 0) {
            texture_pixels = panel_pixels->data();
            texture_pixel_count = panel_pixels->size();
            texture_width = panel_width;
            texture_height = panel_height;
        }

        TRY(create_buffer(physical_device, device, texture_pixel_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, panel_staging_buffer, panel_staging_buffer_memory));
        void* mapped_memory = nullptr;
        result = vkMapMemory(device, panel_staging_buffer_memory, 0, texture_pixel_count, 0, &mapped_memory);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkMapMemory for panel staging buffer failed");
        __builtin_memcpy(mapped_memory, texture_pixels, texture_pixel_count);
        vkUnmapMemory(device, panel_staging_buffer_memory);

        TRY(create_image(physical_device, device, texture_width, texture_height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, panel_texture_image, panel_texture_image_memory));

        VkImageViewCreateInfo image_view_create_info {};
        image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.image = panel_texture_image;
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_create_info.subresourceRange.baseMipLevel = 0;
        image_view_create_info.subresourceRange.levelCount = 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount = 1;
        result = vkCreateImageView(device, &image_view_create_info, nullptr, &panel_texture_image_view);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateImageView for panel texture failed");

        VkSamplerCreateInfo sampler_create_info {};
        sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_create_info.magFilter = VK_FILTER_LINEAR;
        sampler_create_info.minFilter = VK_FILTER_LINEAR;
        sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_create_info.maxLod = 1.0f;
        result = vkCreateSampler(device, &sampler_create_info, nullptr, &panel_texture_sampler);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateSampler for panel texture failed");

        VkDescriptorPoolSize descriptor_pool_size {};
        descriptor_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_pool_size.descriptorCount = 1;
        VkDescriptorPoolCreateInfo descriptor_pool_create_info {};
        descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_pool_create_info.poolSizeCount = 1;
        descriptor_pool_create_info.pPoolSizes = &descriptor_pool_size;
        descriptor_pool_create_info.maxSets = 1;
        result = vkCreateDescriptorPool(device, &descriptor_pool_create_info, nullptr, &panel_descriptor_pool);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateDescriptorPool for panel texture failed");

        VkDescriptorSetAllocateInfo descriptor_set_allocate_info {};
        descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptor_set_allocate_info.descriptorPool = panel_descriptor_pool;
        descriptor_set_allocate_info.descriptorSetCount = 1;
        descriptor_set_allocate_info.pSetLayouts = &panel_descriptor_set_layout;
        result = vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, &panel_descriptor_set);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkAllocateDescriptorSets for panel texture failed");
    }

    if (panel_pipeline != VK_NULL_HANDLE && !overlay_vertices.is_empty()) {
        TRY(create_textured_vertex_buffer(physical_device, device, overlay_vertices, overlay_vertex_buffer, overlay_vertex_buffer_memory));

        VERIFY(overlay_view);
        auto const* texture_pixels = overlay_view->pixels.data();
        size_t texture_pixel_count = overlay_view->pixels.size();
        u32 texture_width = overlay_view->width;
        u32 texture_height = overlay_view->height;

        TRY(create_buffer(physical_device, device, texture_pixel_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, overlay_staging_buffer, overlay_staging_buffer_memory));
        void* mapped_memory = nullptr;
        result = vkMapMemory(device, overlay_staging_buffer_memory, 0, texture_pixel_count, 0, &mapped_memory);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkMapMemory for overlay staging buffer failed");
        __builtin_memcpy(mapped_memory, texture_pixels, texture_pixel_count);
        vkUnmapMemory(device, overlay_staging_buffer_memory);

        TRY(create_image(physical_device, device, texture_width, texture_height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, overlay_texture_image, overlay_texture_image_memory));

        VkImageViewCreateInfo image_view_create_info {};
        image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.image = overlay_texture_image;
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_create_info.subresourceRange.baseMipLevel = 0;
        image_view_create_info.subresourceRange.levelCount = 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount = 1;
        result = vkCreateImageView(device, &image_view_create_info, nullptr, &overlay_texture_image_view);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateImageView for overlay texture failed");

        VkSamplerCreateInfo sampler_create_info {};
        sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_create_info.magFilter = VK_FILTER_LINEAR;
        sampler_create_info.minFilter = VK_FILTER_LINEAR;
        sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_create_info.maxLod = 1.0f;
        result = vkCreateSampler(device, &sampler_create_info, nullptr, &overlay_texture_sampler);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateSampler for overlay texture failed");

        VkDescriptorPoolSize descriptor_pool_size {};
        descriptor_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_pool_size.descriptorCount = 1;
        VkDescriptorPoolCreateInfo descriptor_pool_create_info {};
        descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptor_pool_create_info.poolSizeCount = 1;
        descriptor_pool_create_info.pPoolSizes = &descriptor_pool_size;
        descriptor_pool_create_info.maxSets = 1;
        result = vkCreateDescriptorPool(device, &descriptor_pool_create_info, nullptr, &overlay_descriptor_pool);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkCreateDescriptorPool for overlay texture failed");

        VkDescriptorSetAllocateInfo descriptor_set_allocate_info {};
        descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptor_set_allocate_info.descriptorPool = overlay_descriptor_pool;
        descriptor_set_allocate_info.descriptorSetCount = 1;
        descriptor_set_allocate_info.pSetLayouts = &panel_descriptor_set_layout;
        result = vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, &overlay_descriptor_set);
        if (result != VK_SUCCESS)
            return Error::from_string_literal("vkAllocateDescriptorSets for overlay texture failed");
    }

    u32 image_index = 0;
    result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available, VK_NULL_HANDLE, &image_index);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        warnln("vkAcquireNextImageKHR failed with VkResult {}", to_underlying(result));
        vkDestroyFence(device, in_flight, nullptr);
        vkDestroySemaphore(device, render_finished, nullptr);
        vkDestroySemaphore(device, image_available, nullptr);
        vkDestroyCommandPool(device, command_pool, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        return Error::from_string_literal("vkAcquireNextImageKHR failed");
    }

    VkCommandBufferBeginInfo command_buffer_begin_info {};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    if (panel_texture_image != VK_NULL_HANDLE) {
        u32 texture_width = (panel_pixels && !panel_pixels->is_empty() && panel_width > 0) ? panel_width : 2;
        u32 texture_height = (panel_pixels && !panel_pixels->is_empty() && panel_height > 0) ? panel_height : 2;
        transition_image_layout(command_buffer, panel_texture_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy_buffer_to_image(command_buffer, panel_staging_buffer, panel_texture_image, texture_width, texture_height);
        transition_image_layout(command_buffer, panel_texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkDescriptorImageInfo descriptor_image_info {};
        descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor_image_info.imageView = panel_texture_image_view;
        descriptor_image_info.sampler = panel_texture_sampler;

        VkWriteDescriptorSet descriptor_write {};
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = panel_descriptor_set;
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pImageInfo = &descriptor_image_info;
        vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, nullptr);
    }

    if (overlay_texture_image != VK_NULL_HANDLE) {
        VERIFY(overlay_view);
        transition_image_layout(command_buffer, overlay_texture_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy_buffer_to_image(command_buffer, overlay_staging_buffer, overlay_texture_image, overlay_view->width, overlay_view->height);
        transition_image_layout(command_buffer, overlay_texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkDescriptorImageInfo descriptor_image_info {};
        descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor_image_info.imageView = overlay_texture_image_view;
        descriptor_image_info.sampler = overlay_texture_sampler;

        VkWriteDescriptorSet descriptor_write {};
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = overlay_descriptor_set;
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pImageInfo = &descriptor_image_info;
        vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, nullptr);
    }

    VkClearValue clear_value {};
    clear_value.color = { { 0.02f, 0.08f, 0.10f, 1.0f } };

    VkRenderPassBeginInfo render_pass_begin_info {};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = render_pass;
    render_pass_begin_info.framebuffer = framebuffers[image_index];
    render_pass_begin_info.renderArea.offset = { 0, 0 };
    render_pass_begin_info.renderArea.extent = extent;
    render_pass_begin_info.clearValueCount = 1;
    render_pass_begin_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);
    VkDeviceSize vertex_buffer_offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &vertex_buffer_offset);
    vkCmdDraw(command_buffer, draw_vertices.size(), 1, 0, 0);

    if (panel_pipeline != VK_NULL_HANDLE && panel_vertex_buffer != VK_NULL_HANDLE && panel_descriptor_set != VK_NULL_HANDLE) {
        VkDeviceSize panel_vertex_buffer_offset = 0;
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, panel_pipeline);
        vkCmdPushConstants(command_buffer, panel_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, panel_pipeline_layout, 0, 1, &panel_descriptor_set, 0, nullptr);
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &panel_vertex_buffer, &panel_vertex_buffer_offset);
        vkCmdDraw(command_buffer, panel_vertices.size(), 1, 0, 0);
    }
    if (panel_pipeline != VK_NULL_HANDLE && overlay_vertex_buffer != VK_NULL_HANDLE && overlay_descriptor_set != VK_NULL_HANDLE) {
        auto overlay_constants = make_identity_constants();
        VkDeviceSize overlay_vertex_buffer_offset = 0;
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, panel_pipeline);
        vkCmdPushConstants(command_buffer, panel_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &overlay_constants);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, panel_pipeline_layout, 0, 1, &overlay_descriptor_set, 0, nullptr);
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &overlay_vertex_buffer, &overlay_vertex_buffer_offset);
        vkCmdDraw(command_buffer, overlay_vertices.size(), 1, 0, 0);
    }
    vkCmdEndRenderPass(command_buffer);

    vkEndCommandBuffer(command_buffer);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_available;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_finished;

    result = vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight);
    if (result != VK_SUCCESS) {
        warnln("vkQueueSubmit failed with VkResult {}", to_underlying(result));
        vkDestroyFence(device, in_flight, nullptr);
        vkDestroySemaphore(device, render_finished, nullptr);
        vkDestroySemaphore(device, image_available, nullptr);
        vkDestroyCommandPool(device, command_pool, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        return Error::from_string_literal("vkQueueSubmit failed");
    }

    vkWaitForFences(device, 1, &in_flight, VK_TRUE, UINT64_MAX);

    VkPresentInfoKHR present_info {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &image_index;
    result = vkQueuePresentKHR(graphics_queue, &present_info);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        warnln("vkQueuePresentKHR returned VkResult {}", to_underlying(result));

    vkQueueWaitIdle(graphics_queue);
    vkDeviceWaitIdle(device);
    if (panel_descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, panel_descriptor_pool, nullptr);
    if (overlay_descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, overlay_descriptor_pool, nullptr);
    if (panel_texture_sampler != VK_NULL_HANDLE)
        vkDestroySampler(device, panel_texture_sampler, nullptr);
    if (overlay_texture_sampler != VK_NULL_HANDLE)
        vkDestroySampler(device, overlay_texture_sampler, nullptr);
    if (panel_texture_image_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, panel_texture_image_view, nullptr);
    if (overlay_texture_image_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, overlay_texture_image_view, nullptr);
    if (panel_texture_image != VK_NULL_HANDLE)
        vkDestroyImage(device, panel_texture_image, nullptr);
    if (overlay_texture_image != VK_NULL_HANDLE)
        vkDestroyImage(device, overlay_texture_image, nullptr);
    if (panel_texture_image_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, panel_texture_image_memory, nullptr);
    if (overlay_texture_image_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, overlay_texture_image_memory, nullptr);
    if (panel_staging_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, panel_staging_buffer, nullptr);
    if (overlay_staging_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, overlay_staging_buffer, nullptr);
    if (panel_staging_buffer_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, panel_staging_buffer_memory, nullptr);
    if (overlay_staging_buffer_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, overlay_staging_buffer_memory, nullptr);
    if (panel_vertex_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, panel_vertex_buffer, nullptr);
    if (overlay_vertex_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, overlay_vertex_buffer, nullptr);
    if (panel_vertex_buffer_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, panel_vertex_buffer_memory, nullptr);
    if (overlay_vertex_buffer_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, overlay_vertex_buffer_memory, nullptr);
    if (panel_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, panel_pipeline, nullptr);
    if (panel_pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, panel_pipeline_layout, nullptr);
    if (panel_descriptor_set_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, panel_descriptor_set_layout, nullptr);
    if (panel_fragment_shader != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, panel_fragment_shader, nullptr);
    if (panel_vertex_shader != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, panel_vertex_shader, nullptr);
    if (vertex_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, vertex_buffer, nullptr);
    if (vertex_buffer_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, vertex_buffer_memory, nullptr);
    for (auto framebuffer : framebuffers)
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyShaderModule(device, fragment_shader, nullptr);
    vkDestroyShaderModule(device, vertex_shader, nullptr);
    vkDestroyRenderPass(device, render_pass, nullptr);
    for (auto image_view : swapchain_image_views)
        vkDestroyImageView(device, image_view, nullptr);
    vkDestroyFence(device, in_flight, nullptr);
    vkDestroySemaphore(device, render_finished, nullptr);
    vkDestroySemaphore(device, image_available, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);

    if (!world)
        outln("  Vulkan triangle presented ({}x{})", extent.width, extent.height);
    return {};
}

static ErrorOr<void> draw_frame(VulkanRenderer::Impl& impl, VulkanRenderer::CameraState const& camera_state, World const* world = nullptr, Vector<u8> const* panel_pixels = nullptr, u32 panel_width = 0, u32 panel_height = 0, VulkanRenderer::OverlayView const* overlay_view = nullptr)
{
    TRY(ensure_swapchain_resources(impl));

    // Initialize TextureLibrary once the device is ready (uses the 1-binding world layout for its internal sets).
    if (!impl.texture_library.has_value() && impl.world_descriptor_set_layout != VK_NULL_HANDLE)
        impl.texture_library.emplace(impl.physical_device, impl.device, impl.command_pool, impl.graphics_queue, impl.world_descriptor_set_layout);

    // Build all CPU vertex data first — no GPU uploads yet.
    Vector<Vertex> world_vertices;
    Vector<float> world_instance_data;
    Vector<VkDrawIndirectCommand> indirect_commands; // one per draw group; uploaded alongside vertex data
    Vector<TexturedVertex> panel_vertices;
    Vector<TexturedVertex> overlay_vertices;
    bool world_vertices_dirty = false;   // full vertex + instance + indirect rebuild needed
    bool world_instances_dirty = false;  // instance buffer + AABB only (transform-only update)
    bool panel_vertices_dirty = false;

    // Identity matrix instance for probe/empty-world fallback — the pipeline always expects binding 1.
    static constexpr Array<float, 16> identity_instance {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    if (!world) {
        world_vertices = build_probe_vertices();
        for (auto f : identity_instance)
            world_instance_data.append(f);
        world_vertices_dirty = true;
    } else if (world->layout_dirty() || impl.world_vertex_buffer.buffer == VK_NULL_HANDLE) {
        // Structure changed (spawn/destroy/mesh/material) — full vertex buffer rebuild.
        impl.world_draw_groups.clear();
        build_world_instanced(*world, *impl.mesh_library, world_vertices, world_instance_data, impl.world_draw_groups);
        // Build indirect draw commands — one entry per draw group.
        // The GPU frustum culling compute shader (step 3) will later overwrite instanceCount for culled groups.
        indirect_commands.ensure_capacity(impl.world_draw_groups.size());
        for (auto const& group : impl.world_draw_groups) {
            indirect_commands.append(VkDrawIndirectCommand {
                .vertexCount   = group.vertex_count,
                .instanceCount = group.instance_count,
                .firstVertex   = group.first_vertex,
                .firstInstance = group.first_instance,
            });
        }
        if (world_vertices.is_empty()) {
            world_vertices = build_probe_vertices();
            world_instance_data.clear();
            for (auto f : identity_instance)
                world_instance_data.append(f);
            impl.world_draw_groups.clear();
        }
        world_vertices_dirty = true;

        panel_vertices = build_panel_vertices(*world);
        panel_vertices_dirty = true;
    } else if (world->transform_dirty() && !impl.world_draw_groups.is_empty()) {
        // Only transforms changed — rebuild instance matrices and AABBs, keep vertex buffer as-is.
        rebuild_transforms_only(*world, impl.world_draw_groups, world_instance_data);
        world_instances_dirty = true;
    }

    if (overlay_view && !overlay_view->pixels.is_empty() && overlay_view->width > 0 && overlay_view->height > 0)
        overlay_vertices = build_overlay_vertices(*overlay_view);
    else if (impl.overlay_bitmap_view.bitmap && impl.overlay_bitmap_view.width > 0 && impl.overlay_bitmap_view.height > 0)
        overlay_vertices = build_overlay_vertices(VulkanRenderer::OverlayView { {}, impl.overlay_bitmap_view.width, impl.overlay_bitmap_view.height });

    auto world_push_constants = make_view_projection_constants(impl.extent, camera_state);
    auto panel_push_constants = make_view_projection_constants(impl.extent, camera_state);

    // Build the scene light UBO from the current scene light and camera state.
    SceneLightUBO light_ubo {};
    light_ubo.ambient[0] = impl.scene_light.ambient_rgb[0];
    light_ubo.ambient[1] = impl.scene_light.ambient_rgb[1];
    light_ubo.ambient[2] = impl.scene_light.ambient_rgb[2];
    light_ubo.ambient[3] = impl.scene_light.ambient_intensity;
    light_ubo.sun_direction[0] = impl.scene_light.light_to_xyz[0];
    light_ubo.sun_direction[1] = impl.scene_light.light_to_xyz[1];
    light_ubo.sun_direction[2] = impl.scene_light.light_to_xyz[2];
    light_ubo.sun_direction[3] = 0.0f;
    light_ubo.sun_color[0] = impl.scene_light.light_rgb[0];
    light_ubo.sun_color[1] = impl.scene_light.light_rgb[1];
    light_ubo.sun_color[2] = impl.scene_light.light_rgb[2];
    light_ubo.sun_color[3] = impl.scene_light.light_intensity;
    light_ubo.eye_position[0] = camera_state.position[0];
    light_ubo.eye_position[1] = camera_state.position[1];
    light_ubo.eye_position[2] = camera_state.position[2];
    light_ubo.eye_position[3] = 0.0f;
    auto n_lights = impl.scene_light.point_light_count < VulkanRenderer::SceneLightData::MaxPointLights
        ? impl.scene_light.point_light_count
        : VulkanRenderer::SceneLightData::MaxPointLights;
    light_ubo.light_params[0] = n_lights;
    for (int i = 0; i < n_lights; ++i) {
        auto const& pl = impl.scene_light.point_lights[i];
        light_ubo.point_lights[i].position_radius[0] = pl.position[0];
        light_ubo.point_lights[i].position_radius[1] = pl.position[1];
        light_ubo.point_lights[i].position_radius[2] = pl.position[2];
        light_ubo.point_lights[i].position_radius[3] = pl.radius;
        light_ubo.point_lights[i].color_intensity[0] = pl.color[0];
        light_ubo.point_lights[i].color_intensity[1] = pl.color[1];
        light_ubo.point_lights[i].color_intensity[2] = pl.color[2];
        light_ubo.point_lights[i].color_intensity[3] = pl.intensity;
        light_ubo.point_lights[i].spot_direction[0] = pl.direction[0];
        light_ubo.point_lights[i].spot_direction[1] = pl.direction[1];
        light_ubo.point_lights[i].spot_direction[2] = pl.direction[2];
        light_ubo.point_lights[i].spot_direction[3] = 0.0f;
        light_ubo.point_lights[i].spot_cone[0] = pl.cone_inner_cos;
        light_ubo.point_lights[i].spot_cone[1] = pl.cone_outer_cos;
        light_ubo.point_lights[i].spot_cone[2] = static_cast<float>(pl.type);
        light_ubo.point_lights[i].spot_cone[3] = 0.0f;
    }

    // Wait for previous frame to finish before touching GPU resources.
    vkWaitForFences(impl.device, 1, &impl.in_flight, VK_TRUE, UINT64_MAX);
    vkResetFences(impl.device, 1, &impl.in_flight);
    vkResetCommandPool(impl.device, impl.command_pool, 0);

    // Upload scene light data to the UBO (safe after fence wait).
    if (impl.light_ubo_buffer.buffer != VK_NULL_HANDLE) {
        void* mapped = nullptr;
        vkMapMemory(impl.device, impl.light_ubo_buffer.memory, 0, sizeof(SceneLightUBO), 0, &mapped);
        memcpy(mapped, &light_ubo, sizeof(SceneLightUBO));
        vkUnmapMemory(impl.device, impl.light_ubo_buffer.memory);
    }

    // Upload to device-local buffers now that the command pool is free.
    if (world_vertices_dirty) {
        TRY(upload_vertices_device_local(impl, world_vertices, impl.world_vertex_buffer));
        impl.world_vertex_count = static_cast<u32>(world_vertices.size());
        TRY(upload_vertices_device_local(impl, world_instance_data, impl.world_instance_buffer));
        impl.world_instance_count = static_cast<u32>(world_instance_data.size()) / 16;
        // Upload indirect draw commands to both ref buffer (never touched by GPU) and draw buffer (written by cull compute).
        TRY(upload_buffer_device_local(impl, indirect_commands, impl.indirect_ref_buffer,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
        TRY(upload_buffer_device_local(impl, indirect_commands, impl.indirect_draw_buffer,
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
        impl.indirect_draw_count = static_cast<u32>(indirect_commands.size());
    }
    if (world_vertices_dirty || world_instances_dirty) {
        if (world_instances_dirty) {
            // Transform-only path: re-upload instance buffer in-place (no vertex or indirect rebuild).
            TRY(upload_vertices_device_local(impl, world_instance_data, impl.world_instance_buffer));
            impl.world_instance_count = static_cast<u32>(world_instance_data.size()) / 16;
        }

        // Build and upload per-group world-space AABBs (GroupAABB = vec4 min + vec4 max = 32 bytes).
        struct GroupAABBGPU { float min[4]; float max[4]; }; // w components are padding
        Vector<GroupAABBGPU> aabb_data;
        aabb_data.ensure_capacity(impl.world_draw_groups.size());
        for (auto const& group : impl.world_draw_groups) {
            aabb_data.append(GroupAABBGPU {
                { group.aabb_world_min[0], group.aabb_world_min[1], group.aabb_world_min[2], 0.0f },
                { group.aabb_world_max[0], group.aabb_world_max[1], group.aabb_world_max[2], 0.0f },
            });
        }
        TRY(upload_buffer_device_local(impl, aabb_data, impl.aabb_world_buffer,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT));

        // Update the culling descriptor set to point at the new buffer addresses.
        // (Always needed after a full rebuild; safe to re-bind on transform-only too since
        //  the buffer objects themselves are reused — only contents change.)
        if (impl.cull_descriptor_set != VK_NULL_HANDLE
            && impl.indirect_ref_buffer.buffer != VK_NULL_HANDLE
            && impl.indirect_draw_buffer.buffer != VK_NULL_HANDLE
            && impl.aabb_world_buffer.buffer != VK_NULL_HANDLE) {
            Array<VkDescriptorBufferInfo, 3> buf_infos {};
            buf_infos[0] = { impl.indirect_ref_buffer.buffer,  0, VK_WHOLE_SIZE };
            buf_infos[1] = { impl.indirect_draw_buffer.buffer, 0, VK_WHOLE_SIZE };
            buf_infos[2] = { impl.aabb_world_buffer.buffer,    0, VK_WHOLE_SIZE };
            Array<VkWriteDescriptorSet, 3> writes {};
            for (u32 b = 0; b < 3; ++b) {
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstSet = impl.cull_descriptor_set;
                writes[b].dstBinding = b;
                writes[b].descriptorCount = 1;
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[b].pBufferInfo = &buf_infos[b];
            }
            vkUpdateDescriptorSets(impl.device, 3, writes.data(), 0, nullptr);
        }
    }
    if (panel_vertices_dirty) {
        if (!panel_vertices.is_empty()) {
            TRY(upload_vertices_device_local(impl, panel_vertices, impl.panel_vertex_buffer));
            impl.panel_vertex_count = static_cast<u32>(panel_vertices.size());
        } else {
            destroy_buffer_resource(impl.device, impl.panel_vertex_buffer);
            impl.panel_vertex_count = 0;
        }
    }
    if (!overlay_vertices.is_empty()) {
        // Overlay changes every frame — host-visible is fine, no staging overhead.
        TRY(upload_vertices_to_buffer(impl.physical_device, impl.device, overlay_vertices, impl.overlay_vertex_buffer));
        impl.overlay_vertex_count = static_cast<u32>(overlay_vertices.size());
    } else {
        destroy_buffer_resource(impl.device, impl.overlay_vertex_buffer);
        impl.overlay_vertex_count = 0;
    }

    u32 image_index = 0;
    auto result = vkAcquireNextImageKHR(impl.device, impl.swapchain, UINT64_MAX, impl.image_available, VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        impl.swapchain_dirty = true;
        return {};
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        return Error::from_string_literal("vkAcquireNextImageKHR failed");
    if (result == VK_SUBOPTIMAL_KHR)
        impl.swapchain_dirty = true;

    VkCommandBufferBeginInfo command_buffer_begin_info {};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(impl.command_buffer, &command_buffer_begin_info);

    VkBuffer panel_staging_buffer { VK_NULL_HANDLE };
    VkDeviceMemory panel_staging_buffer_memory { VK_NULL_HANDLE };
    if (impl.panel_pipeline != VK_NULL_HANDLE && impl.panel_vertex_count > 0) {
        static constexpr Array<u8, 16> checkerboard_pixels {
            255, 255, 255, 255,
            48, 48, 48, 255,
            48, 48, 48, 255,
            255, 255, 255, 255,
        };

        auto const* texture_pixels = checkerboard_pixels.data();
        size_t texture_pixel_count = checkerboard_pixels.size();
        u32 texture_width = 2;
        u32 texture_height = 2;
        auto panel_bitmap = impl.panel_bitmap_view.bitmap;
        if (panel_pixels && !panel_pixels->is_empty() && panel_width > 0 && panel_height > 0) {
            texture_pixels = panel_pixels->data();
            texture_pixel_count = panel_pixels->size();
            texture_width = panel_width;
            texture_height = panel_height;
        }
        if (panel_bitmap && impl.panel_bitmap_view.width > 0 && impl.panel_bitmap_view.height > 0) {
            texture_width = impl.panel_bitmap_view.width;
            texture_height = impl.panel_bitmap_view.height;
            texture_pixel_count = static_cast<size_t>(texture_width) * texture_height * 4;
        }

        TRY(ensure_texture_resource(impl, impl.panel_texture, texture_width, texture_height));
        if (impl.panel_texture_dirty || !impl.panel_texture.has_uploaded_contents) {
            TRY(create_buffer(impl.physical_device, impl.device, texture_pixel_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, panel_staging_buffer, panel_staging_buffer_memory));
            void* mapped_memory = nullptr;
            auto result = vkMapMemory(impl.device, panel_staging_buffer_memory, 0, texture_pixel_count, 0, &mapped_memory);
            if (result != VK_SUCCESS)
                return Error::from_string_literal("vkMapMemory for panel staging buffer failed");
            if (panel_bitmap)
                TRY(copy_bitmap_to_mapped_staging(*panel_bitmap, mapped_memory, texture_width, texture_height));
            else
                __builtin_memcpy(mapped_memory, texture_pixels, texture_pixel_count);
            vkUnmapMemory(impl.device, panel_staging_buffer_memory);

            transition_image_layout(impl.command_buffer, impl.panel_texture.image, impl.panel_texture.has_uploaded_contents ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            copy_buffer_to_image(impl.command_buffer, panel_staging_buffer, impl.panel_texture.image, texture_width, texture_height);
            transition_image_layout(impl.command_buffer, impl.panel_texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            impl.panel_texture.has_uploaded_contents = true;
            impl.panel_texture_dirty = false;
        }
    }

    VkBuffer overlay_staging_buffer { VK_NULL_HANDLE };
    VkDeviceMemory overlay_staging_buffer_memory { VK_NULL_HANDLE };
    if (impl.panel_pipeline != VK_NULL_HANDLE && !overlay_vertices.is_empty()) {
        auto texture_width = overlay_view ? overlay_view->width : 0;
        auto texture_height = overlay_view ? overlay_view->height : 0;
        auto const* texture_pixels = overlay_view ? overlay_view->pixels.data() : nullptr;
        size_t texture_pixel_count = overlay_view ? overlay_view->pixels.size() : 0;
        auto overlay_bitmap = impl.overlay_bitmap_view.bitmap;
        if (overlay_bitmap && impl.overlay_bitmap_view.width > 0 && impl.overlay_bitmap_view.height > 0) {
            texture_width = impl.overlay_bitmap_view.width;
            texture_height = impl.overlay_bitmap_view.height;
            texture_pixel_count = static_cast<size_t>(texture_width) * texture_height * 4;
        }
        TRY(ensure_texture_resource(impl, impl.overlay_texture, texture_width, texture_height));
        if (impl.overlay_texture_dirty || !impl.overlay_texture.has_uploaded_contents) {
            TRY(create_buffer(impl.physical_device, impl.device, texture_pixel_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, overlay_staging_buffer, overlay_staging_buffer_memory));
            void* mapped_memory = nullptr;
            auto result = vkMapMemory(impl.device, overlay_staging_buffer_memory, 0, texture_pixel_count, 0, &mapped_memory);
            if (result != VK_SUCCESS)
                return Error::from_string_literal("vkMapMemory for overlay staging buffer failed");
            if (overlay_bitmap)
                TRY(copy_bitmap_to_mapped_staging(*overlay_bitmap, mapped_memory, texture_width, texture_height));
            else
                __builtin_memcpy(mapped_memory, texture_pixels, texture_pixel_count);
            vkUnmapMemory(impl.device, overlay_staging_buffer_memory);

            transition_image_layout(impl.command_buffer, impl.overlay_texture.image, impl.overlay_texture.has_uploaded_contents ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            copy_buffer_to_image(impl.command_buffer, overlay_staging_buffer, impl.overlay_texture.image, texture_width, texture_height);
            transition_image_layout(impl.command_buffer, impl.overlay_texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            impl.overlay_texture.has_uploaded_contents = true;
            impl.overlay_texture_dirty = false;
        }
    }

    Array<VkClearValue, 2> clear_values {};
    clear_values[0].color = { { 0.02f, 0.08f, 0.10f, 1.0f } };
    clear_values[1].depthStencil = { 0.0f, 0 };
    VkRenderPassBeginInfo render_pass_begin_info {};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = impl.render_pass;
    render_pass_begin_info.framebuffer = impl.framebuffers[image_index];
    render_pass_begin_info.renderArea.offset = { 0, 0 };
    render_pass_begin_info.renderArea.extent = impl.extent;
    render_pass_begin_info.clearValueCount = clear_values.size();
    render_pass_begin_info.pClearValues = clear_values.data();

    // Ensure a default 1x1 white albedo texture (also used as fallback for metallic_roughness and AO slots).
    if (impl.albedo_texture.image == VK_NULL_HANDLE) {
        TRY(ensure_texture_resource(impl, impl.albedo_texture, 1, 1));
        // Upload a single white pixel.
        static constexpr Array<u8, 4> white_pixel { 255, 255, 255, 255 };
        VkBuffer staging { VK_NULL_HANDLE };
        VkDeviceMemory staging_mem { VK_NULL_HANDLE };
        TRY(create_buffer(impl.physical_device, impl.device, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, staging_mem));
        void* mapped = nullptr;
        vkMapMemory(impl.device, staging_mem, 0, 4, 0, &mapped);
        __builtin_memcpy(mapped, white_pixel.data(), 4);
        vkUnmapMemory(impl.device, staging_mem);
        // One-shot command to upload.
        VkCommandBufferAllocateInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool = impl.command_pool;
        ai.commandBufferCount = 1;
        VkCommandBuffer upload_cmd { VK_NULL_HANDLE };
        vkAllocateCommandBuffers(impl.device, &ai, &upload_cmd);
        VkCommandBufferBeginInfo bi {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(upload_cmd, &bi);
        transition_image_layout(upload_cmd, impl.albedo_texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy_buffer_to_image(upload_cmd, staging, impl.albedo_texture.image, 1, 1);
        transition_image_layout(upload_cmd, impl.albedo_texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vkEndCommandBuffer(upload_cmd);
        VkSubmitInfo si {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &upload_cmd;
        vkQueueSubmit(impl.graphics_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(impl.graphics_queue);
        vkFreeCommandBuffers(impl.device, impl.command_pool, 1, &upload_cmd);
        vkDestroyBuffer(impl.device, staging, nullptr);
        vkFreeMemory(impl.device, staging_mem, nullptr);
        impl.albedo_texture.has_uploaded_contents = true;
    }

    // Ensure a default 1x1 flat normal texture (tangent-space (0,0,1) = RGBA 128,128,255,255).
    if (impl.flat_normal_texture.image == VK_NULL_HANDLE) {
        TRY(ensure_texture_resource(impl, impl.flat_normal_texture, 1, 1));
        static constexpr Array<u8, 4> flat_normal_pixel { 128, 128, 255, 255 };
        VkBuffer staging { VK_NULL_HANDLE };
        VkDeviceMemory staging_mem { VK_NULL_HANDLE };
        TRY(create_buffer(impl.physical_device, impl.device, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, staging_mem));
        void* mapped = nullptr;
        vkMapMemory(impl.device, staging_mem, 0, 4, 0, &mapped);
        __builtin_memcpy(mapped, flat_normal_pixel.data(), 4);
        vkUnmapMemory(impl.device, staging_mem);
        VkCommandBufferAllocateInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool = impl.command_pool;
        ai.commandBufferCount = 1;
        VkCommandBuffer upload_cmd { VK_NULL_HANDLE };
        vkAllocateCommandBuffers(impl.device, &ai, &upload_cmd);
        VkCommandBufferBeginInfo bi {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(upload_cmd, &bi);
        transition_image_layout(upload_cmd, impl.flat_normal_texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy_buffer_to_image(upload_cmd, staging, impl.flat_normal_texture.image, 1, 1);
        transition_image_layout(upload_cmd, impl.flat_normal_texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vkEndCommandBuffer(upload_cmd);
        VkSubmitInfo si {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &upload_cmd;
        vkQueueSubmit(impl.graphics_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(impl.graphics_queue);
        vkFreeCommandBuffers(impl.device, impl.command_pool, 1, &upload_cmd);
        vkDestroyBuffer(impl.device, staging, nullptr);
        vkFreeMemory(impl.device, staging_mem, nullptr);
        impl.flat_normal_texture.has_uploaded_contents = true;
    }

    // Ensure a 1x1 black fallback texture for the emissive slot (no emission).
    if (impl.fallback_black_texture.image == VK_NULL_HANDLE) {
        TRY(ensure_texture_resource(impl, impl.fallback_black_texture, 1, 1));
        static constexpr Array<u8, 4> black_pixel { 0, 0, 0, 255 };
        VkBuffer staging { VK_NULL_HANDLE };
        VkDeviceMemory staging_mem { VK_NULL_HANDLE };
        TRY(create_buffer(impl.physical_device, impl.device, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, staging_mem));
        void* mapped = nullptr;
        vkMapMemory(impl.device, staging_mem, 0, 4, 0, &mapped);
        __builtin_memcpy(mapped, black_pixel.data(), 4);
        vkUnmapMemory(impl.device, staging_mem);
        VkCommandBufferAllocateInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool = impl.command_pool;
        ai.commandBufferCount = 1;
        VkCommandBuffer upload_cmd { VK_NULL_HANDLE };
        vkAllocateCommandBuffers(impl.device, &ai, &upload_cmd);
        VkCommandBufferBeginInfo bi {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(upload_cmd, &bi);
        transition_image_layout(upload_cmd, impl.fallback_black_texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy_buffer_to_image(upload_cmd, staging, impl.fallback_black_texture.image, 1, 1);
        transition_image_layout(upload_cmd, impl.fallback_black_texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vkEndCommandBuffer(upload_cmd);
        VkSubmitInfo si {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &upload_cmd;
        vkQueueSubmit(impl.graphics_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(impl.graphics_queue);
        vkFreeCommandBuffers(impl.device, impl.command_pool, 1, &upload_cmd);
        vkDestroyBuffer(impl.device, staging, nullptr);
        vkFreeMemory(impl.device, staging_mem, nullptr);
        impl.fallback_black_texture.has_uploaded_contents = true;
    }

    // Rebuild per-group PBR descriptor sets when geometry changed or they haven't been built yet.
    bool const fallbacks_ready = impl.albedo_texture.image != VK_NULL_HANDLE
        && impl.flat_normal_texture.image != VK_NULL_HANDLE
        && impl.fallback_black_texture.image != VK_NULL_HANDLE;
    bool const pbr_sets_stale = world_vertices_dirty || impl.pbr_descriptor_sets.is_empty();

    if (fallbacks_ready && pbr_sets_stale && impl.pbr_descriptor_set_layout != VK_NULL_HANDLE) {
        // Destroy old pool (implicitly frees all sets).
        if (impl.pbr_descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(impl.device, impl.pbr_descriptor_pool, nullptr);
            impl.pbr_descriptor_pool = VK_NULL_HANDLE;
            impl.pbr_descriptor_sets.clear();
        }

        auto n_sets = impl.world_draw_groups.size() + 1; // +1 for probe/empty-world fallback
        VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<u32>(5 * n_sets) };
        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = static_cast<u32>(n_sets);
        VkResult pool_result = vkCreateDescriptorPool(impl.device, &pool_info, nullptr, &impl.pbr_descriptor_pool);
        if (pool_result != VK_SUCCESS)
            impl.pbr_descriptor_pool = VK_NULL_HANDLE;

        struct TexRef { VkImageView view; VkSampler sampler; };

        // Helper: resolve one texture slot to (view, sampler); falls back to the given default resource.
        // Handles both VFS-path textures (external .gltf) and embedded GLB byte blobs.
        auto resolve_slot = [&](MaterialTextureSlot const* slot, VulkanRenderer::Impl::TextureResource const& fallback) -> TexRef
        {
            if (!slot || !slot->is_set())
                return { fallback.image_view, fallback.sampler };

            if (!slot->path.is_empty() && impl.texture_library.has_value() && impl.file_system) {
                if (auto* e = impl.texture_library->resolve(*impl.file_system, slot->path))
                    return { e->image_view, e->sampler };
            }

            if (!slot->data.is_empty() && impl.texture_library.has_value()) {
                // Cache key: the heap address of the embedded byte data.
                // MaterialTextureSlot::data is a Vector<u8> whose heap allocation is stable
                // for the lifetime of MaterialLibrary's cache entry (pointer survives HashMap rehash).
                auto key = MUST(String::formatted("embedded://{:p}", (void const*)slot->data.data()));
                if (auto* e = impl.texture_library->resolve_from_bytes(key, slot->data))
                    return { e->image_view, e->sampler };
            }

            return { fallback.image_view, fallback.sampler };
        };

        // Allocate and write one PBR descriptor set.
        // Returns VK_NULL_HANDLE if the pool is null or allocation fails; caller must guard bind.
        auto alloc_pbr_set = [&](VkImageView v0, VkSampler s0,
                                   VkImageView v1, VkSampler s1,
                                   VkImageView v2, VkSampler s2,
                                   VkImageView v3, VkSampler s3,
                                   VkImageView v4, VkSampler s4) -> VkDescriptorSet
        {
            if (impl.pbr_descriptor_pool == VK_NULL_HANDLE)
                return VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo alloc_info {};
            alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.descriptorPool = impl.pbr_descriptor_pool;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts = &impl.pbr_descriptor_set_layout;
            VkDescriptorSet set { VK_NULL_HANDLE };
            if (vkAllocateDescriptorSets(impl.device, &alloc_info, &set) != VK_SUCCESS)
                return VK_NULL_HANDLE;

            Array<VkDescriptorImageInfo, 5> img_infos {};
            img_infos[0] = { s0, v0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            img_infos[1] = { s1, v1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            img_infos[2] = { s2, v2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            img_infos[3] = { s3, v3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            img_infos[4] = { s4, v4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            Array<VkWriteDescriptorSet, 5> writes {};
            for (u32 b = 0; b < 5; ++b) {
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstSet = set;
                writes[b].dstBinding = b;
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[b].descriptorCount = 1;
                writes[b].pImageInfo = &img_infos[b];
            }
            vkUpdateDescriptorSets(impl.device, 5, writes.data(), 0, nullptr);
            return set;
        };

        auto& fw = impl.albedo_texture;
        auto& fn = impl.flat_normal_texture;
        auto& fb = impl.fallback_black_texture;

        // Index 0: probe/empty-world fallback set (all fallbacks).
        impl.pbr_descriptor_sets.append(alloc_pbr_set(
            fw.image_view, fw.sampler, fn.image_view, fn.sampler,
            fw.image_view, fw.sampler, fb.image_view, fb.sampler,
            fw.image_view, fw.sampler));

        // Indices 1..N: one set per draw group.
        for (auto const& group : impl.world_draw_groups) {
            auto* mat = impl.material_library.resolve(group.material);

            // Albedo: MaterialAsset.albedo > group.material-as-direct-path > white fallback.
            auto albedo = mat && mat->albedo.is_set()
                ? resolve_slot(&mat->albedo, fw)
                : TexRef { fw.image_view, fw.sampler };
            if (!mat && impl.texture_library.has_value() && impl.file_system) {
                if (auto* e = impl.texture_library->resolve(*impl.file_system, group.material))
                    albedo = { e->image_view, e->sampler };
            }

            // Normal: group.normal_map override > MaterialAsset.normal > flat fallback.
            auto normal = TexRef { fn.image_view, fn.sampler };
            if (!group.normal_map.is_empty() && impl.texture_library.has_value() && impl.file_system) {
                if (auto* e = impl.texture_library->resolve(*impl.file_system, group.normal_map))
                    normal = { e->image_view, e->sampler };
            } else if (mat) {
                normal = resolve_slot(&mat->normal, fn);
            }

            // Metallic_roughness, emissive, AO: from MaterialAsset or fallbacks.
            auto mr       = mat ? resolve_slot(&mat->metallic_roughness, fw) : TexRef { fw.image_view, fw.sampler };
            auto emissive = mat ? resolve_slot(&mat->emissive,           fb) : TexRef { fb.image_view, fb.sampler };
            auto ao       = mat ? resolve_slot(&mat->occlusion,          fw) : TexRef { fw.image_view, fw.sampler };

            impl.pbr_descriptor_sets.append(alloc_pbr_set(
                albedo.view, albedo.sampler, normal.view, normal.sampler,
                mr.view, mr.sampler, emissive.view, emissive.sampler,
                ao.view, ao.sampler));
        }
    }

    // ── Frustum culling compute pass ──────────────────────────────────────────────────────────
    // Dispatches before the render pass each frame. Reads per-group world AABBs + reference draw
    // commands; writes live draw commands with instanceCount=0 for groups outside the frustum.
    if (impl.cull_pipeline != VK_NULL_HANDLE
        && impl.cull_descriptor_set != VK_NULL_HANDLE
        && impl.indirect_draw_count > 0
        && !impl.world_draw_groups.is_empty()) {
        struct CullPushConstants {
            float view_proj[16];
            uint32_t group_count;
        };
        CullPushConstants cull_pc {};
        for (int i = 0; i < 16; ++i)
            cull_pc.view_proj[i] = world_push_constants.view_projection[i];
        cull_pc.group_count = impl.indirect_draw_count;

        vkCmdBindPipeline(impl.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, impl.cull_pipeline);
        vkCmdBindDescriptorSets(impl.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            impl.cull_pipeline_layout, 0, 1, &impl.cull_descriptor_set, 0, nullptr);
        vkCmdPushConstants(impl.command_buffer, impl.cull_pipeline_layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPushConstants), &cull_pc);
        uint32_t dispatch_x = (impl.indirect_draw_count + 63u) / 64u;
        vkCmdDispatch(impl.command_buffer, dispatch_x, 1, 1);

        // Barrier: compute SSBO write → indirect draw read.
        VkMemoryBarrier cull_barrier {};
        cull_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        cull_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        cull_barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        vkCmdPipelineBarrier(impl.command_buffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            0, 1, &cull_barrier, 0, nullptr, 0, nullptr);
    }
    // ── End frustum culling compute pass ──────────────────────────────────────────────────────

    vkCmdBeginRenderPass(impl.command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(impl.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.pipeline);
    vkCmdPushConstants(impl.command_buffer, impl.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &world_push_constants);
    VkDeviceSize vertex_buffer_offset = 0;
    VkDeviceSize instance_buffer_offset = 0;
    vkCmdBindVertexBuffers(impl.command_buffer, 0, 1, &impl.world_vertex_buffer.buffer, &vertex_buffer_offset);
    vkCmdBindVertexBuffers(impl.command_buffer, 1, 1, &impl.world_instance_buffer.buffer, &instance_buffer_offset);
    // Bind scene light UBO (set 1) once for all world draws.
    if (impl.light_ubo_descriptor_set != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(impl.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.pipeline_layout, 1, 1, &impl.light_ubo_descriptor_set, 0, nullptr);

    if (impl.world_draw_groups.is_empty() || impl.pbr_descriptor_sets.is_empty()) {
        // Probe triangle / empty world: bind the fallback PBR set (index 0) if available.
        if (!impl.pbr_descriptor_sets.is_empty() && impl.pbr_descriptor_sets[0] != VK_NULL_HANDLE) {
            auto set = impl.pbr_descriptor_sets[0];
            vkCmdBindDescriptorSets(impl.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.pipeline_layout, 0, 1, &set, 0, nullptr);
        }
        MaterialPushConstants default_mat_pc {};
        vkCmdPushConstants(impl.command_buffer, impl.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof(PushConstants), sizeof(MaterialPushConstants), &default_mat_pc);
        vkCmdDraw(impl.command_buffer, impl.world_vertex_count, 1, 0, 0);
    } else {
        // Helper: fill and push MaterialPushConstants from a resolved material (or defaults).
        auto push_mat_pc = [&](MaterialAsset const* mat) {
            MaterialPushConstants mat_pc {};
            if (mat) {
                mat_pc.metallic_factor      = mat->metallic_factor;
                mat_pc.roughness_factor     = mat->roughness_factor;
                mat_pc.emissive_factor[0]   = mat->emissive_factor[0];
                mat_pc.emissive_factor[1]   = mat->emissive_factor[1];
                mat_pc.emissive_factor[2]   = mat->emissive_factor[2];
                mat_pc.alpha_cutoff         = mat->alpha_cutoff;
                mat_pc.alpha_mode           = static_cast<int>(mat->alpha_mode);
                mat_pc.base_color_factor[0] = mat->base_color_factor[0];
                mat_pc.base_color_factor[1] = mat->base_color_factor[1];
                mat_pc.base_color_factor[2] = mat->base_color_factor[2];
                mat_pc.base_color_factor[3] = mat->base_color_factor[3];
            }
            vkCmdPushConstants(impl.command_buffer, impl.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                sizeof(PushConstants), sizeof(MaterialPushConstants), &mat_pc);
        };

        auto draw_group = [&](u32 gi) {
            auto const& group = impl.world_draw_groups[gi];
            u32 set_index = gi + 1;
            if (set_index < impl.pbr_descriptor_sets.size() && impl.pbr_descriptor_sets[set_index] != VK_NULL_HANDLE) {
                auto set = impl.pbr_descriptor_sets[set_index];
                vkCmdBindDescriptorSets(impl.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.pipeline_layout, 0, 1, &set, 0, nullptr);
            }
            push_mat_pc(impl.material_library.resolve(group.material));
            // Use indirect draw so the GPU culling compute shader (step 3) can write instanceCount=0
            // for culled groups without any CPU readback or synchronisation.
            if (impl.indirect_draw_buffer.buffer != VK_NULL_HANDLE && gi < impl.indirect_draw_count) {
                VkDeviceSize offset = static_cast<VkDeviceSize>(gi) * sizeof(VkDrawIndirectCommand);
                vkCmdDrawIndirect(impl.command_buffer, impl.indirect_draw_buffer.buffer, offset, 1, sizeof(VkDrawIndirectCommand));
            } else {
                vkCmdDraw(impl.command_buffer, group.vertex_count, group.instance_count, group.first_vertex, group.first_instance);
            }
        };

        // Pass 1: opaque and clip groups (insertion order, depth writes on).
        // pbr_descriptor_sets[0] = probe fallback, [1..N] = per draw group.
        for (u32 gi = 0; gi < impl.world_draw_groups.size(); ++gi) {
            auto* mat = impl.material_library.resolve(impl.world_draw_groups[gi].material);
            if (mat && mat->alpha_mode == MaterialAsset::AlphaMode::Blend)
                continue;
            draw_group(gi);
        }

        // Pass 2: blend groups, sorted back-to-front by distance from camera.
        // Collect indices of blend groups first.
        Vector<u32> blend_indices;
        for (u32 gi = 0; gi < impl.world_draw_groups.size(); ++gi) {
            auto* mat = impl.material_library.resolve(impl.world_draw_groups[gi].material);
            if (mat && mat->alpha_mode == MaterialAsset::AlphaMode::Blend)
                blend_indices.append(gi);
        }

        if (!blend_indices.is_empty() && impl.blend_pipeline != VK_NULL_HANDLE) {
            // Sort back-to-front by mesh centroid Z (stored in DrawGroup at build time).
            // Smaller (more negative) Z = further from camera in right-handed view space.
            insertion_sort(blend_indices, [&](u32 a, u32 b) {
                return impl.world_draw_groups[a].centroid_z < impl.world_draw_groups[b].centroid_z;
            });

            vkCmdBindPipeline(impl.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.blend_pipeline);
            // Re-bind light UBO since we switched pipelines.
            if (impl.light_ubo_descriptor_set != VK_NULL_HANDLE)
                vkCmdBindDescriptorSets(impl.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.pipeline_layout, 1, 1, &impl.light_ubo_descriptor_set, 0, nullptr);
            for (auto gi : blend_indices)
                draw_group(gi);
        }
    }

    if (impl.panel_pipeline != VK_NULL_HANDLE && impl.panel_vertex_buffer.buffer != VK_NULL_HANDLE && impl.panel_texture.descriptor_set != VK_NULL_HANDLE && impl.panel_vertex_count > 0) {
        VkDeviceSize panel_vertex_buffer_offset = 0;
        vkCmdBindPipeline(impl.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.panel_pipeline);
        vkCmdPushConstants(impl.command_buffer, impl.panel_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &panel_push_constants);
        vkCmdBindDescriptorSets(impl.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.panel_pipeline_layout, 0, 1, &impl.panel_texture.descriptor_set, 0, nullptr);
        vkCmdBindVertexBuffers(impl.command_buffer, 0, 1, &impl.panel_vertex_buffer.buffer, &panel_vertex_buffer_offset);
        vkCmdDraw(impl.command_buffer, impl.panel_vertex_count, 1, 0, 0);
    }
    if (impl.panel_pipeline != VK_NULL_HANDLE && impl.overlay_vertex_buffer.buffer != VK_NULL_HANDLE && impl.overlay_texture.descriptor_set != VK_NULL_HANDLE && impl.overlay_vertex_count > 0) {
        auto overlay_constants = make_identity_constants();
        VkDeviceSize overlay_vertex_buffer_offset = 0;
        vkCmdBindPipeline(impl.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.panel_pipeline);
        vkCmdPushConstants(impl.command_buffer, impl.panel_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &overlay_constants);
        vkCmdBindDescriptorSets(impl.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.panel_pipeline_layout, 0, 1, &impl.overlay_texture.descriptor_set, 0, nullptr);
        vkCmdBindVertexBuffers(impl.command_buffer, 0, 1, &impl.overlay_vertex_buffer.buffer, &overlay_vertex_buffer_offset);
        vkCmdDraw(impl.command_buffer, impl.overlay_vertex_count, 1, 0, 0);
    }
    vkCmdEndRenderPass(impl.command_buffer);
    vkEndCommandBuffer(impl.command_buffer);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &impl.image_available;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &impl.command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &impl.render_finished;
    result = vkQueueSubmit(impl.graphics_queue, 1, &submit_info, impl.in_flight);
    if (result != VK_SUCCESS)
        return Error::from_string_literal("vkQueueSubmit failed");

    VkPresentInfoKHR present_info {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &impl.render_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &impl.swapchain;
    present_info.pImageIndices = &image_index;
    result = vkQueuePresentKHR(impl.graphics_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        impl.swapchain_dirty = true;
    else if (result != VK_SUCCESS)
        warnln("vkQueuePresentKHR returned VkResult {}", to_underlying(result));

    if (panel_staging_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(impl.device, panel_staging_buffer, nullptr);
    if (overlay_staging_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(impl.device, overlay_staging_buffer, nullptr);
    if (panel_staging_buffer_memory != VK_NULL_HANDLE)
        vkFreeMemory(impl.device, panel_staging_buffer_memory, nullptr);
    if (overlay_staging_buffer_memory != VK_NULL_HANDLE)
        vkFreeMemory(impl.device, overlay_staging_buffer_memory, nullptr);
    impl.panel_bitmap_view = {};
    impl.overlay_bitmap_view = {};

    if (!world)
        outln("  Vulkan triangle presented ({}x{})", impl.extent.width, impl.extent.height);
    return {};
}
#endif

VulkanRenderer::VulkanRenderer(SDL_Window& window, VirtualFileSystem const* file_system)
    : m_window(window)
    , m_file_system(file_system)
{
}

VulkanRenderer::~VulkanRenderer()
{
    destroy();
}

ErrorOr<NonnullOwnPtr<VulkanRenderer>> VulkanRenderer::create(SDL_Window& window, VirtualFileSystem const* file_system)
{
    auto renderer = TRY(adopt_nonnull_own_or_enomem(new (nothrow) VulkanRenderer(window, file_system)));
    TRY(renderer->initialize());
    return renderer;
}

ErrorOr<void> VulkanRenderer::initialize()
{
#if !defined(USE_VULKAN)
    return Error::from_string_literal("Vulkan support was not enabled in this build");
#else
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        warnln("SDL_Vulkan_LoadLibrary failed: {}", SDL_GetError());
        return Error::from_string_literal("SDL_Vulkan_LoadLibrary failed");
    }

    Uint32 extension_count = 0;
    auto extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (!extensions) {
        warnln("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
        SDL_Vulkan_UnloadLibrary();
        return Error::from_string_literal("SDL_Vulkan_GetInstanceExtensions failed");
    }

    auto impl = adopt_own_if_nonnull(new (nothrow) Impl);
    if (!impl) {
        SDL_Vulkan_UnloadLibrary();
        return Error::from_errno(ENOMEM);
    }
    impl->mesh_library.emplace(m_file_system, &impl->material_library);
    impl->file_system = m_file_system;

    VkApplicationInfo application_info {};
    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pApplicationName = "MyceliumVR";
    application_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    application_info.pEngineName = "MyceliumVR";
    application_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    application_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_create_info {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pApplicationInfo = &application_info;
    instance_create_info.enabledExtensionCount = extension_count;
    instance_create_info.ppEnabledExtensionNames = extensions;

    auto create_instance_result = vkCreateInstance(&instance_create_info, nullptr, &impl->instance);
    if (create_instance_result != VK_SUCCESS) {
        warnln("vkCreateInstance failed with VkResult {}", to_underlying(create_instance_result));
        SDL_Vulkan_UnloadLibrary();
        return Error::from_string_literal("vkCreateInstance failed");
    }

    if (!SDL_Vulkan_CreateSurface(&m_window, impl->instance, nullptr, &impl->surface)) {
        warnln("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        vkDestroyInstance(impl->instance, nullptr);
        SDL_Vulkan_UnloadLibrary();
        return Error::from_string_literal("SDL_Vulkan_CreateSurface failed");
    }

    impl->physical_device = TRY(pick_physical_device(impl->instance, impl->surface, impl->graphics_queue_family));
    impl->device = TRY(create_logical_device(impl->physical_device, impl->graphics_queue_family));
    vkGetDeviceQueue(impl->device, impl->graphics_queue_family, 0, &impl->graphics_queue);

    VkPhysicalDeviceProperties device_properties {};
    vkGetPhysicalDeviceProperties(impl->physical_device, &device_properties);
    outln("  Vulkan renderer device: {}", device_properties.deviceName);

    m_impl = move(impl);
    m_initialized = true;
    return {};
#endif
}

ErrorOr<void> VulkanRenderer::draw_test_triangle()
{
#if !defined(USE_VULKAN)
    return Error::from_string_literal("Vulkan support was not enabled in this build");
#else
    VERIFY(m_impl);
    return draw_frame(*m_impl, m_impl->camera_state);
#endif
}

ErrorOr<void> VulkanRenderer::draw_world(World const& world)
{
#if !defined(USE_VULKAN)
    return Error::from_string_literal("Vulkan support was not enabled in this build");
#else
    VERIFY(m_impl);
    return draw_frame(*m_impl, m_impl->camera_state, &world, &m_impl->panel_pixels, m_impl->panel_width, m_impl->panel_height, &m_impl->overlay_view);
#endif
}

void VulkanRenderer::resize(int width, int height)
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;
    m_impl->drawable_width = width > 1 ? static_cast<u32>(width) : 1u;
    m_impl->drawable_height = height > 1 ? static_cast<u32>(height) : 1u;
    m_impl->swapchain_dirty = true;
#else
    (void)width;
    (void)height;
#endif
}

void VulkanRenderer::set_camera_state(CameraState const& camera_state)
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;
    m_impl->camera_state = camera_state;
#else
    (void)camera_state;
#endif
}

void VulkanRenderer::set_scene_light(SceneLightData const& light)
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;
    m_impl->scene_light = light;
#else
    (void)light;
#endif
}

VulkanRenderer::SceneLightData VulkanRenderer::scene_light() const
{
#if defined(USE_VULKAN)
    if (m_impl)
        return m_impl->scene_light;
#endif
    return {};
}

VulkanRenderer::CameraState VulkanRenderer::camera_state() const
{
#if defined(USE_VULKAN)
    if (m_impl)
        return m_impl->camera_state;
#endif
    return {};
}

void VulkanRenderer::set_panel_bitmap(Vector<u8> pixels, u32 width, u32 height)
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;
    auto pixels_are_equal = m_impl->panel_pixels.size() == pixels.size()
        && (pixels.is_empty() || __builtin_memcmp(m_impl->panel_pixels.data(), pixels.data(), pixels.size()) == 0);
    auto changed = m_impl->panel_width != width
        || m_impl->panel_height != height
        || !pixels_are_equal;
    m_impl->panel_pixels = move(pixels);
    m_impl->panel_width = width;
    m_impl->panel_height = height;
    m_impl->panel_texture_dirty = changed;
    m_impl->panel_bitmap_view = {};
#else
    (void)pixels;
    (void)width;
    (void)height;
#endif
}

void VulkanRenderer::set_panel_bitmap_view(BitmapView bitmap_view)
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;
    auto changed = m_impl->panel_bitmap_view.bitmap != bitmap_view.bitmap
        || m_impl->panel_bitmap_view.width != bitmap_view.width
        || m_impl->panel_bitmap_view.height != bitmap_view.height;
    m_impl->panel_bitmap_view = bitmap_view;
    m_impl->panel_texture_dirty = changed;
#else
    (void)bitmap_view;
#endif
}

void VulkanRenderer::clear_panel_bitmap()
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;
    m_impl->panel_pixels.clear();
    m_impl->panel_width = 0;
    m_impl->panel_height = 0;
    m_impl->panel_texture_dirty = false;
    m_impl->panel_bitmap_view = {};
    destroy_texture_resource(m_impl->device, m_impl->panel_texture);
#endif
}

void VulkanRenderer::set_overlay_view(OverlayView overlay_view)
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;
    auto pixels_are_equal = m_impl->overlay_view.pixels.size() == overlay_view.pixels.size()
        && (overlay_view.pixels.is_empty() || __builtin_memcmp(m_impl->overlay_view.pixels.data(), overlay_view.pixels.data(), overlay_view.pixels.size()) == 0);
    auto changed = m_impl->overlay_view.width != overlay_view.width
        || m_impl->overlay_view.height != overlay_view.height
        || !pixels_are_equal;
    m_impl->overlay_view = move(overlay_view);
    m_impl->overlay_texture_dirty = changed;
    m_impl->overlay_bitmap_view = {};
#else
    (void)overlay_view;
#endif
}

void VulkanRenderer::set_overlay_bitmap_view(BitmapView bitmap_view)
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;
    auto changed = m_impl->overlay_bitmap_view.bitmap != bitmap_view.bitmap
        || m_impl->overlay_bitmap_view.width != bitmap_view.width
        || m_impl->overlay_bitmap_view.height != bitmap_view.height;
    m_impl->overlay_bitmap_view = bitmap_view;
    m_impl->overlay_view = {};
    m_impl->overlay_texture_dirty = changed;
#else
    (void)bitmap_view;
#endif
}

void VulkanRenderer::clear_overlay_bitmap()
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;
    m_impl->overlay_view = {};
    m_impl->overlay_texture_dirty = false;
    m_impl->overlay_bitmap_view = {};
    destroy_texture_resource(m_impl->device, m_impl->overlay_texture);
#endif
}

void VulkanRenderer::destroy()
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;

    if (m_impl->device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_impl->device);
    destroy_swapchain_resources(m_impl->device, *m_impl);

    if (m_impl->device != VK_NULL_HANDLE)
        vkDestroyDevice(m_impl->device, nullptr);
    if (m_impl->surface != VK_NULL_HANDLE)
        SDL_Vulkan_DestroySurface(m_impl->instance, m_impl->surface, nullptr);
    if (m_impl->instance != VK_NULL_HANDLE)
        vkDestroyInstance(m_impl->instance, nullptr);
    SDL_Vulkan_UnloadLibrary();
    m_impl = nullptr;
#endif
    m_initialized = false;
}

}
