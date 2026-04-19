/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanPipeline.h"
#include "../../Support/VirtualFileSystem.h"
#include <AK/ByteString.h>
#include <AK/ScopeGuard.h>

#if !defined(MYCELIUMVR_SHADER_DIRECTORY)
#    define MYCELIUMVR_SHADER_DIRECTORY "."
#endif

#if !defined(MYCELIUMVR_SHADER_SOURCE_DIRECTORY)
#    define MYCELIUMVR_SHADER_SOURCE_DIRECTORY "."
#endif

namespace MyceliumVR {

static VkCullModeFlags to_vk_cull_mode(VulkanWorldPipeline::CullMode cull_mode)
{
    switch (cull_mode) {
    case VulkanWorldPipeline::CullMode::Disabled:
        return VK_CULL_MODE_NONE;
    case VulkanWorldPipeline::CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    case VulkanWorldPipeline::CullMode::Back:
    default:
        return VK_CULL_MODE_BACK_BIT;
    }
}

ErrorOr<VkShaderModule> VulkanPipelineManager::create_shader_module(VkDevice device, ShaderBytecode const& bytecode)
{
    VkShaderModuleCreateInfo create_info {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = bytecode.byte_size();
    create_info.pCode = bytecode.data();
    VkShaderModule shader_module { VK_NULL_HANDLE };
    TRY(check_vulkan_result(vkCreateShaderModule(device, &create_info, nullptr, &shader_module), "vkCreateShaderModule failed"sv));
    return shader_module;
}

ErrorOr<void> VulkanPipelineManager::ensure_shader_bytecode_loaded(
    VkDevice,
    ShaderBytecode& vertex_bytecode,
    ShaderBytecode& fragment_bytecode,
    Optional<ShaderBytecode>& panel_vertex_bytecode,
    Optional<ShaderBytecode>& panel_fragment_bytecode,
    Optional<ShaderBytecode>& post_vertex_bytecode,
    Optional<ShaderBytecode>& post_fragment_bytecode,
    Optional<ShaderBytecode>& shadow_vertex_bytecode,
    ShaderBytecode& cull_bytecode,
    Optional<ShaderBytecode>& cluster_compute_bytecode,
    bool& out_loaded)
{
    if (out_loaded) return {};

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
        auto post_vertex_shader_source   = TRY(shader_file_system.read_file("mycelium://shader-source/FullscreenPost.vert"sv));
        auto post_fragment_shader_source = TRY(shader_file_system.read_file("mycelium://shader-source/FullscreenPost.frag"sv));
        auto shadow_vertex_shader_source = TRY(shader_file_system.read_file("mycelium://shader-source/ShadowDepth.vert"sv));
        auto cull_compute_source = TRY(shader_file_system.read_file("mycelium://shader-source/FrustumCull.comp"sv));
        vertex_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, { .stage = ShaderStage::Vertex, .language = ShaderSourceLanguage::GLSL, .source_name = "mycelium://shader-source/WorldMesh.vert"sv, .source = StringView { world_vertex_source } }, "mycelium://shader-cache/"sv));
        fragment_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, { .stage = ShaderStage::Fragment, .language = ShaderSourceLanguage::GLSL, .source_name = "mycelium://shader-source/WorldMesh.frag"sv, .source = StringView { world_fragment_source } }, "mycelium://shader-cache/"sv));
        panel_vertex_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, { .stage = ShaderStage::Vertex, .language = ShaderSourceLanguage::GLSL, .source_name = "mycelium://shader-source/TexturedQuad.vert"sv, .source = StringView { panel_vertex_shader_source } }, "mycelium://shader-cache/"sv));
        panel_fragment_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, { .stage = ShaderStage::Fragment, .language = ShaderSourceLanguage::GLSL, .source_name = "mycelium://shader-source/TexturedQuad.frag"sv, .source = StringView { panel_fragment_shader_source } }, "mycelium://shader-cache/"sv));
        post_vertex_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, { .stage = ShaderStage::Vertex, .language = ShaderSourceLanguage::GLSL, .source_name = "mycelium://shader-source/FullscreenPost.vert"sv, .source = StringView { post_vertex_shader_source } }, "mycelium://shader-cache/"sv));
        post_fragment_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, { .stage = ShaderStage::Fragment, .language = ShaderSourceLanguage::GLSL, .source_name = "mycelium://shader-source/FullscreenPost.frag"sv, .source = StringView { post_fragment_shader_source } }, "mycelium://shader-cache/"sv));
        shadow_vertex_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, { .stage = ShaderStage::Vertex, .language = ShaderSourceLanguage::GLSL, .source_name = "mycelium://shader-source/ShadowDepth.vert"sv, .source = StringView { shadow_vertex_shader_source } }, "mycelium://shader-cache/"sv));
        cull_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, { .stage = ShaderStage::Compute, .language = ShaderSourceLanguage::GLSL, .source_name = "mycelium://shader-source/FrustumCull.comp"sv, .source = StringView { cull_compute_source } }, "mycelium://shader-cache/"sv));
        auto cluster_compute_source = TRY(shader_file_system.read_file("mycelium://shader-source/ClusterLightAssign.comp"sv));
        cluster_compute_bytecode = TRY(shader_compiler.load_or_compile_glsl_to_spirv(shader_file_system, { .stage = ShaderStage::Compute, .language = ShaderSourceLanguage::GLSL, .source_name = "mycelium://shader-source/ClusterLightAssign.comp"sv, .source = StringView { cluster_compute_source } }, "mycelium://shader-cache/"sv));
    } else {
        vertex_bytecode = TRY(shader_compiler.load_spirv(shader_file_system, "mycelium://shaders/ProbeTriangle.vert.spv"sv));
        fragment_bytecode = TRY(shader_compiler.load_spirv(shader_file_system, "mycelium://shaders/ProbeTriangle.frag.spv"sv));
    }
    out_loaded = true;
    return {};
}

