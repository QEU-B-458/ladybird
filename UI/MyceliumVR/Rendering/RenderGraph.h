/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "VulkanCommon.h"
#include "Backend/VulkanContext.h"
#include "Backend/VulkanResourceManager.h"

#include <UI/MyceliumVR/Support/Profiling.h>

#include <AK/Function.h>
#include <AK/HashTable.h>
#include <AK/HashMap.h>
#include <AK/NumericLimits.h>
#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>

namespace MyceliumVR {

using ResourceHandle = u32;

struct TextureHandle {
    ResourceHandle value { 0 };
};

struct BufferHandle {
    ResourceHandle value { 0 };
};

enum class ResourceKind {
    Texture,
    Buffer,
};

enum class ResourceState {
    Undefined,
    TransferDst,
    TransferSrc,
    ColorAttachment,
    DepthStencilAttachment,
    ShaderRead,
    VertexBuffer,
    IndirectBuffer,
    StorageRead,
    StorageWrite,
    Present,
};

enum class QueueKind {
    Graphics,
    Compute,
    Transfer,
};

enum class AccessKind {
    Read,
    Write,
    ReadWrite,
    Create,
    Import,
    Export,
};

enum class PipelineDomain {
    TopOfPipe,
    DrawIndirect,
    VertexInput,
    VertexShader,
    FragmentShader,
    EarlyDepth,
    LateDepth,
    ColorOutput,
    ComputeShader,
    Transfer,
    BottomOfPipe,
    Host,
    Present,
};

enum class TextureViewUsage {
    Sampled,
    ColorAttachment,
    DepthStencilAttachment,
    Storage,
    TransferSrc,
    TransferDst,
    ResolveSrc,
    ResolveDst,
    Present,
};

enum class BufferViewUsage {
    Vertex,
    Index,
    Uniform,
    Storage,
    Indirect,
    TransferSrc,
    TransferDst,
};

struct TextureDesc {
    String name;
    u32 width { 0 };
    u32 height { 0 };
    u32 depth { 1 };
    u32 mip_levels { 1 };
    u32 array_layers { 1 };
    VkFormat format { VK_FORMAT_UNDEFINED };
    VkSampleCountFlagBits samples { VK_SAMPLE_COUNT_1_BIT };
    VkImageUsageFlags usage { 0 };
    VkClearValue default_clear {};
    bool transient { true };
    bool imported { false };
    bool external_lifetime { false };
};

struct BufferDesc {
    String name;
    u64 size { 0 };
    u64 alignment { 0 };
    VkBufferUsageFlags usage { 0 };
    bool transient { true };
    bool imported { false };
    bool external_lifetime { false };
};

struct ImportedTextureDesc : public TextureDesc {
    VkImage image { VK_NULL_HANDLE };
    VkImageView view { VK_NULL_HANDLE };
    VkExtent2D extent {};
    ResourceState initial_state { ResourceState::Undefined };
};

struct ImportedBufferDesc : public BufferDesc {
    VkBuffer buffer { VK_NULL_HANDLE };
    ResourceState initial_state { ResourceState::Undefined };
};

struct LoadStoreOps {
    VkAttachmentLoadOp load_op { VK_ATTACHMENT_LOAD_OP_LOAD };
    VkAttachmentStoreOp store_op { VK_ATTACHMENT_STORE_OP_STORE };
    VkClearValue clear_value {};
};

struct DepthLoadStoreOps {
    VkAttachmentLoadOp load_op { VK_ATTACHMENT_LOAD_OP_LOAD };
    VkAttachmentStoreOp store_op { VK_ATTACHMENT_STORE_OP_STORE };
    VkClearValue clear_value {};
};

class RenderGraph;

struct ResourceUsage {
    ResourceHandle handle { 0 };
    ResourceState state { ResourceState::Undefined };
    AccessKind access { AccessKind::Read };
    QueueKind queue { QueueKind::Graphics };
    PipelineDomain stage_hint { PipelineDomain::TopOfPipe };
    bool is_attachment { false };
    bool is_depth { false };
    bool is_storage { false };
    bool preserve_contents { false };
};

struct AttachmentInfo {
    ResourceHandle handle { 0 };
    VkAttachmentLoadOp load_op { VK_ATTACHMENT_LOAD_OP_LOAD };
    VkAttachmentStoreOp store_op { VK_ATTACHMENT_STORE_OP_STORE };
    VkClearValue clear_value {};
};

class RenderPassBuilder {
public:
    explicit RenderPassBuilder(RenderGraph& graph, u32 pass_index)
        : m_graph(graph)
        , m_pass_index(pass_index)
    {
    }

