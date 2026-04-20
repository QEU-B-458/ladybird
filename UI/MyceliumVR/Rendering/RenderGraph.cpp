/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "RenderGraph.h"
#if defined(TRACY_ENABLE)
#    include <tracy/Tracy.hpp>
#endif
#include <AK/StringBuilder.h>
#include <AK/NumericLimits.h>
#include <AK/QuickSort.h>

namespace MyceliumVR {

static TextureHandle as_texture_handle(ResourceHandle handle)
{
    return TextureHandle { handle };
}

static BufferHandle as_buffer_handle(ResourceHandle handle)
{
    return BufferHandle { handle };
}

static bool is_depth_format(VkFormat format)
{
    return format == VK_FORMAT_D16_UNORM
        || format == VK_FORMAT_D32_SFLOAT
        || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

static VkImageAspectFlags image_aspect_for_format(VkFormat format)
{
    if (format == VK_FORMAT_D24_UNORM_S8_UINT)
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if (is_depth_format(format))
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

static VkImageLayout image_layout_for_state(ResourceState state)
{
    switch (state) {
    case ResourceState::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case ResourceState::TransferDst:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ResourceState::TransferSrc:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::ColorAttachment:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthStencilAttachment:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::ShaderRead:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::Present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:
        return VK_IMAGE_LAYOUT_GENERAL;
    }
}

static VkPipelineStageFlags2 stage_mask_for_state(ResourceState state)
{
    switch (state) {
    case ResourceState::Undefined:
        return VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    case ResourceState::TransferDst:
    case ResourceState::TransferSrc:
        return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    case ResourceState::ColorAttachment:
        return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    case ResourceState::DepthStencilAttachment:
        return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    case ResourceState::ShaderRead:
        return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    case ResourceState::VertexBuffer:
        return VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    case ResourceState::IndirectBuffer:
        return VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    case ResourceState::StorageRead:
    case ResourceState::StorageWrite:
        return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    case ResourceState::Present:
        return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    }
    return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
}

static VkPipelineStageFlags2 stage_mask_for_domain(PipelineDomain domain)
{
    switch (domain) {
    case PipelineDomain::TopOfPipe:
        return VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    case PipelineDomain::DrawIndirect:
        return VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    case PipelineDomain::VertexInput:
        return VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    case PipelineDomain::VertexShader:
        return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    case PipelineDomain::FragmentShader:
        return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    case PipelineDomain::EarlyDepth:
        return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    case PipelineDomain::LateDepth:
        return VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    case PipelineDomain::ColorOutput:
        return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    case PipelineDomain::ComputeShader:
        return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    case PipelineDomain::Transfer:
        return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    case PipelineDomain::BottomOfPipe:
        return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    case PipelineDomain::Host:
        return VK_PIPELINE_STAGE_2_HOST_BIT;
    case PipelineDomain::Present:
        return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    }
    return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
}

static VkAccessFlags2 access_mask_for_state(ResourceState state)
{
    switch (state) {
    case ResourceState::Undefined:
    case ResourceState::Present:
        return 0;
    case ResourceState::TransferDst:
        return VK_ACCESS_2_TRANSFER_WRITE_BIT;
    case ResourceState::TransferSrc:
        return VK_ACCESS_2_TRANSFER_READ_BIT;
    case ResourceState::ColorAttachment:
        return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    case ResourceState::DepthStencilAttachment:
        return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case ResourceState::ShaderRead:
        return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    case ResourceState::VertexBuffer:
        return VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    case ResourceState::IndirectBuffer:
        return VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    case ResourceState::StorageRead:
        return VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    case ResourceState::StorageWrite:
        return VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    }
    return 0;
}

static StringView queue_kind_name(QueueKind queue)
{
    switch (queue) {
    case QueueKind::Graphics:
        return "Graphics"sv;
    case QueueKind::Compute:
        return "Compute"sv;
    case QueueKind::Transfer:
        return "Transfer"sv;
    }
    return "Unknown"sv;
}

static StringView resource_kind_name(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::Texture:
        return "Texture"sv;
    case ResourceKind::Buffer:
        return "Buffer"sv;
    }
    return "Unknown"sv;
}

static StringView resource_state_name(ResourceState state)
{
    switch (state) {
    case ResourceState::Undefined:
        return "Undefined"sv;
    case ResourceState::TransferDst:
        return "TransferDst"sv;
    case ResourceState::TransferSrc:
        return "TransferSrc"sv;
    case ResourceState::ColorAttachment:
        return "ColorAttachment"sv;
    case ResourceState::DepthStencilAttachment:
        return "DepthStencilAttachment"sv;
    case ResourceState::ShaderRead:
        return "ShaderRead"sv;
    case ResourceState::VertexBuffer:
        return "VertexBuffer"sv;
    case ResourceState::IndirectBuffer:
        return "IndirectBuffer"sv;
    case ResourceState::StorageRead:
        return "StorageRead"sv;
    case ResourceState::StorageWrite:
        return "StorageWrite"sv;
    case ResourceState::Present:
        return "Present"sv;
    }
    return "Unknown"sv;
}

RenderGraph::AbstractResourceState RenderGraph::abstract_state_for_initial_state(ResourceNode const& node) const
{
    RenderGraph::AbstractResourceState state {};
    state.queue = QueueKind::Graphics;
    state.stages = stage_mask_for_state(node.initial_state);
    state.access = access_mask_for_state(node.initial_state);
    state.layout = node.type == ResourceKind::Texture ? image_layout_for_state(node.initial_state) : VK_IMAGE_LAYOUT_UNDEFINED;
    state.writable = node.initial_state == ResourceState::StorageWrite
        || node.initial_state == ResourceState::ColorAttachment
        || node.initial_state == ResourceState::DepthStencilAttachment
        || node.initial_state == ResourceState::TransferDst;
    state.discardable = node.initial_state == ResourceState::Undefined;
    return state;
}

RenderGraph::AbstractResourceState RenderGraph::abstract_state_for_usage(ResourceUsage const& usage) const
{
    auto const& node = resource(usage.handle);
    RenderGraph::AbstractResourceState state {};
    state.queue = usage.queue;
    state.stages = stage_mask_for_domain(usage.stage_hint);
    state.layout = node.type == ResourceKind::Texture ? image_layout_for_state(usage.state) : VK_IMAGE_LAYOUT_UNDEFINED;
    state.writable = usage.access == AccessKind::Write || usage.access == AccessKind::ReadWrite;
    state.discardable = state.writable && !usage.preserve_contents;

    if (node.type == ResourceKind::Texture) {
        if (usage.is_attachment && usage.is_depth)
            state.access = state.writable ? (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        else if (usage.is_attachment)
            state.access = state.writable ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        else if (usage.is_storage)
            state.access = state.writable ? (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) : VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        else if (usage.state == ResourceState::ShaderRead)
            state.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        else
            state.access = access_mask_for_state(usage.state);
    } else {
        switch (usage.stage_hint) {
        case PipelineDomain::VertexInput:
            state.access = usage.access == AccessKind::Read ? VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT : access_mask_for_state(usage.state);
            break;
        case PipelineDomain::DrawIndirect:
            state.access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            break;
        case PipelineDomain::Transfer:
            state.access = state.writable ? VK_ACCESS_2_TRANSFER_WRITE_BIT : VK_ACCESS_2_TRANSFER_READ_BIT;
            break;
        case PipelineDomain::ComputeShader:
            state.access = state.writable ? (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) : VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            break;
        default:
            state.access = access_mask_for_state(usage.state);
            break;
        }
    }

    return state;
}

Optional<VkImageMemoryBarrier2> RenderGraph::lower_image_barrier(CompiledBarrier const& barrier) const
{
    auto const& node = resource(barrier.to_version);
    if (node.type != ResourceKind::Texture)
        return {};

    return VkImageMemoryBarrier2 {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = barrier.from_state.stages,
        .srcAccessMask = barrier.from_state.access,
        .dstStageMask = barrier.to_state.stages,
        .dstAccessMask = barrier.to_state.access,
        .oldLayout = barrier.from_state.layout,
        .newLayout = barrier.to_state.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = node.image,
        .subresourceRange = { image_aspect_for_format(node.format), 0, 1, 0, 1 },
    };
}

Optional<VkBufferMemoryBarrier2> RenderGraph::lower_buffer_barrier(CompiledBarrier const& barrier) const
{
    auto const& node = resource(barrier.to_version);
    if (node.type != ResourceKind::Buffer)
        return {};

    return VkBufferMemoryBarrier2 {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = barrier.from_state.stages,
        .srcAccessMask = barrier.from_state.access,
        .dstStageMask = barrier.to_state.stages,
        .dstAccessMask = barrier.to_state.access,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = node.buffer,
        .offset = 0,
        .size = node.buffer_size,
    };
}

VkRenderingAttachmentInfo RenderGraph::lower_color_attachment(AttachmentInfo const& attachment) const
{
    auto const& node = resource(attachment.handle);
    return VkRenderingAttachmentInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = node.image_view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = attachment.load_op,
        .storeOp = attachment.store_op,
        .clearValue = attachment.clear_value,
    };
}

VkRenderingAttachmentInfo RenderGraph::lower_depth_attachment(AttachmentInfo const& attachment) const
{
    auto const& node = resource(attachment.handle);
    return VkRenderingAttachmentInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = node.image_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = attachment.load_op,
        .storeOp = attachment.store_op,
        .clearValue = attachment.clear_value,
    };
}

TextureHandle RenderPassBuilder::create_texture(TextureDesc const& desc)
{
    return m_graph.create_texture(desc);
}

BufferHandle RenderPassBuilder::create_buffer(BufferDesc const& desc)
{
    return m_graph.create_buffer(desc);
}

TextureHandle RenderPassBuilder::import_texture(ImportedTextureDesc const& desc)
{
    return m_graph.import_texture(desc);
}

BufferHandle RenderPassBuilder::import_buffer(ImportedBufferDesc const& desc)
{
    return m_graph.import_buffer(desc);
}

TextureHandle RenderPassBuilder::read_sampled(TextureHandle handle)
{
    return as_texture_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::ShaderRead, AccessKind::Read, PipelineDomain::FragmentShader));
}