ErrorOr<VulkanWorldPipeline> VulkanPipelineManager::create_world_pipeline(VkDevice device, VkFormat color_format, VkFormat depth_format, VkDescriptorSetLayout pbr_layout, ShaderBytecode const& vertex_bytecode, ShaderBytecode const& fragment_bytecode)
{
    VulkanWorldPipeline p;
    p.pbr_layout = pbr_layout; // Externally managed (bindless set)
    {
        VkDescriptorSetLayoutBinding binding { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &binding };
        TRY(check_vulkan_result(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &p.light_layout), "vkCreateDescriptorSetLayout light failed"sv));
    }
    {
        VkDescriptorSetLayoutBinding binding { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &binding };
        TRY(check_vulkan_result(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &p.material_layout), "vkCreateDescriptorSetLayout material failed"sv));
    }
    {
        VkDescriptorSetLayoutBinding binding { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &binding };
        TRY(check_vulkan_result(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &p.shadow_layout), "vkCreateDescriptorSetLayout shadow failed"sv));
    }
    {
        VkDescriptorSetLayoutBinding bindings[3] {};
        for (u32 i = 0; i < 3; ++i) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, bindings };
        TRY(check_vulkan_result(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &p.cluster_layout), "vkCreateDescriptorSetLayout cluster failed"sv));
    }
    VkPushConstantRange pc_range { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants) };
    Array<VkDescriptorSetLayout, 5> layouts { p.pbr_layout, p.light_layout, p.material_layout, p.shadow_layout, p.cluster_layout };
    VkPipelineLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, (u32)layouts.size(), layouts.data(), 1, &pc_range };
    TRY(check_vulkan_result(vkCreatePipelineLayout(device, &layout_info, nullptr, &p.layout), "vkCreatePipelineLayout failed"sv));

    auto vs = TRY(create_shader_module(device, vertex_bytecode));
    auto fs = TRY(create_shader_module(device, fragment_bytecode));
    ScopeGuard sg = [&] { vkDestroyShaderModule(device, vs, nullptr); vkDestroyShaderModule(device, fs, nullptr); };

    VkPipelineShaderStageCreateInfo stages[2] { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr }, { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr } };
    VkVertexInputBindingDescription bin_descs[2] { 
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }, 
        { 1, sizeof(GPUInstanceData), VK_VERTEX_INPUT_RATE_INSTANCE } 
    };
    Array<VkVertexInputAttributeDescription, 10> attrs {};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, (u32)__builtin_offsetof(Vertex, position) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, (u32)__builtin_offsetof(Vertex, normal) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, (u32)__builtin_offsetof(Vertex, color) };
    attrs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT, (u32)__builtin_offsetof(Vertex, uv) };
    // Instance attributes (model matrix) locations 4,5,6,7
    for (int i = 0; i < 4; i++) attrs[4 + i] = { (u32)(4 + i), 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32)(i * 16) };
    // Instance attribute (material index) location 8
    attrs[8] = { 8, 1, VK_FORMAT_R32_UINT, (u32)__builtin_offsetof(GPUInstanceData, material_index) };
    // Vertex attribute (tangent) location 9
    attrs[9] = { 9, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (u32)__builtin_offsetof(Vertex, tangent) };

    VkPipelineVertexInputStateCreateInfo vi { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, 2, bin_descs, (u32)attrs.size(), attrs.data() };
    VkPipelineInputAssemblyStateCreateInfo ia { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo vp { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
    VkPipelineRasterizationStateCreateInfo rs { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f };
    VkPipelineMultisampleStateCreateInfo ms { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0, nullptr, VK_FALSE, VK_FALSE };
    
    // Dynamic Rendering configuration
    VkPipelineRenderingCreateInfo rendering_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
        .depthAttachmentFormat = depth_format,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    // Reverse-Z: fragments closer to camera have higher depth values.
    // GREATER_OR_EQUAL lets opaque geometry re-pass the test when a depth prepass
    // has already written the same depth value, enabling hardware early-z.
    VkPipelineDepthStencilStateCreateInfo ds { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_FALSE, VK_FALSE, {}, {}, 0, 0 };
    VkPipelineColorBlendAttachmentState cb_as { VK_FALSE, {}, {}, {}, {}, {}, {}, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo cb { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &cb_as, {0,0,0,0} };
    VkDynamicState dyn_states[2] { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dyn_states };

    VkGraphicsPipelineCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .flags = 0,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pTessellationState = nullptr,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dyn,
        .layout = p.layout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };
    auto create_cull_variants = [&](VkPipeline& back, VkPipeline& front, VkPipeline& no_cull, StringView label) -> ErrorOr<void> {
        rs.cullMode = to_vk_cull_mode(VulkanWorldPipeline::CullMode::Back);
        TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &back), MUST(String::formatted("vkCreateGraphicsPipelines {}-back failed", label))));
        rs.cullMode = to_vk_cull_mode(VulkanWorldPipeline::CullMode::Front);
        TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &front), MUST(String::formatted("vkCreateGraphicsPipelines {}-front failed", label))));
        rs.cullMode = to_vk_cull_mode(VulkanWorldPipeline::CullMode::Disabled);
        TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &no_cull), MUST(String::formatted("vkCreateGraphicsPipelines {}-nocull failed", label))));
        return {};
    };

    // Depth-read-only helper: opaque state, depth test on, depth write off.
    // Used when a depth prepass has already written correct depth values.
    auto create_dro_variants = [&](VkPipeline& ob, VkPipeline& of, VkPipeline& onc) -> ErrorOr<void> {
        cb_as.blendEnable = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;
        return create_cull_variants(ob, of, onc, "world dro"sv);
    };

    struct FragmentSpecializationData {
        VkBool32 has_normal_map;
        int32_t alpha_mode;
    };
    VkSpecializationMapEntry spec_entries[2] {
        { .constantID = 0, .offset = static_cast<u32>(__builtin_offsetof(FragmentSpecializationData, has_normal_map)), .size = sizeof(VkBool32) },
        { .constantID = 1, .offset = static_cast<u32>(__builtin_offsetof(FragmentSpecializationData, alpha_mode)), .size = sizeof(int32_t) },
    };
    auto set_fragment_specialization = [&](bool has_normal_map, VulkanWorldPipeline::AlphaMode alpha_mode) {
        static FragmentSpecializationData data {};
        static VkSpecializationInfo spec_info {
            .mapEntryCount = 2,
            .pMapEntries = spec_entries,
            .dataSize = sizeof(FragmentSpecializationData),
            .pData = &data,
        };
        data.has_normal_map = has_normal_map ? VK_TRUE : VK_FALSE;
        data.alpha_mode = static_cast<int32_t>(alpha_mode);
        stages[1].pSpecializationInfo = &spec_info;
    };

    auto create_alpha_family = [&](bool has_normal_map) -> ErrorOr<void> {
        cb_as.blendEnable = VK_FALSE;
        ds.depthWriteEnable = VK_TRUE;

        set_fragment_specialization(has_normal_map, VulkanWorldPipeline::AlphaMode::Opaque);
        TRY(create_cull_variants(
            has_normal_map ? p.opaque_backface_pipeline : p.opaque_backface_no_nmap_pipeline,
            has_normal_map ? p.opaque_frontface_pipeline : p.opaque_frontface_no_nmap_pipeline,
            has_normal_map ? p.opaque_no_cull_pipeline : p.opaque_no_cull_no_nmap_pipeline,
            has_normal_map ? "world opaque nmap"sv : "world opaque no-nmap"sv));

        set_fragment_specialization(has_normal_map, VulkanWorldPipeline::AlphaMode::Clip);
        TRY(create_cull_variants(
            has_normal_map ? p.clip_backface_pipeline : p.clip_backface_no_nmap_pipeline,
            has_normal_map ? p.clip_frontface_pipeline : p.clip_frontface_no_nmap_pipeline,
            has_normal_map ? p.clip_no_cull_pipeline : p.clip_no_cull_no_nmap_pipeline,
            has_normal_map ? "world clip nmap"sv : "world clip no-nmap"sv));

        set_fragment_specialization(has_normal_map, VulkanWorldPipeline::AlphaMode::Hash);
        TRY(create_cull_variants(
            has_normal_map ? p.hash_backface_pipeline : p.hash_backface_no_nmap_pipeline,
            has_normal_map ? p.hash_frontface_pipeline : p.hash_frontface_no_nmap_pipeline,
            has_normal_map ? p.hash_no_cull_pipeline : p.hash_no_cull_no_nmap_pipeline,
            has_normal_map ? "world hash nmap"sv : "world hash no-nmap"sv));

        cb_as.blendEnable = VK_TRUE;
        cb_as.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cb_as.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cb_as.colorBlendOp = VK_BLEND_OP_ADD;
        cb_as.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cb_as.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cb_as.alphaBlendOp = VK_BLEND_OP_ADD;
        ds.depthWriteEnable = VK_FALSE;
        set_fragment_specialization(has_normal_map, VulkanWorldPipeline::AlphaMode::Blend);
        TRY(create_cull_variants(
            has_normal_map ? p.blend_backface_pipeline : p.blend_backface_no_nmap_pipeline,
            has_normal_map ? p.blend_frontface_pipeline : p.blend_frontface_no_nmap_pipeline,
            has_normal_map ? p.blend_no_cull_pipeline : p.blend_no_cull_no_nmap_pipeline,
            has_normal_map ? "world blend nmap"sv : "world blend no-nmap"sv));

        cb_as.blendEnable = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;
        set_fragment_specialization(has_normal_map, VulkanWorldPipeline::AlphaMode::Opaque);
        TRY(create_dro_variants(
            has_normal_map ? p.dro_backface_pipeline : p.dro_backface_no_nmap_pipeline,
            has_normal_map ? p.dro_frontface_pipeline : p.dro_frontface_no_nmap_pipeline,
            has_normal_map ? p.dro_no_cull_pipeline : p.dro_no_cull_no_nmap_pipeline));

        return {};
    };

    TRY(create_alpha_family(true));
    TRY(create_alpha_family(false));
    stages[1].pSpecializationInfo = nullptr;

    return p;
}

