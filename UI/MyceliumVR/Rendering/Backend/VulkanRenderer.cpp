/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanRenderer.h"
#include "../../World/RenderSnapshot.h"
#include "../Pipeline/TextureLibrary.h"
#include "VulkanBackend.h"
#include "../VulkanCommon.h"
#include "VulkanContext.h"
#include "../Pipeline/VulkanMeshBuilder.h"
#include "../Pipeline/VulkanPBRManager.h"
#include "../Pipeline/VulkanPipeline.h"
#include "VulkanResourceManager.h"
#include "VulkanSwapchain.h"
#include "../RenderGraph.h"
#include "../Passes/ComputeCullPass.h"
#include "../Passes/ClusterLightAssignPass.h"

#include "../Passes/DepthPrepass.h"
#include "../Passes/GeometryPass.h"
#include "../Passes/ShadowPass.h"
#include "../Passes/UIPanelPass.h"
#include "../Web/WebViewManager.h"
#include "VulkanDebug.h"

#include <UI/MyceliumVR/Support/Profiling.h>

#include "../../Support/MaterialLibrary.h"
#include "../../Support/MeshLibrary.h"
#include "../../Support/ShaderCompiler.h"
#include "../../Support/VirtualFileSystem.h"

#include <AK/Array.h>
#include <AK/ByteString.h>
#include <AK/Format.h>
#include <AK/ScopeGuard.h>
#include <LibGfx/Bitmap.h>
#include <SDL3/SDL_vulkan.h>

#if !defined(MYCELIUMVR_SHADER_DIRECTORY)
#    define MYCELIUMVR_SHADER_DIRECTORY "."
#endif

#if !defined(MYCELIUMVR_SHADER_SOURCE_DIRECTORY)
#    define MYCELIUMVR_SHADER_SOURCE_DIRECTORY "."
#endif

namespace MyceliumVR {

#if defined(USE_VULKAN)

struct VulkanRenderer::Impl {
    NonnullOwnPtr<VulkanBackend> backend;
    VulkanSwapchain sc;

    VulkanWorldPipeline world_pipeline;
    VulkanDepthPipeline depth_prepass_pipeline;
    VulkanPanelPipeline panel_pipeline;
    VulkanPostPipeline post_pipeline;
    VulkanShadowPipeline shadow_pipeline;

    VulkanTexture flat_normal_texture;
    VulkanTexture fallback_black_texture;

    ShaderBytecode vertex_shader_bytecode;
    ShaderBytecode fragment_shader_bytecode;
    Optional<ShaderBytecode> panel_vertex_shader_bytecode;
    Optional<ShaderBytecode> panel_fragment_shader_bytecode;
    Optional<ShaderBytecode> post_vertex_shader_bytecode;
    Optional<ShaderBytecode> post_fragment_shader_bytecode;
    Optional<ShaderBytecode> shadow_vertex_shader_bytecode;
    ShaderBytecode cull_shader_bytecode;
    Optional<ShaderBytecode> cluster_compute_shader_bytecode;
    bool shader_bytecode_loaded { false };

    VkDescriptorPool post_descriptor_pool { VK_NULL_HANDLE };
    Array<VkDescriptorSet, 4> post_descriptor_sets {};
    VkSampler post_sampler { VK_NULL_HANDLE };

    VkPipeline cull_pipeline { VK_NULL_HANDLE };
    VkPipelineLayout cull_pipeline_layout { VK_NULL_HANDLE };
    VkDescriptorSetLayout cull_descriptor_set_layout { VK_NULL_HANDLE };
    VkDescriptorPool cull_descriptor_pool { VK_NULL_HANDLE };
    VkDescriptorSet cull_descriptor_set { VK_NULL_HANDLE };

    MyceliumVR::CameraState camera_state;
    SceneLightData scene_light;
    ShadowQuality shadow_quality { default_shadow_quality };

    MaterialLibrary material_library;
    Optional<MeshLibrary> mesh_library;
    Optional<TextureLibrary> m_texture_library;

    OwnPtr<DepthPrepass> depth_prepass;
    OwnPtr<GeometryPass> geometry_pass;
    OwnPtr<ShadowPass> shadow_pass;
    OwnPtr<UIPanelPass> ui_panel_pass;
    OwnPtr<ComputeCullPass> compute_cull_pass;
    OwnPtr<ClusterLightAssignPass> cluster_light_assign_pass;

    OwnPtr<PostProcessPass> bloom_threshold_pass;
    OwnPtr<PostProcessPass> bloom_blur_h_pass;
    OwnPtr<PostProcessPass> bloom_blur_v_pass;
    OwnPtr<PostProcessPass> composite_pass;

    VkPipeline            cluster_light_pipeline        { VK_NULL_HANDLE };
    VkPipelineLayout      cluster_light_pipeline_layout { VK_NULL_HANDLE };
    VkDescriptorSetLayout cluster_compute_dset_layout   { VK_NULL_HANDLE };
    VkDescriptorPool      cluster_compute_dpool         { VK_NULL_HANDLE };
    VkDescriptorSet       cluster_compute_dset          { VK_NULL_HANDLE };
    VkDescriptorPool      cluster_graphics_dpool        { VK_NULL_HANDLE };
    VkDescriptorSet       cluster_graphics_dset         { VK_NULL_HANDLE };

    Optional<RenderGraph> graph;
    WebViewManager web_view_manager;

    struct CachedStrings {
        String backbuffer = "Backbuffer"_string;
        String scene_color = "SceneColor"_string;
        String scene_depth = "SceneDepth"_string;
        String shadow_map = "ShadowMap"_string;
        String bloom_threshold = "BloomThreshold"_string;
        String bloom_blur_h = "BloomBlurH"_string;
        String bloom_blur_v = "BloomBlurV"_string;
        String composite_color = "CompositeColor"_string;
        String cluster_point_lights = "ClusterPointLights"_string;
        String cluster_count        = "ClusterCount"_string;
        String cluster_index        = "ClusterIndex"_string;
        String geometry_vertex = "GeometryVertex"_string;
        String geometry_instance = "GeometryInstance"_string;
        String geometry_indirect_live = "GeometryIndirectLive"_string;
        String geometry_indirect_ref = "GeometryIndirectRef"_string;
        String geometry_aabb = "GeometryAABB"_string;
        String panel_vertices = "PanelVertices"_string;
        String overlay_vertices = "OverlayVertices"_string;
        String ui_panel_texture = "UIPanelTexture"_string;
        String ui_overlay_texture = "UIOverlayTexture"_string;
        String geometry_vertex_staging = "GeometryVertexStaging"_string;
        String geometry_instance_staging = "GeometryInstanceStaging"_string;
        String geometry_indirect_staging = "GeometryIndirectStaging"_string;
        String geometry_aabb_staging = "GeometryAABBStaging"_string;
        String panel_vertex_staging = "PanelVertexStaging"_string;
        String overlay_vertex_staging = "OverlayVertexStaging"_string;
        String ui_panel_texture_staging = "UIPanelTextureStaging"_string;
        String ui_overlay_texture_staging = "UIOverlayTextureStaging"_string;
        String bloom_threshold_name = "BloomThreshold"_string;
        String bloom_blur_h_name = "BloomBlurH"_string;
        String bloom_blur_v_name = "BloomBlurV"_string;
        String composite_name = "Composite"_string;
        String present_name = "PresentComposite"_string;
    } names;

    VirtualFileSystem const* file_system { nullptr };
    bool swapchain_dirty { true };
    u32 drawable_width { 1280 };
    u32 drawable_height { 720 };
    bool is_rendering { false };
    u64 frame_sequence { 0 };

    HashMap<u32, ByteString> registered_meshes;
    HashMap<u32, ByteString> registered_materials;
    HashMap<u32, UIPanelPass::RegisteredPanel> registered_panels;