TextureHandle RenderPassBuilder::read_transfer(TextureHandle handle)
{
    return as_texture_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::TransferSrc, AccessKind::Read, PipelineDomain::Transfer));
}

TextureHandle RenderPassBuilder::write_transfer(TextureHandle handle)
{
    return as_texture_handle(m_graph.register_write(m_pass_index, handle.value, ResourceState::TransferDst, AccessKind::Write, PipelineDomain::Transfer));
}

TextureHandle RenderPassBuilder::read_storage(TextureHandle handle)
{
    return as_texture_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::StorageRead, AccessKind::Read, PipelineDomain::ComputeShader, false, false, true));
}

TextureHandle RenderPassBuilder::write_storage(TextureHandle handle)
{
    return as_texture_handle(m_graph.register_write(m_pass_index, handle.value, ResourceState::StorageWrite, AccessKind::Write, PipelineDomain::ComputeShader, false, false, true));
}

TextureHandle RenderPassBuilder::read_color(TextureHandle handle)
{
    return as_texture_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::ColorAttachment, AccessKind::Read, PipelineDomain::ColorOutput, true, false, false, true));
}

TextureHandle RenderPassBuilder::write_color(TextureHandle handle, LoadStoreOps const& ops)
{
    if (ops.load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
        m_graph.register_read(m_pass_index, handle.value, ResourceState::ColorAttachment, AccessKind::Read, PipelineDomain::ColorOutput, true, false, false, true);
    auto written = m_graph.register_write(m_pass_index, handle.value, ResourceState::ColorAttachment, AccessKind::Write, PipelineDomain::ColorOutput, true, false, false, ops.load_op == VK_ATTACHMENT_LOAD_OP_LOAD);
    m_graph.register_color_attachment(m_pass_index, written, ops.load_op, ops.store_op, ops.clear_value);
    return as_texture_handle(written);
}

TextureHandle RenderPassBuilder::read_depth(TextureHandle handle)
{
    return as_texture_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::DepthStencilAttachment, AccessKind::Read, PipelineDomain::EarlyDepth, true, true, false, true));
}

TextureHandle RenderPassBuilder::write_depth(TextureHandle handle, DepthLoadStoreOps const& ops)
{
    if (ops.load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
        m_graph.register_read(m_pass_index, handle.value, ResourceState::DepthStencilAttachment, AccessKind::Read, PipelineDomain::EarlyDepth, true, true, false, true);
    auto written = m_graph.register_write(m_pass_index, handle.value, ResourceState::DepthStencilAttachment, AccessKind::Write, PipelineDomain::LateDepth, true, true, false, ops.load_op == VK_ATTACHMENT_LOAD_OP_LOAD);
    m_graph.register_depth_attachment(m_pass_index, written, ops.load_op, ops.store_op, ops.clear_value);
    return as_texture_handle(written);
}

BufferHandle RenderPassBuilder::read_vertex(BufferHandle handle)
{
    return as_buffer_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::VertexBuffer, AccessKind::Read, PipelineDomain::VertexInput));
}

BufferHandle RenderPassBuilder::read_index(BufferHandle handle)
{
    return as_buffer_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::VertexBuffer, AccessKind::Read, PipelineDomain::VertexInput));
}

BufferHandle RenderPassBuilder::read_uniform(BufferHandle handle)
{
    return as_buffer_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::ShaderRead, AccessKind::Read, PipelineDomain::FragmentShader));
}

BufferHandle RenderPassBuilder::read_storage(BufferHandle handle)
{
    return as_buffer_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::StorageRead, AccessKind::Read, PipelineDomain::ComputeShader, false, false, true));
}

BufferHandle RenderPassBuilder::write_storage(BufferHandle handle)
{
    return as_buffer_handle(m_graph.register_write(m_pass_index, handle.value, ResourceState::StorageWrite, AccessKind::Write, PipelineDomain::ComputeShader, false, false, true));
}

BufferHandle RenderPassBuilder::read_indirect(BufferHandle handle)
{
    return as_buffer_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::IndirectBuffer, AccessKind::Read, PipelineDomain::DrawIndirect));
}

BufferHandle RenderPassBuilder::read_transfer(BufferHandle handle)
{
    return as_buffer_handle(m_graph.register_read(m_pass_index, handle.value, ResourceState::TransferSrc, AccessKind::Read, PipelineDomain::Transfer));
}

BufferHandle RenderPassBuilder::write_transfer(BufferHandle handle)
{
    return as_buffer_handle(m_graph.register_write(m_pass_index, handle.value, ResourceState::TransferDst, AccessKind::Write, PipelineDomain::Transfer));
}

void RenderPassBuilder::set_execute(Function<void(VkCommandBuffer, RenderGraph&)> callback)
{
    m_graph.m_pass_nodes[m_pass_index].execute_callback = move(callback);
}

void RenderPassBuilder::set_never_cull(bool value)
{
    m_graph.m_pass_nodes[m_pass_index].never_cull = value;
}

void RenderPassBuilder::set_side_effect(bool value)
{
    m_graph.m_pass_nodes[m_pass_index].side_effect = value;
}

void RenderPassBuilder::set_queue_preference(QueueKind queue)
{
    m_graph.m_pass_nodes[m_pass_index].queue = queue;
}

RenderGraph::RenderGraph(VulkanContext& ctx)
    : m_context(ctx)
{
}

RenderGraph::~RenderGraph()
{
    for (auto& resource : m_resources)
        destroy_transient_resource(resource);
}