ErrorOr<VulkanPanelPipeline> VulkanPipelineManager::create_panel_pipeline(VkDevice device, VkFormat color_format, VkFormat depth_format, ShaderBytecode const& vertex_bytecode, ShaderBytecode const& fragment_bytecode)
{
    VulkanPanelPipeline p;
    VkDescriptorSetLayoutBinding binding { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &binding };
    TRY(check_vulkan_result(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &p.layout), "vkCreateDescriptorSetLayout failed"sv));
    VkPushConstantRange pc_range { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants) };
    VkPipelineLayoutCreateInfo layout_info_p { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &p.layout, 1, &pc_range };
    TRY(check_vulkan_result(vkCreatePipelineLayout(device, &layout_info_p, nullptr, &p.pipeline_layout), "vkCreatePipelineLayout failed"sv));

    auto vs = TRY(create_shader_module(device, vertex_bytecode));
    auto fs = TRY(create_shader_module(device, fragment_bytecode));
    ScopeGuard sg = [&] { vkDestroyShaderModule(device, vs, nullptr); vkDestroyShaderModule(device, fs, nullptr); };

    VkPipelineShaderStageCreateInfo stages[2] { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr }, { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr } };
    VkVertexInputBindingDescription b_desc { 0, sizeof(TexturedVertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription a_descs[2] { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, (u32)__builtin_offsetof(TexturedVertex, position) }, { 1, 0, VK_FORMAT_R32G32_SFLOAT, (u32)__builtin_offsetof(TexturedVertex, uv) } };
    VkPipelineVertexInputStateCreateInfo vi { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, 1, &b_desc, 2, a_descs };
    VkPipelineInputAssemblyStateCreateInfo ia { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo vp { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
    VkPipelineRasterizationStateCreateInfo rs { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f };
    VkPipelineMultisampleStateCreateInfo ms { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0, nullptr, VK_FALSE, VK_FALSE };
    
    // Dynamic Rendering configuration
    VkPipelineRenderingCreateInfo rendering_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
        .depthAttachmentFormat = depth_format,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    VkPipelineDepthStencilStateCreateInfo ds { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER, VK_FALSE, VK_FALSE, {}, {}, 0, 0 };
    VkPipelineColorBlendAttachmentState cb_as { VK_TRUE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo cb { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &cb_as, {0,0,0,0} };
    VkDynamicState dyn_states[2] { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dyn_states };

    VkGraphicsPipelineCreateInfo info { 
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, 
        .pNext = &rendering_info, 
        .flags = 0, 
        .stageCount = 2, 
        .pStages = stages, 
        .pVertexInputState = &vi, 
        .pInputAssemblyState = &ia, 
        .pTessellationState = nullptr, 
        .pViewportState = &vp, 
        .pRasterizationState = &rs, 
        .pMultisampleState = &ms, 
        .pDepthStencilState = &ds, 
        .pColorBlendState = &cb, 
        .pDynamicState = &dyn, 
        .layout = p.pipeline_layout, 
        .renderPass = VK_NULL_HANDLE, 
        .subpass = 0, 
        .basePipelineHandle = VK_NULL_HANDLE, 
        .basePipelineIndex = -1 
    };
    TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p.pipeline), "vkCreateGraphicsPipelines failed"sv));
    return p;
}

