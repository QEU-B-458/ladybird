/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define VMA_IMPLEMENTATION
#include "VulkanContext.h"
#include <AK/Vector.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdlib>

#if defined(TRACY_ENABLE)
#    include <tracy/Tracy.hpp>

static void VKAPI_ATTR VKAPI_CALL vma_allocate_callback(VmaAllocator, uint32_t, VkDeviceMemory memory, VkDeviceSize size, void*)
{
    TracyAllocN((void*)memory, size, "VMA");
}

static void VKAPI_ATTR VKAPI_CALL vma_free_callback(VmaAllocator, uint32_t, VkDeviceMemory memory, VkDeviceSize, void*)
{
    TracyFreeN((void*)memory, "VMA");
}
#endif

namespace MyceliumVR {

#if defined(USE_VULKAN)

// Read MYCELIUMVR_VALIDATION env var to select the validation preset.
// In release builds (NDEBUG defined) always returns None so there is zero
// overhead in production.
static ValidationMode detect_validation_mode()
{
#ifdef NDEBUG
    return ValidationMode::None;
#else
    auto* val = getenv("MYCELIUMVR_VALIDATION");
    if (!val || val[0] == '\0')
        return ValidationMode::Normal;
    auto sv = StringView { val, strlen(val) };
    if (sv == "none"sv)         return ValidationMode::None;
    if (sv == "sync"sv)         return ValidationMode::Sync;
    if (sv == "gpu"sv)          return ValidationMode::GPUAssisted;
    return ValidationMode::Normal;
#endif
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

// Returns true only when both VK_KHR_external_memory_fd is present AND the driver
// reports that VK_FORMAT_B8G8R8A8_UNORM images can be exported/imported via opaque fd.
// Both must hold for the zero-copy overlay path to be safe to enable.
static bool check_external_memory_fd_support(VkPhysicalDevice physical_device)
{
    u32 ext_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &ext_count, nullptr);
    Vector<VkExtensionProperties> exts;
    exts.resize(ext_count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &ext_count, exts.data());

    bool has_fd_ext = false;
    for (auto const& ext : exts) {
        if (StringView { ext.extensionName, strlen(ext.extensionName) } == VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) {
            has_fd_ext = true;
            break;
        }
    }
    if (!has_fd_ext)
        return false;

    VkPhysicalDeviceExternalImageFormatInfo ext_fmt_info {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .pNext = nullptr,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
    };
    VkPhysicalDeviceImageFormatInfo2 fmt_info {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &ext_fmt_info,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .flags = 0,
    };
    VkExternalImageFormatProperties ext_props {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
        .pNext = nullptr,
        .externalMemoryProperties = {},
    };
    VkImageFormatProperties2 fmt_props {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext = &ext_props,
        .imageFormatProperties = {},
    };

    if (vkGetPhysicalDeviceImageFormatProperties2(physical_device, &fmt_info, &fmt_props) != VK_SUCCESS)
        return false;

    auto features = ext_props.externalMemoryProperties.externalMemoryFeatures;
    return (features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0
        && (features & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0;
}

static ErrorOr<VkDevice> create_logical_device(VkPhysicalDevice physical_device, u32 graphics_queue_family, bool& supports_multi_draw_indirect, bool enable_external_memory_fd)
{
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };

    VkPhysicalDeviceSynchronization2Features synchronization2_features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        .pNext = nullptr,
        .synchronization2 = VK_TRUE,
    };

    // Enable dynamic rendering feature (Vulkan 1.3 or 1.2 with extension)
    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &synchronization2_features,
        .dynamicRendering = VK_TRUE
    };