    explicit Impl(NonnullOwnPtr<VulkanBackend> b) : backend(move(b)) {}
};

static void destroy_vulkan_resources(VulkanContext& ctx, VulkanRenderer::Impl& impl)
{
    auto device = ctx.device();
    auto allocator = ctx.allocator();
    if (device == VK_NULL_HANDLE) return;

    impl.sc.destroy(device, allocator);
    impl.world_pipeline.destroy(device);
    impl.depth_prepass_pipeline.destroy(device);
    impl.panel_pipeline.destroy(device);
    impl.post_pipeline.destroy(device);
    impl.shadow_pipeline.destroy(device);

    if (impl.depth_prepass) impl.depth_prepass->destroy(device, allocator);
    if (impl.geometry_pass) impl.geometry_pass->destroy(device, allocator);
    if (impl.shadow_pass) impl.shadow_pass->destroy(device, allocator);
    if (impl.ui_panel_pass) impl.ui_panel_pass->destroy(device, allocator);
    if (impl.compute_cull_pass) impl.compute_cull_pass->destroy(device, allocator);
    if (impl.cluster_light_assign_pass) impl.cluster_light_assign_pass->destroy(device, allocator);

    impl.bloom_threshold_pass.clear();
    impl.bloom_blur_h_pass.clear();
    impl.bloom_blur_v_pass.clear();
    impl.composite_pass.clear();

    if (impl.cull_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, impl.cull_pipeline, nullptr);
    if (impl.cull_pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, impl.cull_pipeline_layout, nullptr);
    if (impl.cull_descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, impl.cull_descriptor_pool, nullptr);
    if (impl.cull_descriptor_set_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, impl.cull_descriptor_set_layout, nullptr);
    if (impl.cluster_light_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, impl.cluster_light_pipeline, nullptr);
    if (impl.cluster_light_pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, impl.cluster_light_pipeline_layout, nullptr);
    if (impl.cluster_compute_dpool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, impl.cluster_compute_dpool, nullptr);
    if (impl.cluster_compute_dset_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, impl.cluster_compute_dset_layout, nullptr);
    if (impl.cluster_graphics_dpool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, impl.cluster_graphics_dpool, nullptr);
    if (impl.post_descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, impl.post_descriptor_pool, nullptr);
    if (impl.post_sampler != VK_NULL_HANDLE) vkDestroySampler(device, impl.post_sampler, nullptr);

    impl.flat_normal_texture.destroy(device, allocator);
    impl.fallback_black_texture.destroy(device, allocator);
}

static ErrorOr<void> ensure_shader_bytecode_loaded(VkDevice device, VulkanRenderer::Impl& impl)
{
    return VulkanPipelineManager::ensure_shader_bytecode_loaded(
        device,
        impl.vertex_shader_bytecode,
        impl.fragment_shader_bytecode,
        impl.panel_vertex_shader_bytecode,
        impl.panel_fragment_shader_bytecode,
        impl.post_vertex_shader_bytecode,
        impl.post_fragment_shader_bytecode,
        impl.shadow_vertex_shader_bytecode,
        impl.cull_shader_bytecode,
        impl.cluster_compute_shader_bytecode,
        impl.shader_bytecode_loaded);
}

static ErrorOr<void> ensure_vulkan_resources(VulkanContext const& ctx, VulkanRenderer::Impl& impl)
{
    TRY(ensure_shader_bytecode_loaded(ctx.device(), impl));

    if (impl.sc.swapchain != VK_NULL_HANDLE && !impl.swapchain_dirty) return {};

    TRY(VulkanSwapchainManager::ensure_swapchain_resources(ctx.physical_device(), ctx.device(), ctx.surface(), ctx.allocator(), impl.drawable_width, impl.drawable_height, impl.sc));

    if (!impl.m_texture_library.has_value()) {
        impl.m_texture_library.emplace(ctx.physical_device(), ctx.device(), ctx.graphics_queue_family(), ctx.graphics_queue(), ctx.allocator());
    }

    if (!impl.geometry_pass) {
        impl.geometry_pass = make<GeometryPass>(impl.world_pipeline, impl.material_library, *impl.mesh_library, *impl.m_texture_library, impl.file_system);
    }

    TRY(VulkanPBRManager::ensure_bindless_set(ctx.device(), impl.geometry_pass->bindless_set()));

    if (impl.world_pipeline.opaque_backface_pipeline == VK_NULL_HANDLE) {
        impl.world_pipeline = TRY(VulkanPipelineManager::create_world_pipeline(ctx.device(), impl.sc.image_format, impl.sc.depth_format, impl.geometry_pass->bindless_set().layout, impl.vertex_shader_bytecode, impl.fragment_shader_bytecode));
        VulkanDebug::set_object_name(ctx.device(), VK_OBJECT_TYPE_PIPELINE, (uint64_t)impl.world_pipeline.opaque_backface_pipeline, "MyceliumVR/WorldOpaqueBackfacePipeline"sv);
        VulkanDebug::set_object_name(ctx.device(), VK_OBJECT_TYPE_PIPELINE, (uint64_t)impl.world_pipeline.opaque_no_cull_pipeline, "MyceliumVR/WorldOpaqueNoCullPipeline"sv);
        VulkanDebug::set_object_name(ctx.device(), VK_OBJECT_TYPE_PIPELINE, (uint64_t)impl.world_pipeline.blend_backface_pipeline,  "MyceliumVR/WorldBlendBackfacePipeline"sv);
        VulkanDebug::set_object_name(ctx.device(), VK_OBJECT_TYPE_PIPELINE, (uint64_t)impl.world_pipeline.blend_no_cull_pipeline,  "MyceliumVR/WorldBlendNoCullPipeline"sv);
    }

    if (impl.panel_pipeline.panel_pipeline == VK_NULL_HANDLE && impl.panel_vertex_shader_bytecode.has_value()) {
        impl.panel_pipeline = TRY(VulkanPipelineManager::create_panel_pipeline(ctx.device(), impl.sc.image_format, impl.sc.depth_format, *impl.panel_vertex_shader_bytecode, *impl.panel_fragment_shader_bytecode));
    }

    if (impl.post_pipeline.pipeline == VK_NULL_HANDLE && impl.post_vertex_shader_bytecode.has_value()) {
        impl.post_pipeline = TRY(VulkanPipelineManager::create_post_pipeline(ctx.device(), impl.sc.image_format, *impl.post_vertex_shader_bytecode, *impl.post_fragment_shader_bytecode));
    }

    if (impl.shadow_pipeline.backface_pipeline == VK_NULL_HANDLE && impl.shadow_vertex_shader_bytecode.has_value()) {
        impl.shadow_pipeline = TRY(VulkanPipelineManager::create_shadow_pipeline(ctx.device(), impl.sc.depth_format, *impl.shadow_vertex_shader_bytecode));
        VulkanDebug::name(ctx.device(), impl.shadow_pipeline.backface_pipeline, VK_OBJECT_TYPE_PIPELINE, "MyceliumVR/ShadowBackfacePipeline"sv);
        VulkanDebug::name(ctx.device(), impl.shadow_pipeline.no_cull_pipeline,  VK_OBJECT_TYPE_PIPELINE, "MyceliumVR/ShadowNoCullPipeline"sv);
    }

    if (impl.depth_prepass_pipeline.backface_pipeline == VK_NULL_HANDLE && impl.shadow_vertex_shader_bytecode.has_value()) {
        impl.depth_prepass_pipeline = TRY(VulkanPipelineManager::create_depth_prepass_pipeline(ctx.device(), impl.sc.depth_format, *impl.shadow_vertex_shader_bytecode));
        VulkanDebug::name(ctx.device(), impl.depth_prepass_pipeline.backface_pipeline, VK_OBJECT_TYPE_PIPELINE, "MyceliumVR/DepthPrepassBackfacePipeline"sv);
        VulkanDebug::name(ctx.device(), impl.depth_prepass_pipeline.no_cull_pipeline,  VK_OBJECT_TYPE_PIPELINE, "MyceliumVR/DepthPrepassNoCullPipeline"sv);
    }

    if (impl.post_descriptor_pool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 };
        VkDescriptorPoolCreateInfo pool_info { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 4, 1, &pool_size };
        TRY(check_vulkan_result(vkCreateDescriptorPool(ctx.device(), &pool_info, nullptr, &impl.post_descriptor_pool), "vkCreateDescriptorPool post failed"sv));

        Array<VkDescriptorSetLayout, 4> layouts {
            impl.post_pipeline.layout,
            impl.post_pipeline.layout,
            impl.post_pipeline.layout,
            impl.post_pipeline.layout
        };
        VkDescriptorSetAllocateInfo alloc_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl.post_descriptor_pool, 4, layouts.data() };
        TRY(check_vulkan_result(vkAllocateDescriptorSets(ctx.device(), &alloc_info, impl.post_descriptor_sets.data()), "vkAllocateDescriptorSets post failed"sv));

        VkSamplerCreateInfo sampler_info {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.maxLod = 1.0f;
        TRY(check_vulkan_result(vkCreateSampler(ctx.device(), &sampler_info, nullptr, &impl.post_sampler), "vkCreateSampler post failed"sv));
    }