ErrorOr<VulkanPostPipeline> VulkanPipelineManager::create_post_pipeline(VkDevice device, VkFormat color_format, ShaderBytecode const& vertex_bytecode, ShaderBytecode const& fragment_bytecode)
{
    VulkanPostPipeline p;
    VkDescriptorSetLayoutBinding bindings[2] {};
    for (u32 i = 0; i < 2; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2, bindings };
    TRY(check_vulkan_result(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &p.layout), "vkCreateDescriptorSetLayout post failed"sv));

    struct PostPushConstants {
        float inv_width;
        float inv_height;
        float threshold;
        float bloom_strength;
        u32 mode;
    };
    VkPushConstantRange pc_range { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPushConstants) };
    VkPipelineLayoutCreateInfo pipeline_layout_info { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &p.layout, 1, &pc_range };
    TRY(check_vulkan_result(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &p.pipeline_layout), "vkCreatePipelineLayout post failed"sv));

    auto vs = TRY(create_shader_module(device, vertex_bytecode));
    auto fs = TRY(create_shader_module(device, fragment_bytecode));
    ScopeGuard sg = [&] { vkDestroyShaderModule(device, vs, nullptr); vkDestroyShaderModule(device, fs, nullptr); };

    VkPipelineShaderStageCreateInfo stages[2] {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr }
    };
    VkPipelineVertexInputStateCreateInfo vi { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, 0, nullptr, 0, nullptr };
    VkPipelineInputAssemblyStateCreateInfo ia { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo vp { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
    VkPipelineRasterizationStateCreateInfo rs { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f };
    VkPipelineMultisampleStateCreateInfo ms { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0, nullptr, VK_FALSE, VK_FALSE };
    VkPipelineRenderingCreateInfo rendering_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
        .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };
    VkPipelineDepthStencilStateCreateInfo ds { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_FALSE, VK_FALSE, {}, {}, 0, 0 };
    VkPipelineColorBlendAttachmentState cb_as { VK_FALSE, {}, {}, {}, {}, {}, {}, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo cb { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &cb_as, { 0, 0, 0, 0 } };
    VkDynamicState dyn_states[2] { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dyn_states };

    VkGraphicsPipelineCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .flags = 0,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pTessellationState = nullptr,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dyn,
        .layout = p.pipeline_layout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };
    TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p.pipeline), "vkCreateGraphicsPipelines post failed"sv));
    return p;
}