    // Enable descriptor indexing features (Vulkan 1.2 core)
    VkPhysicalDeviceDescriptorIndexingFeatures indexing_features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .pNext = &dynamic_rendering_features,
        .shaderInputAttachmentArrayDynamicIndexing = VK_FALSE,
        .shaderUniformTexelBufferArrayDynamicIndexing = VK_FALSE,
        .shaderStorageTexelBufferArrayDynamicIndexing = VK_FALSE,
        .shaderUniformBufferArrayNonUniformIndexing = VK_FALSE,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .shaderStorageBufferArrayNonUniformIndexing = VK_FALSE,
        .shaderStorageImageArrayNonUniformIndexing = VK_FALSE,
        .shaderInputAttachmentArrayNonUniformIndexing = VK_FALSE,
        .shaderUniformTexelBufferArrayNonUniformIndexing = VK_FALSE,
        .shaderStorageTexelBufferArrayNonUniformIndexing = VK_FALSE,
        .descriptorBindingUniformBufferUpdateAfterBind = VK_FALSE,
        .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
        .descriptorBindingStorageImageUpdateAfterBind = VK_FALSE,
        .descriptorBindingStorageBufferUpdateAfterBind = VK_FALSE,
        .descriptorBindingUniformTexelBufferUpdateAfterBind = VK_FALSE,
        .descriptorBindingStorageTexelBufferUpdateAfterBind = VK_FALSE,
        .descriptorBindingUpdateUnusedWhilePending = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .descriptorBindingVariableDescriptorCount = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
    };

    VkPhysicalDeviceFeatures supported_features {};
    vkGetPhysicalDeviceFeatures(physical_device, &supported_features);

    VkPhysicalDeviceFeatures enabled_features {};
    if (supported_features.multiDrawIndirect == VK_TRUE) {
        enabled_features.multiDrawIndirect = VK_TRUE;
        supports_multi_draw_indirect = true;
    } else {
        supports_multi_draw_indirect = false;
    }

    Vector<char const*> device_extensions;
    device_extensions.append(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (enable_external_memory_fd)
        device_extensions.append(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);

    VkDeviceCreateInfo device_create_info {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &indexing_features,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<u32>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures = &enabled_features
    };

    VkDevice device { VK_NULL_HANDLE };
    TRY(check_vulkan_result(vkCreateDevice(physical_device, &device_create_info, nullptr, &device), "vkCreateDevice failed"sv));
    return device;
}

ErrorOr<NonnullOwnPtr<VulkanContext>> VulkanContext::create(SDL_Window& window)
{
    auto context = TRY(adopt_nonnull_own_or_enomem(new (nothrow) VulkanContext));
    TRY(context->initialize(window));
    return context;
}