    TextureHandle create_texture(TextureDesc const& desc);
    BufferHandle create_buffer(BufferDesc const& desc);
    TextureHandle import_texture(ImportedTextureDesc const& desc);
    BufferHandle import_buffer(ImportedBufferDesc const& desc);
    TextureHandle read_sampled(TextureHandle handle);
    TextureHandle read_transfer(TextureHandle handle);
    TextureHandle write_transfer(TextureHandle handle);
    TextureHandle read_storage(TextureHandle handle);
    TextureHandle write_storage(TextureHandle handle);
    TextureHandle read_color(TextureHandle handle);
    TextureHandle write_color(TextureHandle handle, LoadStoreOps const& ops = {});
    TextureHandle read_depth(TextureHandle handle);
    TextureHandle write_depth(TextureHandle handle, DepthLoadStoreOps const& ops = {});
    BufferHandle read_vertex(BufferHandle handle);
    BufferHandle read_index(BufferHandle handle);
    BufferHandle read_uniform(BufferHandle handle);
    BufferHandle read_storage(BufferHandle handle);
    BufferHandle write_storage(BufferHandle handle);
    BufferHandle read_indirect(BufferHandle handle);
    BufferHandle read_transfer(BufferHandle handle);
    BufferHandle write_transfer(BufferHandle handle);
    void set_execute(Function<void(VkCommandBuffer, RenderGraph&)>);
    void set_never_cull(bool value = true);
    void set_side_effect(bool value = true);
    void set_queue_preference(QueueKind queue);

private:
    RenderGraph& m_graph;
    u32 m_pass_index { 0 };
};

class RenderGraphPass {
public:
    virtual ~RenderGraphPass() = default;
    virtual String name() const = 0;
    virtual void setup(RenderPassBuilder&) = 0;
    virtual void execute(VkCommandBuffer, RenderGraph&) = 0;
};

class RenderGraph {
public:
    struct AbstractResourceState {
        QueueKind queue { QueueKind::Graphics };
        VkPipelineStageFlags2 stages { VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT };
        VkAccessFlags2 access { 0 };
        VkImageLayout layout { VK_IMAGE_LAYOUT_UNDEFINED };
        bool writable { false };
        bool discardable { false };
    };

    struct CompiledBarrier {
        QueueKind queue { QueueKind::Graphics };
        ResourceHandle from_version { 0 };
        ResourceHandle to_version { 0 };
        AbstractResourceState from_state;
        AbstractResourceState to_state;
    };

    struct ResourceLifetimeInterval {
        u32 resource_index { 0 };
        ResourceKind kind { ResourceKind::Texture };
        bool imported { false };
        bool exported { false };
        u32 first_use { NumericLimits<u32>::max() };
        u32 last_use { 0 };
    };

    struct VersionLifetimeInterval {
        ResourceHandle handle { 0 };
        u32 resource_index { 0 };
        bool imported { false };
        bool exported { false };
        u32 first_use { NumericLimits<u32>::max() };
        u32 last_use { 0 };
    };

    struct CompiledVersionInfo {
        ResourceHandle handle { 0 };
        Optional<ResourceHandle> parent_handle;
        Optional<u32> producer_pass;
        bool imported { false };
        bool exported { false };
        bool live { false };
    };

