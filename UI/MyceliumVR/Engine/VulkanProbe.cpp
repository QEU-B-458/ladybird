/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanProbe.h"

#include "ShaderCompiler.h"
#include "VirtualFileSystem.h"

#include <AK/Array.h>
#include <AK/ByteString.h>
#include <AK/Format.h>
#include <AK/Vector.h>
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

static ErrorOr<void> draw_one_swapchain_triangle(VkPhysicalDevice physical_device, VkDevice device, VkSurfaceKHR surface, VkQueue graphics_queue, u32 graphics_queue_family)
{
    VirtualFileSystem shader_file_system;
    TRY(shader_file_system.mount_directory("mycelium://shaders/"sv, ByteString::formatted("{}", MYCELIUMVR_SHADER_DIRECTORY)));
    TRY(shader_file_system.mount_directory("mycelium://shader-source/"sv, ByteString::formatted("{}", MYCELIUMVR_SHADER_SOURCE_DIRECTORY)));

    ShaderCompiler shader_compiler(ShaderCompilerBackend::ShaderC);
    ShaderBytecode vertex_shader_bytecode;
    ShaderBytecode fragment_shader_bytecode;
    if (shader_compiler.supports_runtime_glsl_compilation()) {
        auto vertex_shader_source = TRY(shader_file_system.read_file("mycelium://shader-source/ProbeTriangle.vert"sv));
        auto fragment_shader_source = TRY(shader_file_system.read_file("mycelium://shader-source/ProbeTriangle.frag"sv));
        vertex_shader_bytecode = TRY(shader_compiler.compile_glsl_to_spirv({
            .stage = ShaderStage::Vertex,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/ProbeTriangle.vert"sv,
            .source = StringView { vertex_shader_source },
        }));
        fragment_shader_bytecode = TRY(shader_compiler.compile_glsl_to_spirv({
            .stage = ShaderStage::Fragment,
            .language = ShaderSourceLanguage::GLSL,
            .source_name = "mycelium://shader-source/ProbeTriangle.frag"sv,
            .source = StringView { fragment_shader_source },
        }));
        outln("  Runtime GLSL shaders compiled with {}", shader_compiler_backend_name(shader_compiler.backend()));
    } else {
        vertex_shader_bytecode = TRY(shader_compiler.load_spirv(shader_file_system, "mycelium://shaders/ProbeTriangle.vert.spv"sv));
        fragment_shader_bytecode = TRY(shader_compiler.load_spirv(shader_file_system, "mycelium://shaders/ProbeTriangle.frag.spv"sv));
        outln("  Runtime GLSL compiler unavailable; loaded precompiled SPIR-V shaders");
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
    vkCmdDraw(command_buffer, 3, 1, 0, 0);
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

    outln("  Vulkan triangle presented ({}x{})", extent.width, extent.height);
    return {};
}
#endif

ErrorOr<void> run_vulkan_probe(SDL_Window& window)
{
#if !defined(USE_VULKAN)
    (void)window;
    return Error::from_string_literal("Vulkan support was not enabled in this build");
#else
    outln("MyceliumVR Vulkan probe:");

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

    outln("  SDL Vulkan extension count: {}", extension_count);
    for (Uint32 i = 0; i < extension_count; ++i)
        outln("    {}", extensions[i]);

    VkApplicationInfo application_info {};
    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pApplicationName = "MyceliumVR Vulkan Probe";
    application_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    application_info.pEngineName = "MyceliumVR";
    application_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    application_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_create_info {};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pApplicationInfo = &application_info;
    instance_create_info.enabledExtensionCount = extension_count;
    instance_create_info.ppEnabledExtensionNames = extensions;

    VkInstance instance { VK_NULL_HANDLE };
    auto create_instance_result = vkCreateInstance(&instance_create_info, nullptr, &instance);
    if (create_instance_result != VK_SUCCESS) {
        warnln("vkCreateInstance failed with VkResult {}", to_underlying(create_instance_result));
        SDL_Vulkan_UnloadLibrary();
        return Error::from_string_literal("vkCreateInstance failed");
    }
    outln("  VkInstance created");

    VkSurfaceKHR surface { VK_NULL_HANDLE };
    if (!SDL_Vulkan_CreateSurface(&window, instance, nullptr, &surface)) {
        warnln("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        vkDestroyInstance(instance, nullptr);
        SDL_Vulkan_UnloadLibrary();
        return Error::from_string_literal("SDL_Vulkan_CreateSurface failed");
    }
    outln("  SDL Vulkan surface created");

    u32 graphics_queue_family = 0;
    auto physical_device = TRY(pick_physical_device(instance, surface, graphics_queue_family));
    VkPhysicalDeviceProperties device_properties {};
    vkGetPhysicalDeviceProperties(physical_device, &device_properties);
    outln("  Physical device: {}", device_properties.deviceName);
    outln("  Graphics/present queue family: {}", graphics_queue_family);

    auto device = TRY(create_logical_device(physical_device, graphics_queue_family));
    VkQueue graphics_queue { VK_NULL_HANDLE };
    vkGetDeviceQueue(device, graphics_queue_family, 0, &graphics_queue);
    outln("  VkDevice created");

    TRY(draw_one_swapchain_triangle(physical_device, device, surface, graphics_queue, graphics_queue_family));

    vkDestroyDevice(device, nullptr);
    SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_Vulkan_UnloadLibrary();

    outln("  Vulkan probe OK");
    return {};
#endif
}

}
