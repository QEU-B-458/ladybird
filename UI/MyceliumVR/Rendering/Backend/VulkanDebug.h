/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../VulkanCommon.h"
#include <AK/StringView.h>
#include <AK/Vector.h>

namespace MyceliumVR {

#if defined(USE_VULKAN)

// Validation preset chosen at startup (env var MYCELIUMVR_VALIDATION or build default).
enum class ValidationMode {
    None,       // No validation layers — release default
    Normal,     // Standard validation + best-practices — debug default
    Sync,       // Normal + synchronization validation
    GPUAssisted // GPU-assisted validation + sync (no best-practices; they conflict)
};

namespace VulkanDebug {

// ── Instance-level setup (call before vkCreateInstance) ───────────────────────

// Append VK_LAYER_KHRONOS_validation to `layers` and VK_EXT_debug_utils to
// `extensions` when available and when mode != None.  No-op in release builds.
void prepare_instance_create(ValidationMode mode,
    Vector<char const*>& layers,
    Vector<char const*>& instance_extensions);

// Fill a VkValidationFeaturesEXT struct from `mode`.  `enables` must outlive
// the vkCreateInstance call; chain `out.pNext` into VkInstanceCreateInfo.pNext
// only when enables is non-empty.
void fill_validation_features(ValidationMode mode,
    Vector<VkValidationFeatureEnableEXT>& enables,
    VkValidationFeaturesEXT& out);

// ── Messenger (call after vkCreateInstance) ───────────────────────────────────

VkDebugUtilsMessengerEXT create_debug_messenger(VkInstance instance);
void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger);

// ── Device-level proc loading (call after vkCreateDevice) ────────────────────

// Load vkSetDebugUtilsObjectNameEXT and label functions via the instance.
void init_device_procs(VkInstance instance);

// ── Object naming (silently no-ops when extension unavailable) ────────────────

void set_object_name(VkDevice device, VkObjectType object_type, uint64_t handle, StringView name);

template<typename T>
inline void name(VkDevice device, T handle, VkObjectType type, StringView label)
{
    set_object_name(device, type, reinterpret_cast<uint64_t>(handle), label);
}

// ── Command buffer labels ─────────────────────────────────────────────────────

void cmd_begin_label(VkCommandBuffer cmd, StringView label, float r = 0.5f, float g = 0.5f, float b = 1.0f);
void cmd_end_label(VkCommandBuffer cmd);
void cmd_insert_label(VkCommandBuffer cmd, StringView label, float r = 1.0f, float g = 1.0f, float b = 0.0f);

} // namespace VulkanDebug

// RAII scope: begins a command buffer label on construction, ends it on destruction.
struct DebugLabelScope {
    DebugLabelScope(VkCommandBuffer cmd, StringView label,
        float r = 0.5f, float g = 0.5f, float b = 1.0f)
        : m_cmd(cmd)
    {
        VulkanDebug::cmd_begin_label(cmd, label, r, g, b);
    }
    ~DebugLabelScope() { VulkanDebug::cmd_end_label(m_cmd); }
    DebugLabelScope(DebugLabelScope const&) = delete;
    DebugLabelScope& operator=(DebugLabelScope const&) = delete;

private:
    VkCommandBuffer m_cmd;
};

#endif // USE_VULKAN

} // namespace MyceliumVR