    struct CompiledResourceUse {
        ResourceHandle handle { 0 };
        ResourceState resource_state { ResourceState::Undefined };
        AccessKind access_kind { AccessKind::Read };
        PipelineDomain stage_hint { PipelineDomain::TopOfPipe };
        bool is_attachment { false };
        bool is_depth { false };
        bool is_storage { false };
        bool preserve_contents { false };
        AbstractResourceState state;
    };

    struct CompiledPass {
        u32 pass_index { 0 };
        String name;
        QueueKind queue { QueueKind::Graphics };
        Vector<u32> dependencies;
        Vector<CompiledResourceUse> resource_uses;
        Vector<CompiledBarrier> pre_barriers;
        Vector<AttachmentInfo> color_attachments;
        Optional<AttachmentInfo> depth_attachment;
        Function<void(VkCommandBuffer, RenderGraph&)>* execute_callback { nullptr };
        RenderGraphPass* pass { nullptr };
    };

    struct PhysicalAllocation {
        u32 physical_index { 0 };
        ResourceKind kind { ResourceKind::Texture };
        bool imported { false };
        bool exported { false };
        VkFormat format { VK_FORMAT_UNDEFINED };
        VkExtent2D extent {};
        VkImageUsageFlags image_usage { 0 };
        VkBufferUsageFlags buffer_usage { 0 };
        VkDeviceSize buffer_size { 0 };
        u32 first_use { NumericLimits<u32>::max() };
        u32 last_use { 0 };
        Vector<u32> resource_indices;
    };

    struct QueueTransition {
        u32 source_pass_index { 0 };
        QueueKind source_queue { QueueKind::Graphics };
        u32 destination_pass_index { 0 };
        QueueKind destination_queue { QueueKind::Graphics };
        Vector<ResourceHandle> resources;
    };

    struct SubmissionBatch {
        QueueKind queue { QueueKind::Graphics };
        u32 batch_index { 0 };
        Vector<u32> pass_indices;
    };

    struct SubmissionPlan {
        Vector<SubmissionBatch> batches;
        Vector<QueueTransition> queue_transitions;
        bool requires_multi_queue_submission { false };
    };

    struct CompiledGraph {
        Vector<CompiledPass> ordered_passes;
        Vector<ResourceLifetimeInterval> resource_lifetimes;
        Vector<VersionLifetimeInterval> version_lifetimes;
        Vector<CompiledVersionInfo> versions;
        Vector<PhysicalAllocation> allocations;
        SubmissionPlan submission_plan;
    };

    struct PassTiming {
        String name;
        double gpu_ms { 0.0 };
    };

    explicit RenderGraph(VulkanContext& ctx);
    ~RenderGraph();

    void reset_for_frame();
    void add_pass(RenderGraphPass& pass);

    ResourceHandle import_image(String name, VkImage image, VkImageView view, VkFormat format, VkExtent2D extent, ResourceState initial_state);
    ResourceHandle import_buffer(String name, VkBuffer buffer, VkDeviceSize size, ResourceState initial_state);
    TextureHandle create_texture(TextureDesc const& desc);
    BufferHandle create_buffer(BufferDesc const& desc);
    TextureHandle import_texture(ImportedTextureDesc const& desc);
    BufferHandle import_buffer(ImportedBufferDesc const& desc);
    void mark_output(ResourceHandle handle, ResourceState final_state);
    void export_texture(TextureHandle handle, ResourceState final_state = ResourceState::Present);
    void export_buffer(BufferHandle handle, ResourceState final_state = ResourceState::StorageWrite);
    void set_frame_push_constants(PushConstants const& push_constants) { m_frame_push_constants = push_constants; }
#if defined(TRACY_ENABLE)
    void set_tracy_context(TracyVkCtx ctx) { m_tracy_vk_ctx = ctx; }
    TracyVkCtx tracy_context() const { return m_tracy_vk_ctx; }
#endif
    CompiledGraph const& compiled_graph() const { return m_compiled_graph; }
    Vector<PassTiming> const& last_frame_timings() const { return m_last_frame_timings; }
    String debug_description() const;

