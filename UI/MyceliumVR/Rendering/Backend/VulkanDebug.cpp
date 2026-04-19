/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "VulkanDebug.h"
#include <AK/Format.h>
#include <cstring>

namespace MyceliumVR {

#if defined(USE_VULKAN)

// ── Static function pointers ──────────────────────────────────────────────────

static PFN_vkCreateDebugUtilsMessengerEXT  s_vkCreateDebugUtilsMessengerEXT  { nullptr };
static PFN_vkDestroyDebugUtilsMessengerEXT s_vkDestroyDebugUtilsMessengerEXT { nullptr };
static PFN_vkSetDebugUtilsObjectNameEXT    s_vkSetDebugUtilsObjectNameEXT    { nullptr };
static PFN_vkCmdBeginDebugUtilsLabelEXT    s_vkCmdBeginDebugUtilsLabelEXT    { nullptr };
static PFN_vkCmdEndDebugUtilsLabelEXT      s_vkCmdEndDebugUtilsLabelEXT      { nullptr };
static PFN_vkCmdInsertDebugUtilsLabelEXT   s_vkCmdInsertDebugUtilsLabelEXT   { nullptr };

// ── Debug messenger callback ──────────────────────────────────────────────────

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    VkDebugUtilsMessengerCallbackDataEXT const* data,
    void*)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        warnln("[Vulkan ERROR] {}", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        warnln("[Vulkan WARN]  {}", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        dbgln("[Vulkan INFO]  {}", data->pMessage);
    // Verbose severity intentionally suppressed.
    return VK_FALSE;
}

// ── Availability helpers ──────────────────────────────────────────────────────

static bool is_instance_extension_available(char const* ext_name)
{
    u32 count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    Vector<VkExtensionProperties> props;
    props.resize(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());
    for (auto const& p : props) {
        if (StringView { p.extensionName, strlen(p.extensionName) } == StringView { ext_name, strlen(ext_name) })
            return true;
    }
    return false;
}

#ifndef NDEBUG
static bool is_validation_layer_available()
{
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    Vector<VkLayerProperties> props;
    props.resize(count);
    vkEnumerateInstanceLayerProperties(&count, props.data());
    for (auto const& p : props) {
        if (StringView { p.layerName, strlen(p.layerName) } == "VK_LAYER_KHRONOS_validation"sv)
            return true;
    }
    return false;
}
#endif

// ── Instance-level setup ──────────────────────────────────────────────────────

void VulkanDebug::prepare_instance_create(ValidationMode mode,
    Vector<char const*>& layers,
    Vector<char const*>& instance_extensions)
{
    // Always request VK_EXT_debug_utils so object naming and command buffer
    // labels work in Release builds when running under RenderDoc or any other
    // tool that provides the extension.
    if (is_instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        instance_extensions.append(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

#ifndef NDEBUG
    // Validation layer is debug-only — it has measurable CPU overhead.
    if (mode == ValidationMode::None)
        return;
    if (!is_validation_layer_available()) {
        warnln("[VulkanDebug] VK_LAYER_KHRONOS_validation not available — skipping validation");
        return;
    }
    layers.append("VK_LAYER_KHRONOS_validation");
#else
    (void)mode;
    (void)layers;
#endif
}

void VulkanDebug::fill_validation_features(ValidationMode mode,
    Vector<VkValidationFeatureEnableEXT>& enables,
    VkValidationFeaturesEXT& out)
{
    out = {};
    out.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;

#ifndef NDEBUG
    switch (mode) {
    case ValidationMode::Normal:
        enables.append(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
        break;
    case ValidationMode::Sync:
        enables.append(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
        enables.append(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
        break;
    case ValidationMode::GPUAssisted:
        // GPU-assisted and best-practices conflict — omit best-practices here.
        enables.append(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
        enables.append(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
        break;
    case ValidationMode::None:
        break;
    }
#else
    (void)mode;
#endif

    out.enabledValidationFeatureCount = static_cast<u32>(enables.size());
    out.pEnabledValidationFeatures = enables.data();
}

// ── Messenger ─────────────────────────────────────────────────────────────────

VkDebugUtilsMessengerEXT VulkanDebug::create_debug_messenger(VkInstance instance)
{
    s_vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    s_vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

    if (!s_vkCreateDebugUtilsMessengerEXT)
        return VK_NULL_HANDLE;

    VkDebugUtilsMessengerCreateInfoEXT ci {};
    ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    ci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debug_messenger_callback;

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if (s_vkCreateDebugUtilsMessengerEXT(instance, &ci, nullptr, &messenger) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return messenger;
}

void VulkanDebug::destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    if (messenger != VK_NULL_HANDLE && s_vkDestroyDebugUtilsMessengerEXT)
        s_vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
}

// ── Device-level proc loading ─────────────────────────────────────────────────

void VulkanDebug::init_device_procs(VkInstance instance)
{
    s_vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
    s_vkCmdBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
    s_vkCmdEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));
    s_vkCmdInsertDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(instance, "vkCmdInsertDebugUtilsLabelEXT"));
}

// ── Object naming ─────────────────────────────────────────────────────────────

void VulkanDebug::set_object_name(VkDevice device, VkObjectType object_type, uint64_t handle, StringView sv_name)
{
    if (!s_vkSetDebugUtilsObjectNameEXT || device == VK_NULL_HANDLE || handle == 0)
        return;
    char buf[256];
    auto len = sv_name.length() < sizeof(buf) - 1 ? sv_name.length() : sizeof(buf) - 1;
    memcpy(buf, sv_name.characters_without_null_termination(), len);
    buf[len] = '\0';

    VkDebugUtilsObjectNameInfoEXT info {};
    info.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType   = object_type;
    info.objectHandle = handle;
    info.pObjectName  = buf;
    s_vkSetDebugUtilsObjectNameEXT(device, &info);
}

// ── Command buffer labels ─────────────────────────────────────────────────────

static void fill_label(VkDebugUtilsLabelEXT& info, StringView sv_label, float r, float g, float b, char* buf, size_t buf_size)
{
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    auto len = sv_label.length() < buf_size - 1 ? sv_label.length() : buf_size - 1;
    memcpy(buf, sv_label.characters_without_null_termination(), len);
    buf[len]      = '\0';
    info.pLabelName = buf;
    info.color[0] = r; info.color[1] = g; info.color[2] = b; info.color[3] = 1.0f;
}

void VulkanDebug::cmd_begin_label(VkCommandBuffer cmd, StringView label, float r, float g, float b)
{
    if (!s_vkCmdBeginDebugUtilsLabelEXT || cmd == VK_NULL_HANDLE)
        return;
    char buf[256];
    VkDebugUtilsLabelEXT info {};
    fill_label(info, label, r, g, b, buf, sizeof(buf));
    s_vkCmdBeginDebugUtilsLabelEXT(cmd, &info);
}

void VulkanDebug::cmd_end_label(VkCommandBuffer cmd)
{
    if (!s_vkCmdEndDebugUtilsLabelEXT || cmd == VK_NULL_HANDLE)
        return;
    s_vkCmdEndDebugUtilsLabelEXT(cmd);
}

void VulkanDebug::cmd_insert_label(VkCommandBuffer cmd, StringView label, float r, float g, float b)
{
    if (!s_vkCmdInsertDebugUtilsLabelEXT || cmd == VK_NULL_HANDLE)
        return;
    char buf[256];
    VkDebugUtilsLabelEXT info {};
    fill_label(info, label, r, g, b, buf, sizeof(buf));
    s_vkCmdInsertDebugUtilsLabelEXT(cmd, &info);
}

#endif // USE_VULKAN

} // namespace MyceliumVR