void RenderGraph::destroy_transient_resource(ResourceNode& node)
{
    if (node.external)
        return;
    if (node.type == ResourceKind::Texture) {
        if (node.image_view != VK_NULL_HANDLE)
            vkDestroyImageView(m_context.device(), node.image_view, nullptr);
        if (node.owns_image_memory && node.image != VK_NULL_HANDLE)
            vmaDestroyImage(m_context.allocator(), node.image, node.image_allocation);
        node.image_view = VK_NULL_HANDLE;
        node.image = VK_NULL_HANDLE;
        node.image_allocation = VK_NULL_HANDLE;
        node.owns_image_memory = false;
    } else {
        if (node.owns_buffer_memory && node.buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(m_context.allocator(), node.buffer, node.buffer_allocation);
        node.buffer = VK_NULL_HANDLE;
        node.buffer_allocation = VK_NULL_HANDLE;
        node.owns_buffer_memory = false;
    }
    node.physical_allocation_index.clear();
}

void RenderGraph::reset_for_frame()
{
    ZoneScoped;
    VERIFY(m_phase != Phase::Executing);
    m_passes.clear();
    m_pass_nodes.clear();
    m_outputs.clear();
    m_execution_order.clear();
    m_compiled_graph.ordered_passes.clear();
    m_compiled_graph.resource_lifetimes.clear();
    m_compiled_graph.version_lifetimes.clear();
    m_compiled_graph.versions.clear();
    m_compiled_graph.allocations.clear();
    m_compiled_graph.submission_plan.batches.clear();
    m_compiled_graph.submission_plan.queue_transitions.clear();
    m_compiled_graph.submission_plan.requires_multi_queue_submission = false;
    m_render_extent = {};
    m_compiled = false;
    m_frame_index++;
#if defined(TRACY_ENABLE)
    TracyPlot("RenderGraph/FrameIndex", static_cast<int64_t>(m_frame_index));
    TracyPlot("RenderGraph/ResourceCount", static_cast<int64_t>(m_resources.size()));
    TracyPlot("RenderGraph/PassNodeCount", static_cast<int64_t>(m_pass_nodes.size()));
    TracyPlot("RenderGraph/PassCount", static_cast<int64_t>(m_passes.size()));
#endif

    for (auto& resource : m_resources) {
        resource.current_version = 0;
        resource.first_pass = NumericLimits<u32>::max();
        resource.last_pass = 0;
        resource.live = false;
        resource.physical_allocation_index.clear();
        resource.versions.clear();
        resource.versions.append({ make_handle(&resource - m_resources.data(), 0), {}, {}, {}, NumericLimits<u32>::max(), 0, false });
        
        if (m_frame_index - resource.last_used_frame > 2) {
            destroy_transient_resource(resource);
        }
        resource.used_this_frame = false;
    }
}

void RenderGraph::add_pass(RenderGraphPass& pass)
{
    ZoneScoped;
    auto name = pass.name();
    ZoneText(reinterpret_cast<char const*>(name.bytes().data()), name.bytes().size());
    VERIFY(m_phase == Phase::Authoring);
    m_passes.append(&pass);
    m_compiled = false;
}

Optional<size_t> RenderGraph::find_resource_by_name(String const& name, ResourceKind type) const
{
    if (auto it = m_resource_index_by_name.find(name); it != m_resource_index_by_name.end()) {
        if (m_resources[it->value].type == type)
            return it->value;
    }
    return {};
}

TextureHandle RenderGraph::import_texture(ImportedTextureDesc const& desc)
{
    VERIFY(m_phase == Phase::Authoring);
    return as_texture_handle(import_image(desc.name, desc.image, desc.view, desc.format, desc.extent, desc.initial_state));
}

TextureHandle RenderGraph::create_texture(TextureDesc const& desc)
{
    VERIFY(m_phase == Phase::Authoring || m_phase == Phase::Building);
    auto existing = find_resource_by_name(desc.name, ResourceKind::Texture);
    size_t index = existing.value_or(m_resources.size());
    if (!existing.has_value()) {
        m_resources.append({});
        m_resource_index_by_name.set(desc.name, static_cast<u32>(index));
    }

    auto& resource = m_resources[index];
    resource.used_this_frame = true;
    resource.last_used_frame = m_frame_index;
    bool desc_changed = resource.external
        || resource.format != desc.format
        || resource.extent.width != desc.width
        || resource.extent.height != desc.height
        || resource.image_usage != desc.usage;
    if (desc_changed)
        destroy_transient_resource(resource);

    resource.name = desc.name;
    resource.type = ResourceKind::Texture;
    resource.external = false;
    resource.format = desc.format;
    resource.extent = { desc.width, desc.height };
    resource.image_usage = desc.usage;
    resource.initial_state = ResourceState::Undefined;
    resource.current_state = ResourceState::Undefined;
    resource.current_version = 0;
    resource.live = false;
    resource.physical_allocation_index.clear();
    resource.owns_image_memory = false;
    resource.versions.clear();
    resource.versions.append({ make_handle(index, 0), {}, {}, {}, NumericLimits<u32>::max(), 0, false });
    return as_texture_handle(make_handle(index, 0));
}

BufferHandle RenderGraph::create_buffer(BufferDesc const& desc)
{
    VERIFY(m_phase == Phase::Authoring || m_phase == Phase::Building);
    auto existing = find_resource_by_name(desc.name, ResourceKind::Buffer);
    size_t index = existing.value_or(m_resources.size());
    if (!existing.has_value()) {
        m_resources.append({});
        m_resource_index_by_name.set(desc.name, static_cast<u32>(index));
    }

    auto& resource = m_resources[index];
    resource.used_this_frame = true;
    resource.last_used_frame = m_frame_index;
    bool desc_changed = resource.external
        || resource.buffer_size != desc.size
        || resource.buffer_usage != desc.usage;
    if (desc_changed)
        destroy_transient_resource(resource);

    resource.name = desc.name;
    resource.type = ResourceKind::Buffer;
    resource.external = false;
    resource.buffer_size = desc.size;
    resource.buffer_usage = desc.usage;
    resource.initial_state = ResourceState::Undefined;
    resource.current_state = ResourceState::Undefined;
    resource.current_version = 0;
    resource.live = false;
    resource.physical_allocation_index.clear();
    resource.owns_buffer_memory = false;
    resource.versions.clear();
    resource.versions.append({ make_handle(index, 0), {}, {}, {}, NumericLimits<u32>::max(), 0, false });
    return as_buffer_handle(make_handle(index, 0));
}

BufferHandle RenderGraph::import_buffer(ImportedBufferDesc const& desc)
{
    VERIFY(m_phase == Phase::Authoring);
    return as_buffer_handle(import_buffer(desc.name, desc.buffer, desc.size, desc.initial_state));
}

ResourceHandle RenderGraph::import_image(String name, VkImage image, VkImageView view, VkFormat format, VkExtent2D extent, ResourceState initial_state)
{
    auto existing = find_resource_by_name(name, ResourceKind::Texture);
    size_t index = existing.value_or(m_resources.size());
    if (!existing.has_value()) {
        m_resources.append({});
        m_resource_index_by_name.set(name, static_cast<u32>(index));
    } else if (!m_resources[index].external) {
        destroy_transient_resource(m_resources[index]);
    }

    auto& resource = m_resources[index];
    resource.used_this_frame = true;
    resource.last_used_frame = m_frame_index;
    resource.name = move(name);
    resource.type = ResourceKind::Texture;
    resource.external = true;
    resource.format = format;
    resource.extent = extent;
    resource.image = image;
    resource.image_view = view;
    resource.initial_state = initial_state;
    resource.current_state = initial_state;
    resource.current_version = 0;
    resource.live = true;
    resource.physical_allocation_index.clear();
    resource.owns_image_memory = false;
    resource.versions.clear();
    resource.versions.append({ make_handle(index, 0), {}, {}, {}, NumericLimits<u32>::max(), 0, false });
    if (m_render_extent.width == 0 || m_render_extent.height == 0)
        m_render_extent = extent;
    return make_handle(index, 0);
}

ResourceHandle RenderGraph::import_buffer(String name, VkBuffer buffer, VkDeviceSize size, ResourceState initial_state)
{
    auto existing = find_resource_by_name(name, ResourceKind::Buffer);
    size_t index = existing.value_or(m_resources.size());
    if (!existing.has_value()) {
        m_resources.append({});
        m_resource_index_by_name.set(name, static_cast<u32>(index));
    } else if (!m_resources[index].external) {
        destroy_transient_resource(m_resources[index]);
    }

    auto& resource = m_resources[index];
    resource.used_this_frame = true;
    resource.last_used_frame = m_frame_index;
    resource.name = move(name);
    resource.type = ResourceKind::Buffer;
    resource.external = true;
    resource.buffer = buffer;
    resource.buffer_size = size;
    resource.initial_state = initial_state;
    resource.current_state = initial_state;
    resource.current_version = 0;
    resource.live = true;
    resource.physical_allocation_index.clear();
    resource.owns_buffer_memory = false;
    resource.versions.clear();
    resource.versions.append({ make_handle(index, 0), {}, {}, {}, NumericLimits<u32>::max(), 0, false });
    return make_handle(index, 0);
}

void RenderGraph::mark_output(ResourceHandle handle, ResourceState final_state)
{
    VERIFY(m_phase == Phase::Authoring);
    m_outputs.append({ handle, final_state });
}

void RenderGraph::export_texture(TextureHandle handle, ResourceState final_state)
{
    mark_output(handle.value, final_state);
}

void RenderGraph::export_buffer(BufferHandle handle, ResourceState final_state)
{
    mark_output(handle.value, final_state);
}

RenderGraph::ResourceNode& RenderGraph::resource(ResourceHandle handle)
{
    return m_resources[handle_resource_index(handle)];
}

RenderGraph::ResourceNode const& RenderGraph::resource(ResourceHandle handle) const
{
    return m_resources[handle_resource_index(handle)];
}

VkImage RenderGraph::get_image(ResourceHandle handle) const
{
    return resource(handle).image;
}

VkImageView RenderGraph::get_image_view(ResourceHandle handle) const
{
    return resource(handle).image_view;
}

VkBuffer RenderGraph::get_buffer(ResourceHandle handle) const
{
    return resource(handle).buffer;
}

RenderGraph::ResourceNode::VersionNode& RenderGraph::ensure_version(ResourceNode& node, u16 version_number)
{
    while (node.versions.size() <= version_number) {
        auto new_version = static_cast<u16>(node.versions.size());
        node.versions.append({ make_handle(&node - m_resources.data(), new_version), {}, {}, {}, NumericLimits<u32>::max(), 0, false });
    }
    return node.versions[version_number];
}

RenderGraph::ResourceNode::VersionNode& RenderGraph::version(ResourceHandle handle)
{
    auto& node = resource(handle);
    return ensure_version(node, handle_version(handle));
}

RenderGraph::ResourceNode::VersionNode const& RenderGraph::version(ResourceHandle handle) const
{
    auto const& node = resource(handle);
    VERIFY(handle_version(handle) < node.versions.size());
    return node.versions[handle_version(handle)];
}

void RenderGraph::add_pass_dependency(u32 pass_index, u32 dependency_pass_index)
{
    if (pass_index == dependency_pass_index)
        return;

    auto& dependencies = m_pass_nodes[pass_index].dependencies;
    for (auto existing : dependencies) {
        if (existing == dependency_pass_index)
            return;
    }
    dependencies.append(dependency_pass_index);
}

ResourceHandle RenderGraph::resolve_latest_handle(ResourceHandle handle) const
{
    auto const& node = resource(handle);
    return make_handle(handle_resource_index(handle), node.current_version);
}

ResourceHandle RenderGraph::register_read(u32 pass_index, ResourceHandle handle, ResourceState state, AccessKind access, PipelineDomain stage_hint, bool is_attachment, bool is_depth, bool is_storage, bool preserve_contents)
{
    VERIFY(m_phase == Phase::Building);
    VERIFY(handle != 0);
    auto resolved_handle = resolve_latest_handle(handle);
    auto& resolved_version = version(resolved_handle);
    if (resolved_version.producer_pass.has_value())
        add_pass_dependency(pass_index, *resolved_version.producer_pass);
    bool already_reader = false;
    for (auto reader_pass : resolved_version.reader_passes) {
        if (reader_pass == pass_index) {
            already_reader = true;
            break;
        }
    }
    if (!already_reader)
        resolved_version.reader_passes.append(pass_index);
    m_pass_nodes[pass_index].reads.append({ resolved_handle, state, access, m_pass_nodes[pass_index].queue, stage_hint, is_attachment, is_depth, is_storage, preserve_contents });
    return resolved_handle;
}

ResourceHandle RenderGraph::register_write(u32 pass_index, ResourceHandle handle, ResourceState state, AccessKind access, PipelineDomain stage_hint, bool is_attachment, bool is_depth, bool is_storage, bool preserve_contents)
{
    VERIFY(m_phase == Phase::Building);
    VERIFY(handle != 0);
    auto resolved_input_handle = resolve_latest_handle(handle);
    auto const& input_version = version(resolved_input_handle);
    if (input_version.producer_pass.has_value())
        add_pass_dependency(pass_index, *input_version.producer_pass);
    for (auto reader_pass : input_version.reader_passes)
        add_pass_dependency(pass_index, reader_pass);

    auto& resource_node = resource(handle);
    ++resource_node.current_version;
    auto new_handle = make_handle(handle_resource_index(handle), resource_node.current_version);
    auto& new_version = ensure_version(resource_node, resource_node.current_version);
    new_version.handle = new_handle;
    new_version.parent_handle = resolved_input_handle;
    new_version.producer_pass = pass_index;
    new_version.reader_passes.clear();
    new_version.first_pass = NumericLimits<u32>::max();
    new_version.last_pass = 0;
    m_pass_nodes[pass_index].writes.append({ new_handle, state, access, m_pass_nodes[pass_index].queue, stage_hint, is_attachment, is_depth, is_storage, preserve_contents });
    return new_handle;
}

void RenderGraph::register_color_attachment(u32 pass_index, ResourceHandle handle, VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op, VkClearValue clear_value)
{
    VERIFY(m_phase == Phase::Building);
    m_pass_nodes[pass_index].color_attachments.append({ handle, load_op, store_op, clear_value });
}

void RenderGraph::register_depth_attachment(u32 pass_index, ResourceHandle handle, VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op, VkClearValue clear_value)
{
    VERIFY(m_phase == Phase::Building);
    m_pass_nodes[pass_index].depth_attachment = AttachmentInfo { handle, load_op, store_op, clear_value };
}

void RenderGraph::build_pass_nodes()
{
    VERIFY(m_phase == Phase::Authoring);
    m_phase = Phase::Building;
    m_pass_nodes.clear();
    m_pass_nodes.ensure_capacity(m_passes.size());
    for (size_t i = 0; i < m_passes.size(); ++i) {
        m_pass_nodes.append({
            .pass = m_passes[i],
            .name = m_passes[i]->name(),
            .queue = QueueKind::Graphics,
            .execute_callback = {},
            .reads = {},
            .writes = {},
            .color_attachments = {},
            .depth_attachment = {},
            .dependencies = {},
            .image_barriers = {},
            .buffer_barriers = {},
            .culled = false,
            .never_cull = false,
            .side_effect = false,
            .sort_order = static_cast<u32>(i),
        });
        RenderPassBuilder builder(*this, static_cast<u32>(i));
        m_passes[i]->setup(builder);
    }
    m_phase = Phase::Authoring;
}

void RenderGraph::resolve_output_versions()
{
    for (auto& output : m_outputs)
        output.handle = resolve_latest_handle(output.handle);
}

ErrorOr<void> RenderGraph::validate_graph() const
{
    HashMap<String, ResourceKind> declared_resources;
    for (auto const& node : m_resources) {
        if (node.name.is_empty())
            continue;
        auto existing = declared_resources.get(node.name);
        if (existing.has_value() && existing.value() != node.type)
            return Error::from_string_literal("RenderGraph validation failed: duplicate resource name reused across resource kinds");
        declared_resources.set(node.name, node.type);
    }

    for (auto const& output : m_outputs) {
        if (output.handle == 0)
            return Error::from_string_literal("RenderGraph validation failed: output handle is invalid");
        auto resource_index = handle_resource_index(output.handle);
        if (resource_index >= m_resources.size())
            return Error::from_string_literal("RenderGraph validation failed: exported resource index is invalid");
        auto const& resource_node = m_resources[resource_index];
        if (handle_version(output.handle) >= resource_node.versions.size())
            return Error::from_string_literal("RenderGraph validation failed: exported resource version does not exist");
    }

    for (auto const& pass : m_pass_nodes) {
        for (auto const& read : pass.reads) {
            if (read.handle == 0)
                return Error::from_string_literal("RenderGraph validation failed: pass declares invalid read handle");
            auto const& read_resource = resource(read.handle);
            auto const& read_version = version(read.handle);
            bool has_source = read_resource.external || read_version.producer_pass.has_value();
            if (!has_source)
                return Error::from_string_literal("RenderGraph validation failed: read of resource version with no producer and no import");
        }
        for (auto const& write : pass.writes) {
            if (write.handle == 0)
                return Error::from_string_literal("RenderGraph validation failed: pass declares invalid write handle");
        }
    }

    return {};
}

void RenderGraph::cull_dead_passes()
{
    HashTable<ResourceHandle> live_versions;
    Vector<ResourceHandle> worklist;

    for (auto& resource_node : m_resources) {
        resource_node.live = false;
        for (auto& version_node : resource_node.versions)
            version_node.live = false;
    }

    for (auto& pass : m_pass_nodes)
        pass.culled = true;

    auto enqueue_live_version = [&](ResourceHandle handle) {
        if (handle == 0 || live_versions.contains(handle))
            return;
        live_versions.set(handle);
        worklist.append(handle);
    };

    for (auto const& output : m_outputs)
        enqueue_live_version(output.handle);

    for (auto pass_index = 0u; pass_index < m_pass_nodes.size(); ++pass_index) {
        auto& pass = m_pass_nodes[pass_index];
        if (!(pass.never_cull || pass.side_effect))
            continue;
        pass.culled = false;
        for (auto const& read : pass.reads)
            enqueue_live_version(read.handle);
        for (auto const& write : pass.writes)
            enqueue_live_version(write.handle);
    }

    while (!worklist.is_empty()) {
        auto handle = worklist.take_last();
        auto& resource_node = resource(handle);
        auto& version_node = version(handle);
        resource_node.live = true;
        version_node.live = true;

        if (!version_node.producer_pass.has_value())
            continue;

        auto& pass = m_pass_nodes[*version_node.producer_pass];
        if (pass.culled)
            pass.culled = false;

        for (auto const& read : pass.reads)
            enqueue_live_version(read.handle);
        for (auto const& write : pass.writes)
            enqueue_live_version(write.handle);
    }
}

void RenderGraph::compute_lifetimes()
{
    m_compiled_graph.resource_lifetimes.clear();
    m_compiled_graph.version_lifetimes.clear();
    m_compiled_graph.versions.clear();

    auto ordered_pass_count = static_cast<u32>(m_execution_order.size());

    for (auto& node : m_resources) {
        node.first_pass = NumericLimits<u32>::max();
        node.last_pass = 0;
        for (auto& version_node : node.versions) {
            version_node.first_pass = NumericLimits<u32>::max();
            version_node.last_pass = 0;
        }
    }

    for (size_t execution_index = 0; execution_index < m_execution_order.size(); ++execution_index) {
        auto pass_index = m_execution_order[execution_index];
        auto const& pass = m_pass_nodes[pass_index];
        if (pass.culled)
            continue;

        auto update_lifetime = [&](ResourceHandle handle) {
            auto& node = resource(handle);
            auto& version_node = version(handle);
            node.first_pass = min(node.first_pass, static_cast<u32>(execution_index));
            node.last_pass = max(node.last_pass, static_cast<u32>(execution_index));
            version_node.first_pass = min(version_node.first_pass, static_cast<u32>(execution_index));
            version_node.last_pass = max(version_node.last_pass, static_cast<u32>(execution_index));
        };

        for (auto const& read : pass.reads)
            update_lifetime(read.handle);
        for (auto const& write : pass.writes)
            update_lifetime(write.handle);
    }

    auto is_exported_version = [&](ResourceHandle handle) {
        for (auto const& output : m_outputs) {
            if (output.handle == handle)
                return true;
        }
        return false;
    };

    for (size_t resource_index = 0; resource_index < m_resources.size(); ++resource_index) {
        auto& node = m_resources[resource_index];
        if (!node.live)
            continue;

        bool resource_exported = false;
        for (auto& version_node : node.versions) {
            if (!version_node.live)
                continue;
            bool imported = node.external && handle_version(version_node.handle) == 0;
            bool exported = is_exported_version(version_node.handle);
            if (imported)
                version_node.first_pass = 0;
            if (exported)
                version_node.last_pass = max(version_node.last_pass, ordered_pass_count);
            resource_exported = resource_exported || exported;

            m_compiled_graph.versions.append({
                .handle = version_node.handle,
                .parent_handle = version_node.parent_handle,
                .producer_pass = version_node.producer_pass,
                .imported = imported,
                .exported = exported,
                .live = version_node.live,
            });
            if (version_node.first_pass != NumericLimits<u32>::max()) {
                m_compiled_graph.version_lifetimes.append({
                    .handle = version_node.handle,
                    .resource_index = static_cast<u32>(resource_index),
                    .imported = imported,
                    .exported = exported,
                    .first_use = version_node.first_pass,
                    .last_use = version_node.last_pass,
                });
            }
        }

        if (node.external)
            node.first_pass = 0;
        if (resource_exported)
            node.last_pass = max(node.last_pass, ordered_pass_count);

        if (node.first_pass != NumericLimits<u32>::max()) {
            m_compiled_graph.resource_lifetimes.append({
                .resource_index = static_cast<u32>(resource_index),
                .kind = node.type,
                .imported = node.external,
                .exported = resource_exported,
                .first_use = node.first_pass,
                .last_use = node.last_pass,
            });
        }
    }
}

ErrorOr<void> RenderGraph::validate_lifetimes() const
{
    HashMap<u32, u32> pass_execution_indices;
    for (u32 execution_index = 0; execution_index < m_compiled_graph.ordered_passes.size(); ++execution_index)
        pass_execution_indices.set(m_compiled_graph.ordered_passes[execution_index].pass_index, execution_index);

    HashMap<u32, ResourceLifetimeInterval const*> resource_lifetimes;
    for (auto const& lifetime : m_compiled_graph.resource_lifetimes) {
        if (lifetime.first_use > lifetime.last_use)
            return Error::from_string_literal("RenderGraph validation failed: resource lifetime has invalid range");
        resource_lifetimes.set(lifetime.resource_index, &lifetime);
    }

    HashMap<ResourceHandle, VersionLifetimeInterval const*> version_lifetimes;
    for (auto const& lifetime : m_compiled_graph.version_lifetimes) {
        if (lifetime.first_use > lifetime.last_use)
            return Error::from_string_literal("RenderGraph validation failed: version lifetime has invalid range");
        version_lifetimes.set(lifetime.handle, &lifetime);
    }

    for (auto const& version_info : m_compiled_graph.versions) {
        if (!version_info.live)
            continue;
        if (!version_lifetimes.contains(version_info.handle))
            return Error::from_string_literal("RenderGraph validation failed: live version is missing lifetime data");

        auto const* version_lifetime = version_lifetimes.get(version_info.handle).value();
        auto resource_lifetime = resource_lifetimes.get(handle_resource_index(version_info.handle));
        if (!resource_lifetime.has_value())
            return Error::from_string_literal("RenderGraph validation failed: live resource is missing lifetime data");
        if (version_lifetime->first_use < resource_lifetime.value()->first_use || version_lifetime->last_use > resource_lifetime.value()->last_use)
            return Error::from_string_literal("RenderGraph validation failed: version lifetime escapes base resource lifetime");

        if (version_info.producer_pass.has_value()) {
            auto execution_index = pass_execution_indices.get(*version_info.producer_pass);
            if (!execution_index.has_value())
                return Error::from_string_literal("RenderGraph validation failed: version producer is missing from compiled pass order");
            if (*execution_index < version_lifetime->first_use || *execution_index > version_lifetime->last_use)
                return Error::from_string_literal("RenderGraph validation failed: version producer falls outside version lifetime");
        }
    }

    for (u32 execution_index = 0; execution_index < m_compiled_graph.ordered_passes.size(); ++execution_index) {
        auto const& pass = m_compiled_graph.ordered_passes[execution_index];
        for (auto const& resource_use : pass.resource_uses) {
            auto version_lifetime = version_lifetimes.get(resource_use.handle);
            if (!version_lifetime.has_value())
                return Error::from_string_literal("RenderGraph validation failed: compiled pass references version with no lifetime");
            if (execution_index < version_lifetime.value()->first_use || execution_index > version_lifetime.value()->last_use)
                return Error::from_string_literal("RenderGraph validation failed: compiled pass uses version outside its lifetime");
        }
    }

    return {};
}

void RenderGraph::plan_physical_allocations()
{
    ZoneScoped;
    m_compiled_graph.allocations.clear();
    HashMap<u32, ResourceLifetimeInterval const*> lifetime_by_resource_index;
    for (auto const& lifetime : m_compiled_graph.resource_lifetimes)
        lifetime_by_resource_index.set(lifetime.resource_index, &lifetime);

    auto resources_overlap = [](ResourceLifetimeInterval const& a, ResourceLifetimeInterval const& b) {
        return !(a.last_use < b.first_use || b.last_use < a.first_use);
    };

    auto resources_are_compatible = [&](ResourceNode const& resource, PhysicalAllocation const& allocation) {
        if (resource.type != allocation.kind)
            return false;
        if (resource.type == ResourceKind::Texture) {
            return resource.format == allocation.format
                && resource.extent.width == allocation.extent.width
                && resource.extent.height == allocation.extent.height
                && resource.image_usage == allocation.image_usage;
        }
        return resource.buffer_size == allocation.buffer_size
            && resource.buffer_usage == allocation.buffer_usage;
    };

    Vector<size_t> sorted_lifetime_indices;
    sorted_lifetime_indices.ensure_capacity(m_compiled_graph.resource_lifetimes.size());
    for (size_t i = 0; i < m_compiled_graph.resource_lifetimes.size(); ++i)
        sorted_lifetime_indices.append(i);

    quick_sort(sorted_lifetime_indices, [&](size_t a, size_t b) {
        auto const& la = m_compiled_graph.resource_lifetimes[a];
        auto const& lb = m_compiled_graph.resource_lifetimes[b];
        if (la.first_use != lb.first_use)
            return la.first_use < lb.first_use;
        return la.resource_index < lb.resource_index;
    });

    for (auto lifetime_index : sorted_lifetime_indices) {
        auto const& lifetime = m_compiled_graph.resource_lifetimes[lifetime_index];
        auto& node = m_resources[lifetime.resource_index];
        node.physical_allocation_index.clear();

        if (lifetime.imported) {
            m_compiled_graph.allocations.append({
                .physical_index = static_cast<u32>(m_compiled_graph.allocations.size()),
                .kind = node.type,
                .imported = true,
                .exported = lifetime.exported,
                .format = node.format,
                .extent = node.extent,
                .image_usage = node.image_usage,
                .buffer_usage = node.buffer_usage,
                .buffer_size = node.buffer_size,
                .first_use = lifetime.first_use,
                .last_use = lifetime.last_use,
                .resource_indices = { lifetime.resource_index },
            });
            node.physical_allocation_index = m_compiled_graph.allocations.size() - 1;
            continue;
        }

        Optional<size_t> chosen_allocation;
        for (size_t allocation_index = 0; allocation_index < m_compiled_graph.allocations.size(); ++allocation_index) {
            auto& allocation = m_compiled_graph.allocations[allocation_index];
            if (allocation.imported)
                continue;
            if (!resources_are_compatible(node, allocation))
                continue;

            bool overlaps = false;
            for (auto aliased_resource_index : allocation.resource_indices) {
                auto aliased_lifetime = lifetime_by_resource_index.get(aliased_resource_index);
                VERIFY(aliased_lifetime.has_value());
                if (resources_overlap(lifetime, *aliased_lifetime.value())) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps)
                continue;

            chosen_allocation = allocation_index;
            break;
        }

        if (!chosen_allocation.has_value()) {
            m_compiled_graph.allocations.append({
                .physical_index = static_cast<u32>(m_compiled_graph.allocations.size()),
                .kind = node.type,
                .imported = false,
                .exported = lifetime.exported,
                .format = node.format,
                .extent = node.extent,
                .image_usage = node.image_usage,
                .buffer_usage = node.buffer_usage,
                .buffer_size = node.buffer_size,
                .first_use = lifetime.first_use,
                .last_use = lifetime.last_use,
                .resource_indices = { lifetime.resource_index },
            });
            chosen_allocation = m_compiled_graph.allocations.size() - 1;
        } else {
            auto& allocation = m_compiled_graph.allocations[*chosen_allocation];
            allocation.resource_indices.append(lifetime.resource_index);
            allocation.first_use = min(allocation.first_use, lifetime.first_use);
            allocation.last_use = max(allocation.last_use, lifetime.last_use);
            allocation.exported = allocation.exported || lifetime.exported;
        }

        node.physical_allocation_index = *chosen_allocation;
    }
}

void RenderGraph::build_submission_plan()
{
    m_compiled_graph.submission_plan.batches.clear();
    m_compiled_graph.submission_plan.queue_transitions.clear();
    m_compiled_graph.submission_plan.requires_multi_queue_submission = false;

    HashMap<u32, size_t> pass_lookup;
    for (size_t i = 0; i < m_compiled_graph.ordered_passes.size(); ++i)
        pass_lookup.set(m_compiled_graph.ordered_passes[i].pass_index, i);

    Optional<size_t> current_batch_index;
    for (auto const& pass : m_compiled_graph.ordered_passes) {
        if (!current_batch_index.has_value() || m_compiled_graph.submission_plan.batches[*current_batch_index].queue != pass.queue) {
            m_compiled_graph.submission_plan.batches.append({
                .queue = pass.queue,
                .batch_index = static_cast<u32>(m_compiled_graph.submission_plan.batches.size()),
                .pass_indices = {},
            });
            current_batch_index = m_compiled_graph.submission_plan.batches.size() - 1;
        }
        m_compiled_graph.submission_plan.batches[*current_batch_index].pass_indices.append(pass.pass_index);
    }

    for (auto const& pass : m_compiled_graph.ordered_passes) {
        for (auto dependency_pass_index : pass.dependencies) {
            auto dependency_lookup = pass_lookup.get(dependency_pass_index);
            if (!dependency_lookup.has_value())
                continue;

            auto const& dependency = m_compiled_graph.ordered_passes[dependency_lookup.value()];
            if (dependency.queue == pass.queue)
                continue;

            Vector<ResourceHandle> crossing_resources;
            for (auto const& resource_use : pass.resource_uses) {
                auto const& version_info = version(resource_use.handle);
                if (version_info.producer_pass.has_value() && *version_info.producer_pass == dependency.pass_index) {
                    bool already_recorded = false;
                    for (auto recorded : crossing_resources) {
                        if (recorded == resource_use.handle) {
                            already_recorded = true;
                            break;
                        }
                    }
                    if (!already_recorded)
                        crossing_resources.append(resource_use.handle);
                }
            }

            QueueTransition transition;
            transition.source_pass_index = dependency.pass_index;
            transition.source_queue = dependency.queue;
            transition.destination_pass_index = pass.pass_index;
            transition.destination_queue = pass.queue;
            transition.resources = move(crossing_resources);
            m_compiled_graph.submission_plan.queue_transitions.append(move(transition));
            m_compiled_graph.submission_plan.requires_multi_queue_submission = true;
        }
    }
}

ErrorOr<void> RenderGraph::validate_submission_plan() const
{
    HashMap<u32, size_t> pass_order;
    for (size_t i = 0; i < m_compiled_graph.ordered_passes.size(); ++i)
        pass_order.set(m_compiled_graph.ordered_passes[i].pass_index, i);

    for (size_t batch_index = 0; batch_index < m_compiled_graph.submission_plan.batches.size(); ++batch_index) {
        auto const& batch = m_compiled_graph.submission_plan.batches[batch_index];
        if (batch.batch_index != batch_index)
            return Error::from_string_literal("RenderGraph validation failed: submission batch index is inconsistent");
        for (auto pass_index : batch.pass_indices) {
            auto pass_lookup = pass_order.get(pass_index);
            if (!pass_lookup.has_value())
                return Error::from_string_literal("RenderGraph validation failed: submission batch references unknown pass");
            auto const& pass = m_compiled_graph.ordered_passes[pass_lookup.value()];
            if (pass.queue != batch.queue)
                return Error::from_string_literal("RenderGraph validation failed: submission batch mixes queue kinds");
        }
    }

    for (auto const& transition : m_compiled_graph.submission_plan.queue_transitions) {
        auto source_lookup = pass_order.get(transition.source_pass_index);
        auto destination_lookup = pass_order.get(transition.destination_pass_index);
        if (!source_lookup.has_value() || !destination_lookup.has_value())
            return Error::from_string_literal("RenderGraph validation failed: submission plan references unknown cross-queue pass");
        if (*source_lookup >= *destination_lookup)
            return Error::from_string_literal("RenderGraph validation failed: cross-queue transition is not forward-ordered");
        auto const& source_pass = m_compiled_graph.ordered_passes[source_lookup.value()];
        auto const& destination_pass = m_compiled_graph.ordered_passes[destination_lookup.value()];
        if (source_pass.queue != transition.source_queue || destination_pass.queue != transition.destination_queue)
            return Error::from_string_literal("RenderGraph validation failed: cross-queue transition queue metadata is inconsistent");
    }

    return {};
}

ErrorOr<void> RenderGraph::topological_sort_passes()
{
    m_execution_order.clear();

    Vector<u32> indegree;
    indegree.resize(m_pass_nodes.size());
    Vector<Vector<u32>> outgoing;
    outgoing.resize(m_pass_nodes.size());

    for (size_t pass_index = 0; pass_index < m_pass_nodes.size(); ++pass_index) {
        auto const& pass = m_pass_nodes[pass_index];
        if (pass.culled)
            continue;

        for (auto dependency : pass.dependencies) {
            if (dependency >= m_pass_nodes.size() || m_pass_nodes[dependency].culled)
                continue;
            ++indegree[pass_index];
            outgoing[dependency].append(static_cast<u32>(pass_index));
        }
    }

    HashTable<u32> emitted;
    while (m_execution_order.size() < m_pass_nodes.size()) {
        Optional<u32> next_pass;
        for (u32 pass_index = 0; pass_index < m_pass_nodes.size(); ++pass_index) {
            if (m_pass_nodes[pass_index].culled || emitted.contains(pass_index))
                continue;
            if (indegree[pass_index] == 0) {
                next_pass = pass_index;
                break;
            }
        }

        if (!next_pass.has_value())
            break;

        emitted.set(*next_pass);
        m_execution_order.append(*next_pass);
        for (auto dependent : outgoing[*next_pass]) {
            VERIFY(indegree[dependent] > 0);
            --indegree[dependent];
        }
    }

    for (u32 pass_index = 0; pass_index < m_pass_nodes.size(); ++pass_index) {
        if (!m_pass_nodes[pass_index].culled && !emitted.contains(pass_index))
            return Error::from_string_literal("RenderGraph validation failed: cycle detected in pass graph");
    }

    return {};
}

ErrorOr<void> RenderGraph::allocate_physical_resources()
{
    ZoneScoped;
    for (auto& node : m_resources) {
        if (!node.live || node.external)
            continue;
        auto allocation_index = node.physical_allocation_index;
        if (!allocation_index.has_value())
            continue;

        auto const& allocation = m_compiled_graph.allocations[*allocation_index];
        auto const owner_resource_index = allocation.resource_indices[0];
        auto& owner_node = m_resources[owner_resource_index];

        if (node.type == ResourceKind::Texture) {
            if (owner_node.image == VK_NULL_HANDLE) {
                TRY(VulkanResourceManager::create_image(
                    m_context.allocator(),
                    allocation.extent.width,
                    allocation.extent.height,
                    allocation.format,
                    VK_IMAGE_TILING_OPTIMAL,
                    allocation.image_usage,
                    VMA_MEMORY_USAGE_GPU_ONLY,
                    owner_node.image,
                    owner_node.image_allocation));
                owner_node.owns_image_memory = true;
            }

            if (node.image_view != VK_NULL_HANDLE && node.image != owner_node.image) {
                vkDestroyImageView(m_context.device(), node.image_view, nullptr);
                node.image_view = VK_NULL_HANDLE;
            }
            node.image = owner_node.image;
            node.image_allocation = owner_node.image_allocation;
            node.owns_image_memory = &node == &owner_node;

            if (node.image_view == VK_NULL_HANDLE) {
                VkImageViewCreateInfo view_info {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .image = node.image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = node.format,
                    .components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
                    .subresourceRange = { image_aspect_for_format(node.format), 0, 1, 0, 1 },
                };
                TRY(check_vulkan_result(vkCreateImageView(m_context.device(), &view_info, nullptr, &node.image_view), "vkCreateImageView failed in RenderGraph"sv));
            }
        } else {
            if (owner_node.buffer == VK_NULL_HANDLE) {
                VulkanBuffer buffer;
                TRY(VulkanResourceManager::create_buffer(
                    m_context.allocator(),
                    allocation.buffer_size,
                    allocation.buffer_usage,
                    VMA_MEMORY_USAGE_GPU_ONLY,
                    buffer));
                owner_node.buffer = buffer.buffer;
                owner_node.buffer_allocation = buffer.allocation;
                owner_node.owns_buffer_memory = true;
            }

            node.buffer = owner_node.buffer;
            node.buffer_allocation = owner_node.buffer_allocation;
            node.owns_buffer_memory = &node == &owner_node;
        }
    }
    return {};
}

void RenderGraph::solve_barriers()
{
    ZoneScoped;
    struct PreviousUse {
        ResourceHandle handle { 0 };
        AbstractResourceState state;
        ResourceState resource_state { ResourceState::Undefined };
    };

    HashMap<u32, PreviousUse> previous_uses;
    for (u32 resource_index = 0; resource_index < m_resources.size(); ++resource_index) {
        auto& node = m_resources[resource_index];
        node.current_state = node.initial_state;
        previous_uses.set(resource_index, PreviousUse {
            .handle = make_handle(resource_index, 0),
            .state = abstract_state_for_initial_state(node),
            .resource_state = node.initial_state,
        });
    }

    auto needs_barrier = [&](AbstractResourceState const& before, AbstractResourceState const& after) {
        if (before.layout != after.layout)
            return true;
        if (before.queue != after.queue)
            return true;
        if (before.writable || after.writable)
            return true;
        if (before.access != after.access)
            return true;
        if (before.stages != after.stages)
            return true;
        return false;
    };

    auto append_barrier = [&](CompiledPass& pass, PreviousUse const& previous, CompiledResourceUse const& current) {
        auto& node = resource(current.handle);
        if (!needs_barrier(previous.state, current.state))
            return;

        if (node.type == ResourceKind::Texture) {
            pass.pre_barriers.append({
                .queue = pass.queue,
                .from_version = previous.handle,
                .to_version = current.handle,
                .from_state = previous.state,
                .to_state = current.state,
            });
        } else {
            pass.pre_barriers.append({
                .queue = pass.queue,
                .from_version = previous.handle,
                .to_version = current.handle,
                .from_state = previous.state,
                .to_state = current.state,
            });
        }
    };

    for (auto& compiled_pass : m_compiled_graph.ordered_passes) {
        compiled_pass.pre_barriers.clear();
        for (auto const& resource_use : compiled_pass.resource_uses) {
            auto resource_index = handle_resource_index(resource_use.handle);
            auto previous = previous_uses.get(resource_index);
            VERIFY(previous.has_value());
            append_barrier(compiled_pass, *previous, resource_use);
            previous_uses.set(resource_index, PreviousUse {
                .handle = resource_use.handle,
                .state = resource_use.state,
                .resource_state = resource_use.resource_state,
            });
            resource(resource_use.handle).current_state = resource_use.resource_state;
        }
    }
}

void RenderGraph::build_compiled_graph()
{
    m_compiled_graph.ordered_passes.clear();
    m_compiled_graph.ordered_passes.ensure_capacity(m_execution_order.size());

    for (auto pass_index : m_execution_order) {
        auto& pass = m_pass_nodes[pass_index];
        if (pass.culled)
            continue;

        Vector<CompiledResourceUse> compiled_uses;
        compiled_uses.ensure_capacity(pass.reads.size() + pass.writes.size());
        for (auto const& read : pass.reads) {
            compiled_uses.append({
                .handle = read.handle,
                .resource_state = read.state,
                .access_kind = read.access,
                .stage_hint = read.stage_hint,
                .is_attachment = read.is_attachment,
                .is_depth = read.is_depth,
                .is_storage = read.is_storage,
                .preserve_contents = read.preserve_contents,
                .state = abstract_state_for_usage(read),
            });
        }
        for (auto const& write : pass.writes) {
            compiled_uses.append({
                .handle = write.handle,
                .resource_state = write.state,
                .access_kind = write.access,
                .stage_hint = write.stage_hint,
                .is_attachment = write.is_attachment,
                .is_depth = write.is_depth,
                .is_storage = write.is_storage,
                .preserve_contents = write.preserve_contents,
                .state = abstract_state_for_usage(write),
            });
        }

        m_compiled_graph.ordered_passes.append({
            .pass_index = pass_index,
            .name = pass.name,
            .queue = pass.queue,
            .dependencies = pass.dependencies,
            .resource_uses = move(compiled_uses),
            .pre_barriers = {},
            .color_attachments = pass.color_attachments,
            .depth_attachment = pass.depth_attachment,
            .execute_callback = pass.execute_callback.has_value() ? &*pass.execute_callback : nullptr,
            .pass = pass.pass,
        });
    }
}

ErrorOr<void> RenderGraph::compile()
{
    ZoneScoped;
    VERIFY(m_phase == Phase::Authoring);

    {
        ZoneScopedN("RenderGraph/BuildPassNodes");
        build_pass_nodes();
    }
    TRY(validate_graph());
    resolve_output_versions();
    {
        ZoneScopedN("RenderGraph/CullDeadPasses");
        cull_dead_passes();
    }

    for (auto const& output : m_outputs) {
        auto const& output_version = version(output.handle);
        if (!output_version.live)
            return Error::from_string_literal("RenderGraph validation failed: exported resource version is dead");
    }

    {
        ZoneScopedN("RenderGraph/TopologicalSort");
        TRY(topological_sort_passes());
    }
    {
        ZoneScopedN("RenderGraph/BuildCompiledGraph");
        build_compiled_graph();
    }
    {
        ZoneScopedN("RenderGraph/ComputeLifetimes");
        compute_lifetimes();
    }
    TRY(validate_lifetimes());
    {
        ZoneScopedN("RenderGraph/PlanAllocations");
        plan_physical_allocations();
    }
    {
        ZoneScopedN("RenderGraph/BuildSubmissionPlan");
        build_submission_plan();
    }
    TRY(validate_submission_plan());

    m_compiled = true;
    return {};
}

ErrorOr<void> RenderGraph::transition_outputs_for_present(VkCommandBuffer cmd)
{
    Vector<VkImageMemoryBarrier2> barriers;
    for (auto const& output : m_outputs) {
        auto& node = resource(output.handle);
        if (node.type != ResourceKind::Texture || node.current_state == output.final_state)
            continue;
        barriers.append({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = stage_mask_for_state(node.current_state),
            .srcAccessMask = access_mask_for_state(node.current_state),
            .dstStageMask = stage_mask_for_state(output.final_state),
            .dstAccessMask = access_mask_for_state(output.final_state),
            .oldLayout = image_layout_for_state(node.current_state),
            .newLayout = image_layout_for_state(output.final_state),
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = node.image,
            .subresourceRange = { image_aspect_for_format(node.format), 0, 1, 0, 1 },
        });
        node.current_state = output.final_state;
    }

    if (!barriers.is_empty()) {
        VkDependencyInfo dependency_info {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = static_cast<u32>(barriers.size()),
            .pImageMemoryBarriers = barriers.data(),
        };
        vkCmdPipelineBarrier2(cmd, &dependency_info);
    }

    return {};
}

ErrorOr<void> RenderGraph::execute(VkCommandBuffer cmd)
{
    if (!m_compiled)
        TRY(compile());

    VERIFY(m_phase == Phase::Authoring);
    m_phase = Phase::Executing;
    ScopeGuard execution_guard = [&] { m_phase = Phase::Authoring; };

    TRY(allocate_physical_resources());
    solve_barriers();

    for (auto& pass : m_compiled_graph.ordered_passes) {
        Vector<VkImageMemoryBarrier2> image_barriers;
        Vector<VkBufferMemoryBarrier2> buffer_barriers;
        for (auto const& barrier : pass.pre_barriers) {
            if (auto image_barrier = lower_image_barrier(barrier); image_barrier.has_value())
                image_barriers.append(*image_barrier);
            if (auto buffer_barrier = lower_buffer_barrier(barrier); buffer_barrier.has_value())
                buffer_barriers.append(*buffer_barrier);
        }

        if (!image_barriers.is_empty() || !buffer_barriers.is_empty()) {
            VkDependencyInfo dependency_info {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = 0,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = nullptr,
                .bufferMemoryBarrierCount = static_cast<u32>(buffer_barriers.size()),
                .pBufferMemoryBarriers = buffer_barriers.data(),
                .imageMemoryBarrierCount = static_cast<u32>(image_barriers.size()),
                .pImageMemoryBarriers = image_barriers.data(),
            };
            vkCmdPipelineBarrier2(cmd, &dependency_info);
        }

        bool const has_rendering_attachments = !pass.color_attachments.is_empty() || pass.depth_attachment.has_value();
        if (has_rendering_attachments) {
            Vector<VkRenderingAttachmentInfo> color_attachments;
            color_attachments.ensure_capacity(pass.color_attachments.size());

            VkExtent2D render_extent = m_render_extent;
            for (auto const& attachment : pass.color_attachments) {
                auto const& node = resource(attachment.handle);
                render_extent = node.extent;
                color_attachments.append(lower_color_attachment(attachment));
            }

            Optional<VkRenderingAttachmentInfo> depth_attachment;
            if (pass.depth_attachment.has_value()) {
                auto const& attachment = *pass.depth_attachment;
                auto const& node = resource(attachment.handle);
                if (render_extent.width == 0 || render_extent.height == 0)
                    render_extent = node.extent;
                depth_attachment = lower_depth_attachment(attachment);
            }

            VkRenderingInfo rendering_info {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .pNext = nullptr,
                .flags = 0,
                .renderArea = { { 0, 0 }, render_extent },
                .layerCount = 1,
                .viewMask = 0,
                .colorAttachmentCount = static_cast<u32>(color_attachments.size()),
                .pColorAttachments = color_attachments.data(),
                .pDepthAttachment = depth_attachment.has_value() ? &*depth_attachment : nullptr,
                .pStencilAttachment = nullptr,
            };
            vkCmdBeginRendering(cmd, &rendering_info);
            m_render_extent = render_extent;
        }

        if (pass.execute_callback)
            (*pass.execute_callback)(cmd, *this);
        else
            pass.pass->execute(cmd, *this);

        if (has_rendering_attachments)
            vkCmdEndRendering(cmd);
    }

    TRY(transition_outputs_for_present(cmd));

#if defined(TRACY_ENABLE)
    if (m_tracy_vk_ctx) {
        TracyVkCollect(m_tracy_vk_ctx, cmd);
    }
#endif

    return {};
}

String RenderGraph::debug_description() const
{
    StringBuilder builder;
    builder.appendff("RenderGraph\n");
    builder.appendff("Passes ({})\n", m_compiled_graph.ordered_passes.size());
    for (size_t i = 0; i < m_compiled_graph.ordered_passes.size(); ++i) {
        auto const& pass = m_compiled_graph.ordered_passes[i];
        builder.appendff("  [{}] pass={} name={} queue={} deps=", i, pass.pass_index, pass.name, queue_kind_name(pass.queue));
        for (size_t dep_index = 0; dep_index < pass.dependencies.size(); ++dep_index) {
            builder.appendff("{}{}", dep_index == 0 ? "" : ",", pass.dependencies[dep_index]);
        }
        builder.appendff(" uses={} barriers={}\n", pass.resource_uses.size(), pass.pre_barriers.size());
        for (auto const& resource_use : pass.resource_uses) {
            auto const& node = resource(resource_use.handle);
            builder.appendff("    use handle=0x{:x} resource={} state={} queue={} writable={}\n",
                resource_use.handle,
                node.name,
                resource_state_name(resource_use.resource_state),
                queue_kind_name(resource_use.state.queue),
                resource_use.state.writable ? "true" : "false");
        }
    }

    builder.appendff("Versions ({})\n", m_compiled_graph.versions.size());
    for (auto const& version_info : m_compiled_graph.versions) {
        auto const& node = resource(version_info.handle);
        builder.appendff("  handle=0x{:x} resource={} imported={} exported={} live={} producer=",
            version_info.handle,
            node.name,
            version_info.imported ? "true" : "false",
            version_info.exported ? "true" : "false",
            version_info.live ? "true" : "false");
        if (version_info.producer_pass.has_value())
            builder.appendff("{}\n", *version_info.producer_pass);
        else
            builder.appendff("none\n");
    }

    builder.appendff("Resource Lifetimes ({})\n", m_compiled_graph.resource_lifetimes.size());
    for (auto const& lifetime : m_compiled_graph.resource_lifetimes) {
        auto const& node = m_resources[lifetime.resource_index];
        builder.appendff("  resource={} kind={} first={} last={} imported={} exported={}\n",
            node.name,
            resource_kind_name(lifetime.kind),
            lifetime.first_use,
            lifetime.last_use,
            lifetime.imported ? "true" : "false",
            lifetime.exported ? "true" : "false");
    }

    builder.appendff("Version Lifetimes ({})\n", m_compiled_graph.version_lifetimes.size());
    for (auto const& lifetime : m_compiled_graph.version_lifetimes) {
        auto const& node = m_resources[lifetime.resource_index];
        builder.appendff("  handle=0x{:x} resource={} first={} last={} imported={} exported={}\n",
            lifetime.handle,
            node.name,
            lifetime.first_use,
            lifetime.last_use,
            lifetime.imported ? "true" : "false",
            lifetime.exported ? "true" : "false");
    }

    builder.appendff("Allocations ({})\n", m_compiled_graph.allocations.size());
    for (auto const& allocation : m_compiled_graph.allocations) {
        builder.appendff("  physical={} kind={} first={} last={} imported={} exported={} resources=",
            allocation.physical_index,
            resource_kind_name(allocation.kind),
            allocation.first_use,
            allocation.last_use,
            allocation.imported ? "true" : "false",
            allocation.exported ? "true" : "false");
        for (size_t i = 0; i < allocation.resource_indices.size(); ++i) {
            auto resource_index = allocation.resource_indices[i];
            builder.appendff("{}{}", i == 0 ? "" : ",", m_resources[resource_index].name);
        }
        builder.appendff("\n");
    }

    builder.appendff("Submission Batches ({})\n", m_compiled_graph.submission_plan.batches.size());
    for (auto const& batch : m_compiled_graph.submission_plan.batches) {
        builder.appendff("  batch={} queue={} passes=", batch.batch_index, queue_kind_name(batch.queue));
        for (size_t i = 0; i < batch.pass_indices.size(); ++i)
            builder.appendff("{}{}", i == 0 ? "" : ",", batch.pass_indices[i]);
        builder.appendff("\n");
    }

    builder.appendff("Queue Transitions ({}) multi_queue={}\n",
        m_compiled_graph.submission_plan.queue_transitions.size(),
        m_compiled_graph.submission_plan.requires_multi_queue_submission ? "true" : "false");
    for (auto const& transition : m_compiled_graph.submission_plan.queue_transitions) {
        builder.appendff("  {}:{} -> {}:{} resources=",
            transition.source_pass_index,
            queue_kind_name(transition.source_queue),
            transition.destination_pass_index,
            queue_kind_name(transition.destination_queue));
        for (size_t i = 0; i < transition.resources.size(); ++i) {
            auto handle = transition.resources[i];
            builder.appendff("{}0x{:x}", i == 0 ? "" : ",", handle);
        }
        builder.appendff("\n");
    }

    return MUST(builder.to_string());
}

}