    VkImage get_image(ResourceHandle handle) const;
    VkImageView get_image_view(ResourceHandle handle) const;
    VkBuffer get_buffer(ResourceHandle handle) const;
    VkExtent2D get_texture_extent(TextureHandle handle) const { return resource(handle.value).extent; }
    VkImage get_texture(TextureHandle handle) const { return get_image(handle.value); }
    VkImageView get_texture_view(TextureHandle handle) const { return get_image_view(handle.value); }
    VkBuffer get_buffer(BufferHandle handle) const { return get_buffer(handle.value); }
    VkExtent2D render_extent() const { return m_render_extent; }
    PushConstants const& frame_push_constants() const { return m_frame_push_constants; }
    VulkanContext& context() { return m_context; }
    VulkanContext const& context() const { return m_context; }

    ErrorOr<void> execute(VkCommandBuffer cmd);

private:
    enum class Phase {
        Authoring,
        Building,
        Executing,
    };

    struct ResourceNode {
        struct VersionNode {
            ResourceHandle handle { 0 };
            Optional<ResourceHandle> parent_handle;
            Optional<u32> producer_pass;
            Vector<u32> reader_passes;
            u32 first_pass { NumericLimits<u32>::max() };
            u32 last_pass { 0 };
            bool live { false };
        };

        String name;
        ResourceKind type { ResourceKind::Texture };
        bool external { false };

        VkFormat format { VK_FORMAT_UNDEFINED };
        VkExtent2D extent {};
        VkImageUsageFlags image_usage { 0 };
        VkBufferUsageFlags buffer_usage { 0 };
        VkDeviceSize buffer_size { 0 };

        VkImage image { VK_NULL_HANDLE };
        VkImageView image_view { VK_NULL_HANDLE };
        VmaAllocation image_allocation { VK_NULL_HANDLE };

        VkBuffer buffer { VK_NULL_HANDLE };
        VmaAllocation buffer_allocation { VK_NULL_HANDLE };
        Optional<u32> physical_allocation_index;
        bool owns_image_memory { false };
        bool owns_buffer_memory { false };

        ResourceState initial_state { ResourceState::Undefined };
        ResourceState current_state { ResourceState::Undefined };
        u16 current_version { 0 };
        u32 first_pass { NumericLimits<u32>::max() };
        u32 last_pass { 0 };
        bool live { false };
        u32 last_used_frame { 0 };
        bool used_this_frame { false };
        Vector<VersionNode> versions;
    };

    struct PassNode {
        RenderGraphPass* pass { nullptr };
        String name;
        QueueKind queue { QueueKind::Graphics };
        Optional<Function<void(VkCommandBuffer, RenderGraph&)>> execute_callback;
        Vector<ResourceUsage> reads;
        Vector<ResourceUsage> writes;
        Vector<AttachmentInfo> color_attachments;
        Optional<AttachmentInfo> depth_attachment;
        Vector<u32> dependencies;
        Vector<VkImageMemoryBarrier2> image_barriers;
        Vector<VkBufferMemoryBarrier2> buffer_barriers;
        bool culled { false };
        bool never_cull { false };
        bool side_effect { false };
        u32 sort_order { 0 };
    };

    struct OutputNode {
        ResourceHandle handle { 0 };
        ResourceState final_state { ResourceState::Present };
    };

    static constexpr u32 handle_version_bits = 16;
    static constexpr u32 handle_version_mask = (1u << handle_version_bits) - 1u;

    static ResourceHandle make_handle(u32 resource_index, u16 version)
    {
        return ((resource_index + 1u) << handle_version_bits) | version;
    }

    static u32 handle_resource_index(ResourceHandle handle)
    {
        VERIFY(handle != 0);
        return (handle >> handle_version_bits) - 1u;
    }