ErrorOr<void> VulkanContext::initialize(SDL_Window& window)
{
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        warnln("SDL_Vulkan_LoadLibrary failed: {}", SDL_GetError());
        return Error::from_string_literal("SDL_Vulkan_LoadLibrary failed");
    }

    Uint32 sdl_extension_count = 0;
    auto sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
    if (!sdl_extensions) {
        warnln("SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError());
        SDL_Vulkan_UnloadLibrary();
        return Error::from_string_literal("SDL_Vulkan_GetInstanceExtensions failed");
    }

    // Collect layers and extensions; let the debug helper append its own.
    Vector<char const*> layers;
    Vector<char const*> extensions;
    for (Uint32 i = 0; i < sdl_extension_count; ++i)
        extensions.append(sdl_extensions[i]);

    auto validation_mode = detect_validation_mode();
    VulkanDebug::prepare_instance_create(validation_mode, layers, extensions);

    // Build optional validation-features chain for sync / GPU-assisted modes.
    Vector<VkValidationFeatureEnableEXT> feature_enables;
    VkValidationFeaturesEXT validation_features {};
    VulkanDebug::fill_validation_features(validation_mode, feature_enables, validation_features);

    VkApplicationInfo application_info {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "MyceliumVR",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "MyceliumVR",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3
    };

    VkInstanceCreateInfo instance_create_info {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = feature_enables.is_empty() ? nullptr : &validation_features,
        .flags = 0,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = static_cast<u32>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<u32>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    TRY(check_vulkan_result(vkCreateInstance(&instance_create_info, nullptr, &m_instance), "vkCreateInstance failed"sv));

    // Messenger is debug-only (validation layer must be present for it to fire).
    // init_device_procs() below loads naming/label pfns unconditionally so they
    // work in Release builds when running under RenderDoc.
#ifndef NDEBUG
    m_debug_messenger = VulkanDebug::create_debug_messenger(m_instance);
#endif

    if (!SDL_Vulkan_CreateSurface(&window, m_instance, nullptr, &m_surface)) {
        warnln("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return Error::from_string_literal("SDL_Vulkan_CreateSurface failed");
    }

    m_physical_device = TRY(pick_physical_device(m_instance, m_surface, m_graphics_queue_family));
    m_supports_external_image_import = check_external_memory_fd_support(m_physical_device);
    if (m_supports_external_image_import)
        outln("VulkanContext: VK_KHR_external_memory_fd supported — zero-copy overlay path available.");
    else
        outln("VulkanContext: VK_KHR_external_memory_fd not available — overlay will use CPU bitmap path.");
    m_device = TRY(create_logical_device(m_physical_device, m_graphics_queue_family, m_supports_multi_draw_indirect, m_supports_external_image_import));
    vkGetDeviceQueue(m_device, m_graphics_queue_family, 0, &m_graphics_queue);

    VkPhysicalDeviceProperties device_props {};
    vkGetPhysicalDeviceProperties(m_physical_device, &device_props);
    m_timestamp_period = device_props.limits.timestampPeriod;

    // Load device-level debug proc addresses (naming + labels).
    VulkanDebug::init_device_procs(m_instance);

    // Name core Vulkan objects so they appear correctly in RenderDoc / validation output.
    VulkanDebug::set_object_name(m_device, VK_OBJECT_TYPE_INSTANCE,    (uint64_t)m_instance,       "MyceliumVR/Instance"sv);
    VulkanDebug::set_object_name(m_device, VK_OBJECT_TYPE_DEVICE,      (uint64_t)m_device,         "MyceliumVR/Device"sv);
    VulkanDebug::set_object_name(m_device, VK_OBJECT_TYPE_SURFACE_KHR, (uint64_t)m_surface,        "MyceliumVR/Surface"sv);
    VulkanDebug::set_object_name(m_device, VK_OBJECT_TYPE_QUEUE,       (uint64_t)m_graphics_queue, "MyceliumVR/GraphicsQueue"sv);

    VmaDeviceMemoryCallbacks vma_callbacks {};
#if defined(TRACY_ENABLE)
    vma_callbacks.pfnAllocate = vma_allocate_callback;
    vma_callbacks.pfnFree = vma_free_callback;
#endif

    VmaAllocatorCreateInfo allocator_info {
        .flags = 0,
        .physicalDevice = m_physical_device,
        .device = m_device,
        .preferredLargeHeapBlockSize = 0,
        .pAllocationCallbacks = nullptr,
        .pDeviceMemoryCallbacks = &vma_callbacks,
        .pHeapSizeLimit = nullptr,
        .pVulkanFunctions = nullptr,
        .instance = m_instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
        .pTypeExternalMemoryHandleTypes = nullptr
    };

    TRY(check_vulkan_result(vmaCreateAllocator(&allocator_info, &m_allocator), "vmaCreateAllocator failed"sv));

    return {};
}

VulkanContext::~VulkanContext()
{
    if (m_allocator != VK_NULL_HANDLE)
        vmaDestroyAllocator(m_allocator);
    if (m_device != VK_NULL_HANDLE)
        vkDestroyDevice(m_device, nullptr);
    if (m_surface != VK_NULL_HANDLE)
        SDL_Vulkan_DestroySurface(m_instance, m_surface, nullptr);
    // Messenger must be destroyed before the instance.
    VulkanDebug::destroy_debug_messenger(m_instance, m_debug_messenger);
    if (m_instance != VK_NULL_HANDLE)
        vkDestroyInstance(m_instance, nullptr);
    SDL_Vulkan_UnloadLibrary();
}

#endif

}