    if (impl.cull_pipeline == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding bindings[3] {};
        for (u32 i = 0; i < 3; ++i) {
            bindings[i].binding = (u32)i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            bindings[i].pImmutableSamplers = nullptr;
        }
        VkDescriptorSetLayoutCreateInfo layout_info { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = nullptr, .flags = 0, .bindingCount = 3, .pBindings = bindings };
        TRY(check_vulkan_result(vkCreateDescriptorSetLayout(ctx.device(), &layout_info, nullptr, &impl.cull_descriptor_set_layout), "vkCreateDescriptorSetLayout cull failed"sv));

        VkPushConstantRange pc_range { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(Mat4) + 4 };
        VkPipelineLayoutCreateInfo pipeline_layout_info { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pNext = nullptr, .flags = 0, .setLayoutCount = 1, .pSetLayouts = &impl.cull_descriptor_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc_range };
        TRY(check_vulkan_result(vkCreatePipelineLayout(ctx.device(), &pipeline_layout_info, nullptr, &impl.cull_pipeline_layout), "vkCreatePipelineLayout cull failed"sv));

        VkShaderModule cs = TRY(VulkanPipelineManager::create_shader_module(ctx.device(), impl.cull_shader_bytecode));
        ScopeGuard sg = [&] { vkDestroyShaderModule(ctx.device(), cs, nullptr); };

        VkComputePipelineCreateInfo pipeline_info {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .pNext = nullptr, .flags = 0, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = cs, .pName = "main", .pSpecializationInfo = nullptr },
            .layout = impl.cull_pipeline_layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1
        };
        TRY(check_vulkan_result(vkCreateComputePipelines(ctx.device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &impl.cull_pipeline), "vkCreateComputePipelines failed"sv));

        VkDescriptorPoolSize pool_size { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 3 };
        VkDescriptorPoolCreateInfo pool_info { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .pNext = nullptr, .flags = 0, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size };
        TRY(check_vulkan_result(vkCreateDescriptorPool(ctx.device(), &pool_info, nullptr, &impl.cull_descriptor_pool), "vkCreateDescriptorPool cull failed"sv));

        VkDescriptorSetAllocateInfo alloc_info { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .pNext = nullptr, .descriptorPool = impl.cull_descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &impl.cull_descriptor_set_layout };
        TRY(check_vulkan_result(vkAllocateDescriptorSets(ctx.device(), &alloc_info, &impl.cull_descriptor_set), "vkAllocateDescriptorSets cull failed"sv));