ErrorOr<VulkanShadowPipeline> VulkanPipelineManager::create_shadow_pipeline(VkDevice device, VkFormat depth_format, ShaderBytecode const& vertex_bytecode)
{
    VulkanShadowPipeline p;
    VkPushConstantRange pc_range { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4) };
    VkPipelineLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 0, nullptr, 1, &pc_range };
    TRY(check_vulkan_result(vkCreatePipelineLayout(device, &layout_info, nullptr, &p.layout), "vkCreatePipelineLayout shadow failed"sv));

    auto vs = TRY(create_shader_module(device, vertex_bytecode));
    ScopeGuard sg = [&] { vkDestroyShaderModule(device, vs, nullptr); };

    VkPipelineShaderStageCreateInfo stage { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr };
    VkVertexInputBindingDescription bin_descs[2] { 
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }, 
        { 1, sizeof(GPUInstanceData), VK_VERTEX_INPUT_RATE_INSTANCE } 
    };
    Array<VkVertexInputAttributeDescription, 6> attrs {};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, (u32)__builtin_offsetof(Vertex, position) };
    for (int i = 0; i < 4; ++i)
        attrs[1 + i] = { (u32)(4 + i), 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32)(i * 16) };
    // location 8: material index (unused in shadow but part of Instance buffer)
    attrs[5] = { 8, 1, VK_FORMAT_R32_UINT, (u32)__builtin_offsetof(GPUInstanceData, material_index) };

    VkPipelineVertexInputStateCreateInfo vi { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, 2, bin_descs, (u32)attrs.size(), attrs.data() };
    VkPipelineInputAssemblyStateCreateInfo ia { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo vp { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
    VkPipelineRasterizationStateCreateInfo rs { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_TRUE, 0.0f, 0.0f, 0.0f, 1.0f };
    VkPipelineMultisampleStateCreateInfo ms { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0, nullptr, VK_FALSE, VK_FALSE };
    VkPipelineRenderingCreateInfo rendering_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = 0,
        .colorAttachmentCount = 0,
        .pColorAttachmentFormats = nullptr,
        .depthAttachmentFormat = depth_format,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };
    VkPipelineDepthStencilStateCreateInfo ds { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, VK_FALSE, {}, {}, 0, 0 };
    VkPipelineColorBlendStateCreateInfo cb { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 0, nullptr, { 0, 0, 0, 0 } };
    VkDynamicState dyn_states[3] { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
    VkPipelineDynamicStateCreateInfo dyn { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 3, dyn_states };

    VkGraphicsPipelineCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .flags = 0,
        .stageCount = 1,
        .pStages = &stage,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pTessellationState = nullptr,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dyn,
        .layout = p.layout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };
    TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p.backface_pipeline), "vkCreateGraphicsPipelines shadow backface failed"sv));

    rs.cullMode = VK_CULL_MODE_FRONT_BIT;
    TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p.frontface_pipeline), "vkCreateGraphicsPipelines shadow frontface failed"sv));

    rs.cullMode = VK_CULL_MODE_NONE;
    TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p.no_cull_pipeline), "vkCreateGraphicsPipelines shadow no-cull failed"sv));

    return p;
}