    static u16 handle_version(ResourceHandle handle)
    {
        return static_cast<u16>(handle & handle_version_mask);
    }

    Optional<size_t> find_resource_by_name(String const& name, ResourceKind type) const;
    ResourceHandle register_read(u32 pass_index, ResourceHandle handle, ResourceState state, AccessKind access = AccessKind::Read, PipelineDomain stage_hint = PipelineDomain::TopOfPipe, bool is_attachment = false, bool is_depth = false, bool is_storage = false, bool preserve_contents = false);
    ResourceHandle register_write(u32 pass_index, ResourceHandle handle, ResourceState state, AccessKind access = AccessKind::Write, PipelineDomain stage_hint = PipelineDomain::TopOfPipe, bool is_attachment = false, bool is_depth = false, bool is_storage = false, bool preserve_contents = false);
    void register_color_attachment(u32 pass_index, ResourceHandle handle, VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op, VkClearValue clear_value);
    void register_depth_attachment(u32 pass_index, ResourceHandle handle, VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op, VkClearValue clear_value);
    void add_pass_dependency(u32 pass_index, u32 dependency_pass_index);
    AbstractResourceState abstract_state_for_initial_state(ResourceNode const& node) const;
    AbstractResourceState abstract_state_for_usage(ResourceUsage const& usage) const;
    Optional<VkImageMemoryBarrier2> lower_image_barrier(CompiledBarrier const& barrier) const;
    Optional<VkBufferMemoryBarrier2> lower_buffer_barrier(CompiledBarrier const& barrier) const;
    VkRenderingAttachmentInfo lower_color_attachment(AttachmentInfo const& attachment) const;
    VkRenderingAttachmentInfo lower_depth_attachment(AttachmentInfo const& attachment) const;
    ResourceHandle resolve_latest_handle(ResourceHandle handle) const;
    void resolve_output_versions();
    ErrorOr<void> validate_graph() const;
    ErrorOr<void> topological_sort_passes();
    ResourceNode::VersionNode& ensure_version(ResourceNode& node, u16 version);
    ResourceNode::VersionNode& version(ResourceHandle handle);
    ResourceNode::VersionNode const& version(ResourceHandle handle) const;

    ErrorOr<void> compile();
    void build_pass_nodes();
    void cull_dead_passes();
    void compute_lifetimes();
    ErrorOr<void> validate_lifetimes() const;
    void plan_physical_allocations();
    void build_submission_plan();
    ErrorOr<void> validate_submission_plan() const;
    ErrorOr<void> allocate_physical_resources();
    void solve_barriers();
    void build_compiled_graph();
    ErrorOr<void> transition_outputs_for_present(VkCommandBuffer cmd);
    void destroy_transient_resource(ResourceNode&);
    ResourceNode& resource(ResourceHandle handle);
    ResourceNode const& resource(ResourceHandle handle) const;

    static constexpr u32 k_max_timestamp_passes = 64;

    VulkanContext& m_context;
    VkQueryPool m_timestamp_pool { VK_NULL_HANDLE };
    Vector<String> m_pending_pass_names; // names recorded this frame, in order
    Vector<PassTiming> m_last_frame_timings;
    Vector<RenderGraphPass*> m_passes;
    Vector<PassNode> m_pass_nodes;
    Vector<ResourceNode> m_resources;
    Vector<OutputNode> m_outputs;
    Vector<u32> m_execution_order;
    HashMap<String, u32> m_resource_index_by_name;
    u32 m_frame_index { 0 };
#if defined(TRACY_ENABLE)
    TracyVkCtx m_tracy_vk_ctx { nullptr };
#endif
    CompiledGraph m_compiled_graph;
    PushConstants m_frame_push_constants {};
    VkExtent2D m_render_extent {};
    bool m_compiled { false };
    Phase m_phase { Phase::Authoring };

    friend class RenderPassBuilder;
};

}