        VulkanDebug::name(ctx.device(), impl.cull_pipeline,         VK_OBJECT_TYPE_PIPELINE,              "MyceliumVR/CullPipeline"sv);
        VulkanDebug::name(ctx.device(), impl.cull_pipeline_layout,  VK_OBJECT_TYPE_PIPELINE_LAYOUT,       "MyceliumVR/CullPipelineLayout"sv);
        VulkanDebug::name(ctx.device(), impl.cull_descriptor_pool,  VK_OBJECT_TYPE_DESCRIPTOR_POOL,       "MyceliumVR/CullDescriptorPool"sv);
        VulkanDebug::name(ctx.device(), impl.cull_descriptor_set_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "MyceliumVR/CullDescriptorSetLayout"sv);
    }

    if (!impl.compute_cull_pass) {
        impl.compute_cull_pass = make<ComputeCullPass>(impl.cull_pipeline, impl.cull_pipeline_layout, impl.cull_descriptor_set);
    }

    if (impl.cluster_light_pipeline == VK_NULL_HANDLE && impl.cluster_compute_shader_bytecode.has_value()) {
        // Compute descriptor set layout for ClusterLightAssign: 3 SSBO bindings (compute stage)
        VkDescriptorSetLayoutBinding bindings[3] {};
        for (u32 i = 0; i < 3; ++i) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dsl_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, bindings };
        TRY(check_vulkan_result(vkCreateDescriptorSetLayout(ctx.device(), &dsl_info, nullptr, &impl.cluster_compute_dset_layout), "vkCreateDescriptorSetLayout cluster-compute failed"sv));

        VkPushConstantRange pc_range { VK_SHADER_STAGE_COMPUTE_BIT, 0, 84 }; // sizeof(ClusterPC) = 64 + 20
        VkPipelineLayoutCreateInfo pl_info { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &impl.cluster_compute_dset_layout, 1, &pc_range };
        TRY(check_vulkan_result(vkCreatePipelineLayout(ctx.device(), &pl_info, nullptr, &impl.cluster_light_pipeline_layout), "vkCreatePipelineLayout cluster-light failed"sv));

        VkShaderModule cs = TRY(VulkanPipelineManager::create_shader_module(ctx.device(), *impl.cluster_compute_shader_bytecode));
        ScopeGuard sg_cs = [&] { vkDestroyShaderModule(ctx.device(), cs, nullptr); };

        VkComputePipelineCreateInfo cp_info {
            .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext  = nullptr,
            .flags  = 0,
            .stage  = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, cs, "main", nullptr },
            .layout = impl.cluster_light_pipeline_layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex  = -1,
        };
        TRY(check_vulkan_result(vkCreateComputePipelines(ctx.device(), VK_NULL_HANDLE, 1, &cp_info, nullptr, &impl.cluster_light_pipeline), "vkCreateComputePipelines cluster-light failed"sv));

        VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
        VkDescriptorPoolCreateInfo pool_info { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &pool_size };
        TRY(check_vulkan_result(vkCreateDescriptorPool(ctx.device(), &pool_info, nullptr, &impl.cluster_compute_dpool), "vkCreateDescriptorPool cluster-compute failed"sv));

        VkDescriptorSetAllocateInfo alloc_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl.cluster_compute_dpool, 1, &impl.cluster_compute_dset_layout };
        TRY(check_vulkan_result(vkAllocateDescriptorSets(ctx.device(), &alloc_info, &impl.cluster_compute_dset), "vkAllocateDescriptorSets cluster-compute failed"sv));

        VulkanDebug::name(ctx.device(), impl.cluster_light_pipeline,        VK_OBJECT_TYPE_PIPELINE,              "MyceliumVR/ClusterLightAssignPipeline"sv);
        VulkanDebug::name(ctx.device(), impl.cluster_light_pipeline_layout,  VK_OBJECT_TYPE_PIPELINE_LAYOUT,       "MyceliumVR/ClusterLightAssignLayout"sv);
        VulkanDebug::name(ctx.device(), impl.cluster_compute_dpool,          VK_OBJECT_TYPE_DESCRIPTOR_POOL,       "MyceliumVR/ClusterComputeDescPool"sv);
        VulkanDebug::name(ctx.device(), impl.cluster_compute_dset_layout,    VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "MyceliumVR/ClusterComputeDescLayout"sv);
    }

    if (!impl.cluster_light_assign_pass && impl.cluster_light_pipeline != VK_NULL_HANDLE) {
        impl.cluster_light_assign_pass = make<ClusterLightAssignPass>(impl.cluster_light_pipeline, impl.cluster_light_pipeline_layout, impl.cluster_compute_dset);
        TRY(impl.cluster_light_assign_pass->ensure_buffers(ctx));

        // Create graphics-side descriptor set (set=4) pointing to the same cluster buffers.
        VkDescriptorPoolSize gpool_size { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
        VkDescriptorPoolCreateInfo gpool_info { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &gpool_size };
        TRY(check_vulkan_result(vkCreateDescriptorPool(ctx.device(), &gpool_info, nullptr, &impl.cluster_graphics_dpool), "vkCreateDescriptorPool cluster-graphics failed"sv));

        VkDescriptorSetAllocateInfo galloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl.cluster_graphics_dpool, 1, &impl.world_pipeline.cluster_layout };
        TRY(check_vulkan_result(vkAllocateDescriptorSets(ctx.device(), &galloc, &impl.cluster_graphics_dset), "vkAllocateDescriptorSets cluster-graphics failed"sv));

        VkDescriptorBufferInfo gbuf_infos[3] {
            { impl.cluster_light_assign_pass->point_light_buffer().buffer,   0, impl.cluster_light_assign_pass->point_light_buffer().size   },
            { impl.cluster_light_assign_pass->cluster_count_buffer().buffer, 0, impl.cluster_light_assign_pass->cluster_count_buffer().size },
            { impl.cluster_light_assign_pass->cluster_index_buffer().buffer, 0, impl.cluster_light_assign_pass->cluster_index_buffer().size },
        };
        VkWriteDescriptorSet gwrites[3] {};
        for (u32 i = 0; i < 3; ++i) {
            gwrites[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            gwrites[i].dstSet          = impl.cluster_graphics_dset;
            gwrites[i].dstBinding      = i;
            gwrites[i].descriptorCount = 1;
            gwrites[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            gwrites[i].pBufferInfo     = &gbuf_infos[i];
        }
        vkUpdateDescriptorSets(ctx.device(), 3, gwrites, 0, nullptr);

        VulkanDebug::name(ctx.device(), impl.cluster_graphics_dpool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, "MyceliumVR/ClusterGraphicsDescPool"sv);
        VulkanDebug::name(ctx.device(), impl.cluster_graphics_dset,  VK_OBJECT_TYPE_DESCRIPTOR_SET,  "MyceliumVR/ClusterGraphicsDescSet"sv);
    }

    if (!impl.depth_prepass && impl.depth_prepass_pipeline.backface_pipeline != VK_NULL_HANDLE) {
        impl.depth_prepass = make<DepthPrepass>(impl.depth_prepass_pipeline);
    }

    if (!impl.shadow_pass) {
        impl.shadow_pass = make<ShadowPass>(impl.shadow_pipeline);
    }

    if (!impl.ui_panel_pass) {
        impl.ui_panel_pass = make<UIPanelPass>(impl.panel_pipeline);
    }

    if (!impl.bloom_threshold_pass) {
        impl.bloom_threshold_pass = TRY(adopt_nonnull_own_or_enomem(new (nothrow) PostProcessPass(impl.names.bloom_threshold_name.bytes_as_string_view(), impl.post_pipeline, PostProcessMode::Threshold, impl.post_descriptor_sets[0], impl.post_sampler)));
    }
    if (!impl.bloom_blur_h_pass) {
        impl.bloom_blur_h_pass = TRY(adopt_nonnull_own_or_enomem(new (nothrow) PostProcessPass(impl.names.bloom_blur_h_name.bytes_as_string_view(), impl.post_pipeline, PostProcessMode::BlurHorizontal, impl.post_descriptor_sets[1], impl.post_sampler)));
    }
    if (!impl.bloom_blur_v_pass) {
        impl.bloom_blur_v_pass = TRY(adopt_nonnull_own_or_enomem(new (nothrow) PostProcessPass(impl.names.bloom_blur_v_name.bytes_as_string_view(), impl.post_pipeline, PostProcessMode::BlurVertical, impl.post_descriptor_sets[2], impl.post_sampler)));
    }
    if (!impl.composite_pass) {
        impl.composite_pass = TRY(adopt_nonnull_own_or_enomem(new (nothrow) PostProcessPass(impl.names.composite_name.bytes_as_string_view(), impl.post_pipeline, PostProcessMode::Composite, impl.post_descriptor_sets[3], impl.post_sampler)));
    }

    impl.swapchain_dirty = false;
    return {};
}

static ErrorOr<void> validate_compiled_frame_graph(RenderGraph const& graph, VulkanRenderer::Impl const& impl, World const*)
{
    auto const& compiled = graph.compiled_graph();
    if (compiled.ordered_passes.is_empty())
        return Error::from_string_literal("Frame graph validation failed: no compiled passes");
    if (compiled.submission_plan.batches.is_empty())
        return Error::from_string_literal("Frame graph validation failed: no submission batches");
    if (graph.render_extent().width != impl.sc.extent.width || graph.render_extent().height != impl.sc.extent.height)
        return Error::from_string_literal("Frame graph validation failed: render extent does not match swapchain extent");

    auto has_pass = [&](StringView pass_name) {
        for (auto const& pass : compiled.ordered_passes) {
            if (pass.name == pass_name)
                return true;
        }
        return false;
    };

    bool expect_cull = !impl.geometry_pass->draw_groups().is_empty();
    bool expect_shadow = expect_cull && impl.shadow_quality > 0;
    bool expect_ui = impl.ui_panel_pass->has_panel_draw_work() || impl.ui_panel_pass->has_overlay_draw_work();

    if (!has_pass("Geometry"sv))
        return Error::from_string_literal("Frame graph validation failed: geometry pass missing");
    if (!has_pass("BloomThreshold"sv) || !has_pass("BloomBlurH"sv) || !has_pass("BloomBlurV"sv) || !has_pass("Composite"sv) || !has_pass("PresentComposite"sv))
        return Error::from_string_literal("Frame graph validation failed: post/composite chain missing");
    if (expect_shadow && !has_pass("Shadow"sv))
        return Error::from_string_literal("Frame graph validation failed: shadow pass missing for populated world");
    if (!expect_shadow && has_pass("Shadow"sv))
        return Error::from_string_literal("Frame graph validation failed: shadow pass present when shadows are disabled");
    if (expect_cull && !has_pass("ComputeCull"sv))
        return Error::from_string_literal("Frame graph validation failed: compute cull pass missing for populated world");
    if (!expect_cull && has_pass("ComputeCull"sv))
        return Error::from_string_literal("Frame graph validation failed: compute cull pass present without populated world");
    if (expect_ui && !has_pass("UIPanel"sv))
        return Error::from_string_literal("Frame graph validation failed: UI pass missing when panel or overlay draw work exists");
    if (!expect_ui && has_pass("UIPanel"sv))
        return Error::from_string_literal("Frame graph validation failed: UI pass present when no panel or overlay draw work exists");

    return {};
}

static void compute_shadow_fit_from_draw_groups(Vector<DrawGroup> const& draw_groups, Vec3 light_dir, Vec3& out_center, float& out_radius, float& out_depth)
{
    if (draw_groups.is_empty()) {
        out_center = { 0.0f, 0.0f, 0.0f };
        out_radius = 18.0f;
        out_depth = 64.0f;
        return;
    }

    Vec3 world_min { NumericLimits<float>::max(), NumericLimits<float>::max(), NumericLimits<float>::max() };
    Vec3 world_max { -NumericLimits<float>::max(), -NumericLimits<float>::max(), -NumericLimits<float>::max() };
    for (auto const& group : draw_groups) {
        world_min.x = min(world_min.x, group.aabb_world_min[0]);
        world_min.y = min(world_min.y, group.aabb_world_min[1]);
        world_min.z = min(world_min.z, group.aabb_world_min[2]);
        world_max.x = max(world_max.x, group.aabb_world_max[0]);
        world_max.y = max(world_max.y, group.aabb_world_max[1]);
        world_max.z = max(world_max.z, group.aabb_world_max[2]);
    }

    out_center = {
        (world_min.x + world_max.x) * 0.5f,
        (world_min.y + world_max.y) * 0.5f,
        (world_min.z + world_max.z) * 0.5f,
    };

    auto extents = Vec3 {
        (world_max.x - world_min.x) * 0.5f,
        (world_max.y - world_min.y) * 0.5f,
        (world_max.z - world_min.z) * 0.5f,
    };
    out_radius = max(max(extents.x, extents.y), extents.z);
    out_radius = max(out_radius, 8.0f);
    out_radius *= 1.15f;

    auto diagonal = __builtin_sqrtf(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z);
    auto depth_padding = max(out_radius * 2.0f, diagonal * 1.5f);
    auto light_distance = max(out_radius * 2.0f, 20.0f);
    out_depth = max(light_distance + depth_padding, 64.0f);
    (void)light_dir;
}

static ErrorOr<void> draw_frame(VulkanContext const& ctx, VulkanRenderer::Impl& impl, MyceliumVR::CameraState const& camera_state, World const* world = nullptr, ReadonlySpan<u8> const* snapshot = nullptr, ReadonlySpan<u8> const* static_scene = nullptr)
{
    if (impl.is_rendering) return {};
    impl.is_rendering = true;
    ScopeGuard rendering_guard = [&] { impl.is_rendering = false; };

    TRY(ensure_vulkan_resources(ctx, impl));
    auto frame_id = ++impl.frame_sequence;

    u32 img_idx = 0;
    auto frame = TRY(impl.backend->begin_frame(impl.sc, img_idx));
    auto cmd = frame.command_buffer;

    VmaAllocator allocator = ctx.allocator();
    TRY(VulkanPBRManager::ensure_fallback_textures(ctx.physical_device(), ctx.device(), allocator, impl.backend->frame_pool(), ctx.graphics_queue(), impl.flat_normal_texture, impl.fallback_black_texture));

    auto effective_camera_state = camera_state;
    float effective_fov_radians = 1.0471976f;
    float effective_near_plane = 0.1f;
    float effective_far_plane = 100.0f;

    if (snapshot && snapshot->size() >= sizeof(FrameHeader)) {
        auto const& header = *reinterpret_cast<FrameHeader const*>(snapshot->data());
        
        // Update global scene light from snapshot
        impl.scene_light.ambient_intensity = header.ambient_intensity;
        memcpy(impl.scene_light.ambient_rgb, header.ambient_rgb, sizeof(float) * 3);
        impl.scene_light.light_intensity = header.sun_intensity;
        memcpy(impl.scene_light.light_to_xyz, header.sun_direction, sizeof(float) * 3);
        memcpy(impl.scene_light.light_rgb, header.sun_rgb, sizeof(float) * 3);
        impl.scene_light.point_light_count = header.point_light_count;
        
        u8 const* snapshot_ptr = snapshot->data() + sizeof(FrameHeader);
        auto const* cameras = reinterpret_cast<SnapshotCamera const*>(snapshot_ptr); snapshot_ptr += sizeof(SnapshotCamera) * header.camera_count;
        snapshot_ptr += sizeof(FrameDrawGroup) * header.draw_group_count;
        snapshot_ptr += sizeof(SnapshotInstance) * header.instance_count;
        snapshot_ptr += sizeof(SnapshotPanelEntry) * header.panel_count;
        auto const* snapshot_lights = reinterpret_cast<SnapshotLightEntry const*>(snapshot_ptr);

        for (u32 i = 0; i < header.point_light_count && i < MaxPointLights; ++i) {
            auto& pl = impl.scene_light.point_lights[i];
            memcpy(pl.position, snapshot_lights[i].position, sizeof(float) * 3);
            pl.radius = snapshot_lights[i].radius;
            memcpy(pl.color, snapshot_lights[i].color, sizeof(float) * 3);
            pl.intensity = snapshot_lights[i].intensity;
        }

        if (header.camera_count > 0 && header.primary_camera_idx < header.camera_count) {
            auto const& snapshot_camera = cameras[header.primary_camera_idx];
            memcpy(effective_camera_state.position, snapshot_camera.position, sizeof(float) * 3);
            effective_camera_state.yaw_degrees = snapshot_camera.yaw_degrees;
            effective_camera_state.pitch_degrees = snapshot_camera.pitch_degrees;
            effective_fov_radians = snapshot_camera.fov_degrees * (3.14159265f / 180.0f);
            effective_near_plane = snapshot_camera.near_plane;
            effective_far_plane = snapshot_camera.far_plane;
        }
    }

    if (snapshot && snapshot->size() >= sizeof(FrameHeader)) {
        impl.geometry_pass->set_handle_mappings(&impl.registered_meshes, &impl.registered_materials);
        impl.geometry_pass->set_snapshot(*snapshot);
        if (static_scene)
            impl.geometry_pass->set_static_scene(*static_scene);
        else
            impl.geometry_pass->set_static_scene({});
    } else {
        impl.geometry_pass->set_world(world);
    }
    
    impl.geometry_pass->set_scene_light(impl.scene_light);
    impl.geometry_pass->set_camera_position(effective_camera_state.position);
    impl.geometry_pass->set_shadow_quality(impl.shadow_quality);
    impl.geometry_pass->set_fallback_textures(impl.flat_normal_texture, impl.fallback_black_texture);
    TRY(impl.geometry_pass->prepare(ctx, impl.backend->frame_pool()));

    if (snapshot && snapshot->size() >= sizeof(FrameHeader)) {
        impl.ui_panel_pass->set_panel_handles(&impl.registered_panels);
        impl.ui_panel_pass->set_snapshot(*snapshot);
        if (static_scene)
            impl.ui_panel_pass->set_static_scene(*static_scene);
        else
            impl.ui_panel_pass->set_static_scene({});
    } else {
        impl.ui_panel_pass->set_world(world);
    }
    impl.ui_panel_pass->set_debug_frame_id(frame_id);
    TRY(impl.ui_panel_pass->prepare(ctx, impl.backend->frame_pool()));

    auto safe_height_u32 = impl.sc.extent.height > 0 ? impl.sc.extent.height : 1u;
    auto aspect_ratio = static_cast<float>(impl.sc.extent.width) / static_cast<float>(safe_height_u32);
    auto projection = make_perspective_matrix(effective_fov_radians, aspect_ratio, effective_near_plane, effective_far_plane);
    auto view_matrix = make_view_matrix(effective_camera_state);
    auto view_projection = multiply(projection, view_matrix);
    PushConstants pc {};
    for (size_t i = 0; i < 16; ++i)
        pc.view_projection[i] = view_projection.elements[i];

    float fl = 1.0f / __builtin_tanf(effective_fov_radians * 0.5f);
    float proj_x = fl / aspect_ratio;
    float proj_y = -fl;

    if (impl.cluster_light_assign_pass) {
        impl.cluster_light_assign_pass->upload_lights(impl.scene_light);
        impl.cluster_light_assign_pass->set_view_matrix(view_matrix);
        impl.cluster_light_assign_pass->set_projection_params(effective_near_plane, effective_far_plane, proj_x, proj_y);
    }

    impl.geometry_pass->set_cluster_descriptor_set(impl.cluster_graphics_dset);
    impl.geometry_pass->set_cluster_params(effective_near_plane, effective_far_plane);
    impl.geometry_pass->set_screen_size(impl.sc.extent.width, impl.sc.extent.height);

    auto light_dir = normalize(Vec3 { impl.scene_light.light_to_xyz[0], impl.scene_light.light_to_xyz[1], impl.scene_light.light_to_xyz[2] });
    Vec3 shadow_center {};
    float shadow_radius = 18.0f;
    float shadow_depth = 64.0f;
    compute_shadow_fit_from_draw_groups(impl.geometry_pass->draw_groups(), light_dir, shadow_center, shadow_radius, shadow_depth);
    auto light_distance = max(shadow_radius * 2.0f, 20.0f);
    auto light_eye = Vec3 {
        shadow_center.x - light_dir.x * light_distance,
        shadow_center.y - light_dir.y * light_distance,
        shadow_center.z - light_dir.z * light_distance,
    };
    auto shadow_view = make_look_at_matrix(light_eye, shadow_center, Vec3 { 0.0f, 1.0f, 0.0f });
    auto shadow_projection = make_orthographic_matrix(-shadow_radius, shadow_radius, -shadow_radius, shadow_radius, 0.1f, shadow_depth);
    auto shadow_view_projection = multiply(shadow_projection, shadow_view);

    struct CallbackGraphPass final : public RenderGraphPass {
        String pass_name;
        Function<void(RenderPassBuilder&)> setup_callback;
        Function<void(VkCommandBuffer, RenderGraph&)> execute_callback;

        CallbackGraphPass(String name, Function<void(RenderPassBuilder&)> setup, Function<void(VkCommandBuffer, RenderGraph&)> execute)
            : pass_name(move(name))
            , setup_callback(move(setup))
            , execute_callback(move(execute))
        {
        }

        virtual String name() const override { return pass_name; }
        virtual void setup(RenderPassBuilder& builder) override { setup_callback(builder); }
        virtual void execute(VkCommandBuffer cmd, RenderGraph& graph) override { execute_callback(cmd, graph); }
    };

    auto& graph = *impl.graph;
    ZoneScopedN("Frame/BuildGraph");
#if defined(TRACY_ENABLE)
    graph.set_tracy_context(impl.backend->tracy_vk_ctx());
#endif
    graph.reset_for_frame();
    graph.set_frame_push_constants(pc);
    auto make_imported_texture = [&](String name, VkImage image, VkImageView view, VkFormat format, VkExtent2D extent, VkImageUsageFlags usage, ResourceState initial_state) {
        ImportedTextureDesc desc;
        desc.name = move(name);
        desc.width = extent.width;
        desc.height = extent.height;
        desc.format = format;
        desc.usage = usage;
        desc.transient = false;
        desc.imported = true;
        desc.external_lifetime = true;
        desc.image = image;
        desc.view = view;
        desc.extent = extent;
        desc.initial_state = initial_state;
        return desc;
    };
    auto make_imported_buffer = [&](String name, VkBuffer buffer, VkDeviceSize size, VkBufferUsageFlags usage, ResourceState initial_state) {
        ImportedBufferDesc desc;
        desc.name = move(name);
        desc.size = size;
        desc.usage = usage;
        desc.transient = false;
        desc.imported = true;
        desc.external_lifetime = true;
        desc.buffer = buffer;
        desc.initial_state = initial_state;
        return desc;
    };

    auto backbuffer = graph.import_texture(make_imported_texture(impl.names.backbuffer, impl.sc.images[img_idx], impl.sc.image_views[img_idx], impl.sc.image_format, impl.sc.extent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, ResourceState::Undefined));

    TextureDesc scene_color_desc;
    scene_color_desc.name = impl.names.scene_color;
    scene_color_desc.width = impl.sc.extent.width;
    scene_color_desc.height = impl.sc.extent.height;
    scene_color_desc.format = impl.sc.image_format;
    scene_color_desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    scene_color_desc.transient = true;
    auto scene_color = graph.create_texture(scene_color_desc);

    TextureDesc scene_depth_desc;
    scene_depth_desc.name = impl.names.scene_depth;
    scene_depth_desc.width = impl.sc.extent.width;
    scene_depth_desc.height = impl.sc.extent.height;
    scene_depth_desc.format = impl.sc.depth_format;
    scene_depth_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    scene_depth_desc.transient = true;
    auto scene_depth = graph.create_texture(scene_depth_desc);

    TextureHandle shadow_map {};
    if (impl.shadow_quality > 0) {
        TextureDesc shadow_map_desc;
        shadow_map_desc.name = impl.names.shadow_map;
        shadow_map_desc.width = 2048;
        shadow_map_desc.height = 2048;
        shadow_map_desc.format = impl.sc.depth_format;
        shadow_map_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        shadow_map_desc.transient = true;
        shadow_map = graph.create_texture(shadow_map_desc);
    }

    TextureDesc bloom_threshold_desc;
    bloom_threshold_desc.name = impl.names.bloom_threshold;
    bloom_threshold_desc.width = impl.sc.extent.width;
    bloom_threshold_desc.height = impl.sc.extent.height;
    bloom_threshold_desc.format = impl.sc.image_format;
    bloom_threshold_desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    bloom_threshold_desc.transient = true;
    auto bloom_threshold = graph.create_texture(bloom_threshold_desc);

    TextureDesc bloom_blur_h_desc = bloom_threshold_desc;
    bloom_blur_h_desc.name = impl.names.bloom_blur_h;
    auto bloom_blur_h = graph.create_texture(bloom_blur_h_desc);

    TextureDesc bloom_blur_v_desc = bloom_threshold_desc;
    bloom_blur_v_desc.name = impl.names.bloom_blur_v;
    auto bloom_blur_v = graph.create_texture(bloom_blur_v_desc);

    TextureDesc composite_color_desc = scene_color_desc;
    composite_color_desc.name = impl.names.composite_color;
    composite_color_desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto composite_color = graph.create_texture(composite_color_desc);
    auto geometry_vertex = graph.import_buffer(make_imported_buffer(impl.names.geometry_vertex, impl.geometry_pass->vertex_buffer().buffer, impl.geometry_pass->vertex_buffer().size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, impl.geometry_pass->vertex_buffer_initial_state()));
    auto geometry_instance = graph.import_buffer(make_imported_buffer(impl.names.geometry_instance, impl.geometry_pass->instance_buffer().buffer, impl.geometry_pass->instance_buffer().size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, impl.geometry_pass->instance_buffer_initial_state()));
    auto geometry_indirect_live = graph.import_buffer(make_imported_buffer(impl.names.geometry_indirect_live, impl.geometry_pass->indirect_buffer().buffer, impl.geometry_pass->indirect_buffer().size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, impl.geometry_pass->indirect_live_buffer_initial_state()));
    auto geometry_indirect_ref = graph.import_buffer(make_imported_buffer(impl.names.geometry_indirect_ref, impl.geometry_pass->indirect_ref_buffer().buffer, impl.geometry_pass->indirect_ref_buffer().size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, impl.geometry_pass->indirect_ref_buffer_initial_state()));
    auto geometry_aabb = graph.import_buffer(make_imported_buffer(impl.names.geometry_aabb, impl.geometry_pass->aabb_world_buffer().buffer, impl.geometry_pass->aabb_world_buffer().size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, impl.geometry_pass->aabb_buffer_initial_state()));
    BufferHandle cluster_point_lights_buf {};
    BufferHandle cluster_count_buf {};
    BufferHandle cluster_index_buf {};
    if (impl.cluster_light_assign_pass && impl.cluster_light_assign_pass->buffers_ready()) {
        cluster_point_lights_buf = graph.import_buffer(make_imported_buffer(impl.names.cluster_point_lights, impl.cluster_light_assign_pass->point_light_buffer().buffer, impl.cluster_light_assign_pass->point_light_buffer().size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceState::StorageRead));
        cluster_count_buf        = graph.import_buffer(make_imported_buffer(impl.names.cluster_count,        impl.cluster_light_assign_pass->cluster_count_buffer().buffer, impl.cluster_light_assign_pass->cluster_count_buffer().size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceState::Undefined));
        cluster_index_buf        = graph.import_buffer(make_imported_buffer(impl.names.cluster_index,        impl.cluster_light_assign_pass->cluster_index_buffer().buffer, impl.cluster_light_assign_pass->cluster_index_buffer().size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceState::Undefined));
    }

    auto panel_vertices = graph.import_buffer(make_imported_buffer(impl.names.panel_vertices, impl.ui_panel_pass->panel_vertex_buffer().buffer, impl.ui_panel_pass->panel_vertex_buffer().size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, impl.ui_panel_pass->panel_vertex_buffer_initial_state()));
    auto overlay_vertices = graph.import_buffer(make_imported_buffer(impl.names.overlay_vertices, impl.ui_panel_pass->overlay_vertex_buffer().buffer, impl.ui_panel_pass->overlay_vertex_buffer().size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, impl.ui_panel_pass->overlay_vertex_buffer_initial_state()));
    TextureHandle panel_texture {};
    // NOTE: Multi-panel textures are currently handled by UIPanelPass semi-externally to RenderGraph
    // as an array of textures in the descriptor set. We don't import a single one here anymore.
    
    BufferHandle geometry_vertex_staging {};
    if (impl.geometry_pass->has_vertex_upload())
        geometry_vertex_staging = graph.import_buffer(make_imported_buffer(impl.names.geometry_vertex_staging, impl.geometry_pass->vertex_staging_buffer().buffer, impl.geometry_pass->vertex_staging_buffer().size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceState::TransferSrc));
    BufferHandle geometry_instance_staging {};
    if (impl.geometry_pass->has_instance_upload())
        geometry_instance_staging = graph.import_buffer(make_imported_buffer(impl.names.geometry_instance_staging, impl.geometry_pass->instance_staging_buffer().buffer, impl.geometry_pass->instance_staging_buffer().size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceState::TransferSrc));
    BufferHandle geometry_indirect_staging {};
    if (impl.geometry_pass->has_indirect_ref_upload())
        geometry_indirect_staging = graph.import_buffer(make_imported_buffer(impl.names.geometry_indirect_staging, impl.geometry_pass->indirect_staging_buffer().buffer, impl.geometry_pass->indirect_staging_buffer().size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceState::TransferSrc));
    BufferHandle geometry_aabb_staging {};
    if (impl.geometry_pass->has_aabb_upload())
        geometry_aabb_staging = graph.import_buffer(make_imported_buffer(impl.names.geometry_aabb_staging, impl.geometry_pass->aabb_staging_buffer().buffer, impl.geometry_pass->aabb_staging_buffer().size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceState::TransferSrc));
    BufferHandle panel_vertex_staging {};
    if (impl.ui_panel_pass->has_panel_vertex_upload())
        panel_vertex_staging = graph.import_buffer(make_imported_buffer(impl.names.panel_vertex_staging, impl.ui_panel_pass->panel_vertex_staging_buffer().buffer, impl.ui_panel_pass->panel_vertex_staging_buffer().size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceState::TransferSrc));
    BufferHandle overlay_vertex_staging {};
    if (impl.ui_panel_pass->has_overlay_vertex_upload())
        overlay_vertex_staging = graph.import_buffer(make_imported_buffer(impl.names.overlay_vertex_staging, impl.ui_panel_pass->overlay_vertex_staging_buffer().buffer, impl.ui_panel_pass->overlay_vertex_staging_buffer().size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceState::TransferSrc));
    // NOTE: Multi-panel texture staging is handled internally by UIPanelPass
    
    bool has_geometry = !impl.geometry_pass->draw_groups().is_empty();
    bool shadow_pass_active = impl.shadow_quality > 0 && has_geometry;
    bool depth_prepass_active = false;
    if (impl.depth_prepass && has_geometry) {
        impl.depth_prepass->set_draw_groups(impl.geometry_pass->draw_groups());
        impl.depth_prepass->set_camera_view_projection(pc);
        impl.depth_prepass->set_graph_bindings({
            .depth_target = scene_depth,
            .vertex_buffer = geometry_vertex,
            .instance_buffer = geometry_instance,
            .indirect_buffer = geometry_indirect_live,
        });
        depth_prepass_active = impl.depth_prepass->has_work();
    }
    impl.geometry_pass->set_has_depth_prepass(depth_prepass_active);

    impl.geometry_pass->set_graph_bindings({
        .color_target = scene_color,
        .depth_target = scene_depth,
        .shadow_map = shadow_pass_active ? shadow_map : TextureHandle {},
        .vertex_buffer = geometry_vertex,
        .instance_buffer = geometry_instance,
        .indirect_buffer = geometry_indirect_live,
        .cluster_point_lights = cluster_point_lights_buf,
        .cluster_count = cluster_count_buf,
        .cluster_index = cluster_index_buf,
    });
    impl.geometry_pass->set_shadow_view_projection(shadow_view_projection);
    impl.ui_panel_pass->set_graph_bindings({
        .color_target = scene_color,
        .depth_target = scene_depth,
        .panel_texture = panel_texture,
        .overlay_texture = {},
        .panel_vertex_buffer = panel_vertices,
        .overlay_vertex_buffer = overlay_vertices,
    });
    graph.export_texture(backbuffer, ResourceState::Present);

    CallbackGraphPass geometry_upload_pass(
        MUST(String::from_utf8("GeometryUploads"sv)),
        [&](RenderPassBuilder& builder) {
            builder.set_queue_preference(QueueKind::Transfer);
            if (geometry_vertex_staging.value != 0) {
                builder.read_transfer(geometry_vertex_staging);
                builder.write_transfer(geometry_vertex);
            }
            if (geometry_instance_staging.value != 0) {
                builder.read_transfer(geometry_instance_staging);
                builder.write_transfer(geometry_instance);
            }
            if (geometry_indirect_staging.value != 0) {
                builder.read_transfer(geometry_indirect_staging);
                builder.write_transfer(geometry_indirect_ref);
            }
            if (geometry_aabb_staging.value != 0) {
                builder.read_transfer(geometry_aabb_staging);
                builder.write_transfer(geometry_aabb);
            }
        },
        [&](VkCommandBuffer cmd, RenderGraph&) {
            impl.geometry_pass->record_uploads(cmd);
        });
    if (impl.geometry_pass->has_upload_work())
        graph.add_pass(geometry_upload_pass);

    CallbackGraphPass ui_upload_pass(
        MUST(String::from_utf8("UIUploads"sv)),
        [&](RenderPassBuilder& builder) {
            builder.set_queue_preference(QueueKind::Transfer);
            // NOTE: Multi-panel textures use internal staging buffers.
            // We still need to declare dependencies if we want RenderGraph to handle barriers.
            // For now we just declare the fixed vertex buffers.
            if (panel_vertex_staging.value != 0) {
                builder.read_transfer(panel_vertex_staging);
                builder.write_transfer(panel_vertices);
            }
            if (overlay_vertex_staging.value != 0) {
                builder.read_transfer(overlay_vertex_staging);
                builder.write_transfer(overlay_vertices);
            }
        },
        [&](VkCommandBuffer cmd, RenderGraph&) {
            impl.ui_panel_pass->record_uploads(cmd);
        });
    if (impl.ui_panel_pass->has_upload_work())
        graph.add_pass(ui_upload_pass);

    {
        ZoneScopedN("Frame/AddPasses");
        if (has_geometry) {
            {
                if (shadow_pass_active) {
                    ZoneScopedN("Frame/AddPasses/ShadowPass");
                    impl.shadow_pass->set_graph_bindings({
                        .shadow_target = shadow_map,
                        .vertex_buffer = geometry_vertex,
                        .instance_buffer = geometry_instance,
                        .indirect_buffer = geometry_indirect_ref,
                    });
                    impl.shadow_pass->set_light_view_projection(shadow_view_projection);
                    impl.shadow_pass->set_draw_groups(impl.geometry_pass->draw_groups());
                    graph.add_pass(*impl.shadow_pass);
                }
            }

            {
                ZoneScopedN("Frame/AddPasses/ComputeCullPass");
                TRY(impl.compute_cull_pass->update_descriptor_set(ctx.device(), impl.geometry_pass->indirect_ref_buffer(), impl.geometry_pass->indirect_buffer(), impl.geometry_pass->aabb_world_buffer()));
                impl.compute_cull_pass->set_params(pc, static_cast<u32>(impl.geometry_pass->draw_groups().size()));
                impl.compute_cull_pass->set_graph_bindings({
                    .ref_indirect = geometry_indirect_ref,
                    .live_indirect = geometry_indirect_live,
                    .aabb_buffer = geometry_aabb,
                });
                graph.add_pass(*impl.compute_cull_pass);
            }

            if (impl.cluster_light_assign_pass && cluster_count_buf.value != 0) {
                ZoneScopedN("Frame/AddPasses/ClusterLightAssign");
                impl.cluster_light_assign_pass->set_graph_bindings({
                    .point_lights_buffer = cluster_point_lights_buf,
                    .count_buffer        = cluster_count_buf,
                    .index_buffer        = cluster_index_buf,
                });
                graph.add_pass(*impl.cluster_light_assign_pass);
            }

            if (depth_prepass_active) {
                ZoneScopedN("Frame/AddPasses/DepthPrepass");
                graph.add_pass(*impl.depth_prepass);
            }
        }

        {
            ZoneScopedN("Frame/AddPasses/ScenePasses");
            graph.add_pass(*impl.geometry_pass);
            graph.add_pass(*impl.ui_panel_pass);
        }

        {
            ZoneScopedN("Frame/AddPasses/PostPasses");
            impl.bloom_threshold_pass->set_graph_bindings({ .input_a = scene_color, .input_b = {}, .output = bloom_threshold });
            impl.bloom_blur_h_pass->set_graph_bindings({ .input_a = bloom_threshold, .input_b = {}, .output = bloom_blur_h });
            impl.bloom_blur_v_pass->set_graph_bindings({ .input_a = bloom_blur_h, .input_b = {}, .output = bloom_blur_v });
            impl.composite_pass->set_graph_bindings({ .input_a = scene_color, .input_b = bloom_blur_v, .output = composite_color });
            
            graph.add_pass(*impl.bloom_threshold_pass);
            graph.add_pass(*impl.bloom_blur_h_pass);
            graph.add_pass(*impl.bloom_blur_v_pass);
            graph.add_pass(*impl.composite_pass);
        }

        CallbackGraphPass present_pass(
            impl.names.present_name,
            [&](RenderPassBuilder& builder) {
                builder.set_queue_preference(QueueKind::Transfer);
                builder.read_transfer(composite_color);
                builder.write_transfer(backbuffer);
            },
            [&](VkCommandBuffer cmd, RenderGraph& graph) {
                VkImageCopy region {};
                region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                auto extent = graph.render_extent();
                region.extent = { extent.width, extent.height, 1 };
                vkCmdCopyImage(cmd,
                    graph.get_texture(composite_color), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    graph.get_texture(backbuffer), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &region);
            });
        {
            ZoneScopedN("Frame/AddPasses/PresentPass");
            graph.add_pass(present_pass);
        }

        VulkanDebug::cmd_begin_label(cmd, "Frame"sv, 0.2f, 0.8f, 0.2f);
        {
#if defined(TRACY_ENABLE)
            TracyVkZone(impl.backend->tracy_vk_ctx(), cmd, "GPU/Frame");
#endif
            TRY(graph.execute(cmd));
            if (auto validation = validate_compiled_frame_graph(graph, impl, world); validation.is_error()) {
                warnln("{}", graph.debug_description());
                return validation.release_error();
            }
        }
        VulkanDebug::cmd_end_label(cmd); // Frame
    }

    TRY(impl.backend->end_frame(impl.sc, img_idx, cmd));

    return {};
}

VulkanRenderer::VulkanRenderer(SDL_Window& window, VirtualFileSystem const* file_system) : m_window(window), m_file_system(file_system) {}
VulkanRenderer::~VulkanRenderer() { destroy(); }
ErrorOr<NonnullOwnPtr<VulkanRenderer>> VulkanRenderer::create(SDL_Window& window, VirtualFileSystem const* file_system) {
    auto r = TRY(adopt_nonnull_own_or_enomem(new (nothrow) VulkanRenderer(window, file_system)));
    TRY(r->initialize());
    return r;
}
ErrorOr<void> VulkanRenderer::initialize() {
#if !defined(USE_VULKAN)
    return Error::from_string_literal("Vulkan disabled");
#else
    m_context = TRY(VulkanContext::create(m_window));
    auto backend = TRY(VulkanBackend::create(*m_context));
    auto impl = adopt_own_if_nonnull(new (nothrow) Impl(move(backend)));
    if (!impl) return Error::from_errno(ENOMEM);
    impl->file_system = m_file_system;
    impl->mesh_library.emplace(m_file_system, &impl->material_library);
    impl->graph.emplace(*m_context);
    impl->web_view_manager = WebViewManager(m_context->supports_external_image_import(), m_context->device(), true);
    m_impl = move(impl);
    m_initialized = true;
    return {};
#endif
}
void VulkanRenderer::destroy() {
#if defined(USE_VULKAN)
    if (m_impl && m_context) {
        vkDeviceWaitIdle(m_context->device());
        destroy_vulkan_resources(*m_context, *m_impl);
    }
    m_impl = nullptr; m_context = nullptr;
#endif
    m_initialized = false;
}
ErrorOr<void> VulkanRenderer::draw_world(World const& w) {
#if defined(USE_VULKAN)
    if (!m_initialized || !m_impl || !m_context) return Error::from_string_literal("Renderer not ready");
    
    // Stage 1: build snapshot in-process and draw from it
    static Vector<u8> snapshot_buffer;
    if (snapshot_buffer.size() < MyceliumVR::DefaultSnapshotSlotBytes)
        snapshot_buffer.resize(MyceliumVR::DefaultSnapshotSlotBytes);
    
    // We need a WorldRuntime to build the snapshot, but we only have World.
    // In Stage 1, we can just call build_render_snapshot directly if we had a helper.
    // For now, let's assume we are called from Engine which has both.
    // Wait, draw_world is the OLD way. Engine should call draw_snapshot.
    
    return draw_frame(*m_context, *m_impl, m_impl->camera_state, &w);
#else
    (void)w; return {};
#endif
}

ErrorOr<void> VulkanRenderer::draw_snapshot(ReadonlySpan<u8> slot, ReadonlySpan<u8> static_scene) {
#if defined(USE_VULKAN)
    if (!m_initialized || !m_impl || !m_context) return Error::from_string_literal("Renderer not ready");
    return draw_frame(*m_context, *m_impl, m_impl->camera_state, nullptr, &slot, static_scene.is_empty() ? nullptr : &static_scene);
#else
    (void)slot; (void)static_scene; return {};
#endif
}
ErrorOr<void> VulkanRenderer::draw_test_triangle() {
#if defined(USE_VULKAN)
    if (!m_initialized || !m_impl || !m_context) return Error::from_string_literal("Renderer not ready");
    return draw_frame(*m_context, *m_impl, m_impl->camera_state);
#else
    return {};
#endif
}
void VulkanRenderer::register_mesh(u32 handle, ByteString const& virtual_path)
{
    if (m_impl) m_impl->registered_meshes.set(handle, virtual_path);
}

void VulkanRenderer::register_material(u32 handle, ByteString const& virtual_path)
{
    if (m_impl) m_impl->registered_materials.set(handle, virtual_path);
}

void VulkanRenderer::register_panel(u32 handle, u32 entity_id, ByteString const& url, float width, float height)
{
    if (m_impl) {
        m_impl->registered_panels.set(handle, { entity_id, url, width, height });
        // Extract world_id from handle (top 8 bits)
        u32 world_id = handle >> 24;
        m_impl->web_view_manager.register_panel(world_id, entity_id, MUST(String::from_utf8(url.view())), width, height);
    }
}

void VulkanRenderer::force_rebuild_scene()
{
    if (m_impl)
        m_impl->geometry_pass->force_rebuild();
}

void VulkanRenderer::sync_web_views(u32 world_id, World const& world)
{
    if (m_impl)
        m_impl->web_view_manager.sync_world(world_id, world);
}

void VulkanRenderer::handle_web_view_event(SDL_Event const& event)
{
    if (m_impl)
        m_impl->web_view_manager.handle_sdl_event(event);
}

ErrorOr<void> VulkanRenderer::sync_snapshot_buffers()
{
    if (!m_impl || !m_impl->ui_panel_pass)
        return {};

    auto dirty_panels = TRY(m_impl->web_view_manager.snapshot_dirty_panels());
    for (auto& panel : dirty_panels) {
        if (panel.vulkan_image == VK_NULL_HANDLE)
            continue;
        m_impl->ui_panel_pass->set_external_panel_image(panel.handle, panel.vulkan_image, panel.width, panel.height);
    }
    return {};
}

void VulkanRenderer::post_render()
{
#if defined(USE_VULKAN)
    if (!m_impl)
        return;

    if (m_context && m_impl->web_view_manager.has_pending_presented_vulkan_panels())
        vkQueueWaitIdle(m_context->graphics_queue());

    m_impl->web_view_manager.post_render();
#endif
}

void VulkanRenderer::wait_for_device_idle()
{
#if defined(USE_VULKAN)
    if (m_context)
        vkDeviceWaitIdle(m_context->device());
#endif
}

void VulkanRenderer::unload_world_resources(u32 world_id)
{
    if (!m_impl) return;

    // 1. Clear handle mappings for this world
    Vector<u32> mesh_handles_to_remove;
    for (auto const& entry : m_impl->registered_meshes) {
        if ((entry.key >> 24) == world_id) mesh_handles_to_remove.append(entry.key);
    }
    for (auto handle : mesh_handles_to_remove) m_impl->registered_meshes.remove(handle);

    Vector<u32> material_handles_to_remove;
    for (auto const& entry : m_impl->registered_materials) {
        if ((entry.key >> 24) == world_id) material_handles_to_remove.append(entry.key);
    }
    for (auto handle : material_handles_to_remove) m_impl->registered_materials.remove(handle);

    Vector<u32> panel_handles_to_remove;
    for (auto const& entry : m_impl->registered_panels) {
        if ((entry.key >> 24) == world_id) panel_handles_to_remove.append(entry.key);
    }
    for (auto handle : panel_handles_to_remove) m_impl->registered_panels.remove(handle);

    // 2. Instruct libraries to drop their cached state.
    if (m_impl->mesh_library.has_value())
        m_impl->mesh_library->unload_world_resources(world_id);
    
    m_impl->material_library.unload_world_resources(world_id);

    if (m_impl->m_texture_library.has_value())
        m_impl->m_texture_library->unload_world_resources(world_id);

    // 3. Clear WebViewManager panels for this world
    m_impl->web_view_manager.unload_world_panels(world_id);
}

void VulkanRenderer::resize(int w, int h) { if (m_impl) { m_impl->drawable_width = w > 0 ? w : 1; m_impl->drawable_height = h > 0 ? h : 1; m_impl->swapchain_dirty = true; } }
void VulkanRenderer::set_camera_state(CameraState const& s) { if (m_impl) m_impl->camera_state = s; }
VulkanRenderer::CameraState VulkanRenderer::camera_state() const { return m_impl ? m_impl->camera_state : CameraState {}; }
void VulkanRenderer::set_scene_light(SceneLightData const& l) { if (m_impl) m_impl->scene_light = l; }
VulkanRenderer::SceneLightData VulkanRenderer::scene_light() const { return m_impl ? m_impl->scene_light : SceneLightData {}; }
void VulkanRenderer::set_shadow_quality(ShadowQuality shadow_quality) { if (m_impl) m_impl->shadow_quality = shadow_quality; }
VulkanRenderer::ShadowQuality VulkanRenderer::shadow_quality() const { return m_impl ? m_impl->shadow_quality : default_shadow_quality; }
void VulkanRenderer::wait_for_graphics_queue_idle()
{
#if defined(USE_VULKAN)
    if (m_context)
        vkQueueWaitIdle(m_context->graphics_queue());
#endif
}

bool VulkanRenderer::supports_external_image_import() const
{
#if defined(USE_VULKAN)
    return m_context && m_context->supports_external_image_import();
#else
    return false;
#endif
}

VkDevice VulkanRenderer::vulkan_device() const
{
#if defined(USE_VULKAN)
    return m_context ? m_context->device() : VK_NULL_HANDLE;
#else
    return VK_NULL_HANDLE;
#endif
}
u32 VulkanRenderer::last_frame_draw_calls() const
{
#if defined(USE_VULKAN)
    if (!m_impl || !m_impl->geometry_pass)
        return 0;
    return static_cast<u32>(m_impl->geometry_pass->draw_groups().size());
#else
    return 0;
#endif
}

u32 VulkanRenderer::last_frame_triangle_count() const
{
#if defined(USE_VULKAN)
    if (!m_impl || !m_impl->geometry_pass)
        return 0;
    u32 total = 0;
    for (auto const& g : m_impl->geometry_pass->draw_groups())
        total += (g.vertex_count / 3) * g.instance_count;
    return total;
#else
    return 0;
#endif
}

Vector<VulkanRenderer::PassTiming> VulkanRenderer::last_frame_timings() const
{
#if defined(USE_VULKAN)
    if (!m_impl || !m_impl->graph.has_value())
        return {};
    Vector<PassTiming> result;
    for (auto const& t : m_impl->graph->last_frame_timings())
        result.append({ t.name, t.gpu_ms });
    return result;
#else
    return {};
#endif
}

void VulkanRenderer::clear_overlay_bitmap() { }
void VulkanRenderer::set_external_overlay_image(VkImage image, u32 w, u32 h) {
    if (m_impl && m_impl->ui_panel_pass) {
        if (m_impl->ui_panel_pass->external_overlay_image() != image) {
            dbgln("VulkanRenderer: set_external_overlay_image image={} size={}x{}",
                (void*)image,
                w,
                h);
        }
        m_impl->ui_panel_pass->set_external_overlay_image(image, w, h);
    }
}
void VulkanRenderer::clear_external_overlay() { if (m_impl && m_impl->ui_panel_pass) m_impl->ui_panel_pass->clear_external_overlay(); }

} // namespace MyceliumVR
#endif