ErrorOr<VulkanDepthPipeline> VulkanPipelineManager::create_depth_prepass_pipeline(VkDevice device, VkFormat depth_format, ShaderBytecode const& vertex_bytecode)
{
    VulkanDepthPipeline p;
    VkPushConstantRange pc_range { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4) };
    VkPipelineLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 0, nullptr, 1, &pc_range };
    TRY(check_vulkan_result(vkCreatePipelineLayout(device, &layout_info, nullptr, &p.layout), "vkCreatePipelineLayout depth-prepass failed"sv));

    auto vs = TRY(create_shader_module(device, vertex_bytecode));
    ScopeGuard sg = [&] { vkDestroyShaderModule(device, vs, nullptr); };

    VkPipelineShaderStageCreateInfo stage { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr };
    VkVertexInputBindingDescription bin_descs[2] {
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX },
        { 1, sizeof(GPUInstanceData), VK_VERTEX_INPUT_RATE_INSTANCE }
    };
    Array<VkVertexInputAttributeDescription, 6> attrs {};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, (u32)__builtin_offsetof(Vertex, position) };
    for (int i = 0; i < 4; ++i)
        attrs[1 + i] = { (u32)(4 + i), 1, VK_FORMAT_R32G32B32A32_SFLOAT, (u32)(i * 16) };
    attrs[5] = { 8, 1, VK_FORMAT_R32_UINT, (u32)__builtin_offsetof(GPUInstanceData, material_index) };

    VkPipelineVertexInputStateCreateInfo vi { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, 2, bin_descs, (u32)attrs.size(), attrs.data() };
    VkPipelineInputAssemblyStateCreateInfo ia { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
    VkPipelineViewportStateCreateInfo vp { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
    VkPipelineRasterizationStateCreateInfo rs { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f };
    VkPipelineMultisampleStateCreateInfo ms { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0, nullptr, VK_FALSE, VK_FALSE };
    VkPipelineRenderingCreateInfo rendering_info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = 0,
        .colorAttachmentCount = 0,
        .pColorAttachmentFormats = nullptr,
        .depthAttachmentFormat = depth_format,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };
    // Reverse-Z: GREATER passes fragments closer to camera (higher depth value).
    VkPipelineDepthStencilStateCreateInfo ds { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER, VK_FALSE, VK_FALSE, {}, {}, 0, 0 };
    VkPipelineColorBlendStateCreateInfo cb { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 0, nullptr, { 0, 0, 0, 0 } };
    VkDynamicState dyn_states[2] { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dyn_states };

    VkGraphicsPipelineCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .flags = 0,
        .stageCount = 1,
        .pStages = &stage,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pTessellationState = nullptr,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dyn,
        .layout = p.layout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };
    TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p.backface_pipeline), "vkCreateGraphicsPipelines depth-prepass backface failed"sv));

    rs.cullMode = VK_CULL_MODE_FRONT_BIT;
    TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p.frontface_pipeline), "vkCreateGraphicsPipelines depth-prepass frontface failed"sv));

    rs.cullMode = VK_CULL_MODE_NONE;
    TRY(check_vulkan_result(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p.no_cull_pipeline), "vkCreateGraphicsPipelines depth-prepass no-cull failed"sv));

    return p;
}

}
