/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanSwapchain.h"
#include "VulkanDebug.h"
#include "VulkanResourceManager.h"
#include <AK/NumericLimits.h>
#include <cstdio>

namespace MyceliumVR {

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

ErrorOr<void> VulkanSwapchainManager::ensure_swapchain_resources(
    VkPhysicalDevice physical_device,
    VkDevice device,
    VkSurfaceKHR surface,
    VmaAllocator allocator,
    u32 width,
    u32 height,
    VulkanSwapchain& sc)
{
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
    auto present_mode   = choose_present_mode(present_modes);
    auto extent         = surface_capabilities.currentExtent;
    if (extent.width == NumericLimits<u32>::max()) {
        extent.width  = width;
        extent.height = height;
    }

    if (sc.swapchain != VK_NULL_HANDLE && sc.extent.width == extent.width && sc.extent.height == extent.height)
        return {};

    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);

    sc.destroy(device, allocator);

    sc.image_format = surface_format.format;
    sc.extent       = extent;

    auto image_count = surface_capabilities.minImageCount + 1;
    if (surface_capabilities.maxImageCount > 0 && image_count > surface_capabilities.maxImageCount)
        image_count = surface_capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR swapchain_create_info {};
    swapchain_create_info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface          = surface;
    swapchain_create_info.minImageCount    = image_count;
    swapchain_create_info.imageFormat      = surface_format.format;
    swapchain_create_info.imageColorSpace  = surface_format.colorSpace;
    swapchain_create_info.imageExtent      = extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.preTransform     = surface_capabilities.currentTransform;
    swapchain_create_info.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode      = present_mode;
    swapchain_create_info.clipped          = VK_TRUE;

    TRY(check_vulkan_result(vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &sc.swapchain), "vkCreateSwapchainKHR failed"sv));
    VulkanDebug::name(device, sc.swapchain, VK_OBJECT_TYPE_SWAPCHAIN_KHR, "MyceliumVR/Swapchain"sv);

    u32 sc_image_count = 0;
    vkGetSwapchainImagesKHR(device, sc.swapchain, &sc_image_count, nullptr);
    sc.images.resize(sc_image_count);
    vkGetSwapchainImagesKHR(device, sc.swapchain, &sc_image_count, sc.images.data());

    sc.image_views.resize(sc_image_count);
    for (u32 i = 0; i < sc_image_count; ++i) {
        VkImageViewCreateInfo iv_info {};
        iv_info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv_info.image            = sc.images[i];
        iv_info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        iv_info.format           = sc.image_format;
        iv_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        TRY(check_vulkan_result(vkCreateImageView(device, &iv_info, nullptr, &sc.image_views[i]), "vkCreateImageView failed"sv));

        char buf[64];
        snprintf(buf, sizeof(buf), "MyceliumVR/SwapchainImage[%u]", i);
        VulkanDebug::name(device, sc.images[i], VK_OBJECT_TYPE_IMAGE, StringView { buf, strlen(buf) });
        snprintf(buf, sizeof(buf), "MyceliumVR/SwapchainImageView[%u]", i);
        VulkanDebug::name(device, sc.image_views[i], VK_OBJECT_TYPE_IMAGE_VIEW, StringView { buf, strlen(buf) });
    }

    sc.depth_format = find_depth_format(physical_device);
    TRY(VulkanResourceManager::create_image(allocator, extent.width, extent.height, sc.depth_format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sc.depth_image, sc.depth_allocation));
    {
        VkImageViewCreateInfo div_info {};
        div_info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        div_info.image            = sc.depth_image;
        div_info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        div_info.format           = sc.depth_format;
        div_info.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        TRY(check_vulkan_result(vkCreateImageView(device, &div_info, nullptr, &sc.depth_view), "vkCreateImageView for depth failed"sv));
    }
    VulkanDebug::name(device, sc.depth_image, VK_OBJECT_TYPE_IMAGE,      "MyceliumVR/DepthImage"sv);
    VulkanDebug::name(device, sc.depth_view,  VK_OBJECT_TYPE_IMAGE_VIEW, "MyceliumVR/DepthImageView"sv);

    // Per-image render_finished semaphores
    {
        VkSemaphoreCreateInfo s_info { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
        sc.render_finished_semaphores.resize(sc_image_count);
        for (u32 i = 0; i < sc_image_count; ++i) {
            TRY(check_vulkan_result(vkCreateSemaphore(device, &s_info, nullptr, &sc.render_finished_semaphores[i]), "vkCreateSemaphore for render_finished failed"sv));
            char buf[64];
            snprintf(buf, sizeof(buf), "MyceliumVR/RenderFinishedSemaphore[%u]", i);
            VulkanDebug::name(device, sc.render_finished_semaphores[i], VK_OBJECT_TYPE_SEMAPHORE, StringView { buf, strlen(buf) });
        }
    }

    if (sc.image_available == VK_NULL_HANDLE) {
        VkSemaphoreCreateInfo s_info { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
        VkFenceCreateInfo     f_info { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,     nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
        TRY(check_vulkan_result(vkCreateSemaphore(device, &s_info, nullptr, &sc.image_available), "vkCreateSemaphore failed"sv));
        TRY(check_vulkan_result(vkCreateFence(device,    &f_info, nullptr, &sc.in_flight),       "vkCreateFence failed"sv));
        VulkanDebug::name(device, sc.image_available, VK_OBJECT_TYPE_SEMAPHORE, "MyceliumVR/ImageAvailableSemaphore"sv);
        VulkanDebug::name(device, sc.in_flight,       VK_OBJECT_TYPE_FENCE,     "MyceliumVR/InFlightFence"sv);
    }

    return {};
}

}
