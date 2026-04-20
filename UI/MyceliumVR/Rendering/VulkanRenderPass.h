/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "VulkanCommon.h"
#include "Backend/VulkanContext.h"
#include "RenderGraph.h"

namespace MyceliumVR {

class VulkanRenderPass : public RenderGraphPass {
public:
    virtual ~VulkanRenderPass() override = default;

    virtual ErrorOr<void> prepare(VulkanContext const& ctx, VkCommandPool command_pool) = 0;
    
    virtual void record_uploads(VkCommandBuffer) {}
    virtual void execute(VkCommandBuffer command_buffer, VkExtent2D extent, PushConstants const& pc) = 0;
    
    // From RenderGraphPass
    virtual String name() const override = 0;
    virtual void setup(RenderPassBuilder&) override = 0;
    virtual void execute(VkCommandBuffer cmd, RenderGraph& graph) override = 0;

    virtual void destroy(VkDevice device, VmaAllocator allocator) = 0;
};

}
