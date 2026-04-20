# MyceliumVR Render Graph

MyceliumVR uses a DAG-based `RenderGraph` to orchestrate Vulkan rendering. It handles resource lifetime management, automatic synchronization (barriers), and pass execution ordering.

## Key Concepts

### 1. Passes
A pass is a self-contained unit of rendering work. You can implement the `RenderGraphPass` interface or use the `CallbackGraphPass` for quick implementations.

- **Setup Phase**: Declare which resources the pass reads from or writes to.
- **Execute Phase**: Record Vulkan commands into the provided command buffer.

### 2. Resources
Resources are either **Textures** or **Buffers**.
- **Transient Resources**: Created via `builder.create_texture()` or `builder.create_buffer()`. Their memory is automatically managed and reused by the graph.
- **Imported Resources**: External resources (like the Swapchain backbuffer) brought into the graph via `graph.import_texture()` or `graph.import_buffer()`.

### 3. Handles and Versioning
The graph uses `TextureHandle` and `BufferHandle`. Every time a resource is written to, the graph produces a new "version" handle. You must always use the handle returned by the registration methods (e.g., `builder.write_color(handle)`) for subsequent reads to ensure correct dependency tracking.

---

## Adding a New Pass

### Using CallbackGraphPass (Inline)

```cpp
auto my_texture = graph.create_texture(desc);

struct CallbackGraphPass final : public RenderGraphPass {
    // ... constructor and name() ...
    virtual void setup(RenderPassBuilder& builder) override {
        builder.write_color(my_texture);
    }
    virtual void execute(VkCommandBuffer cmd, RenderGraph& graph) override {
        // Record commands...
        auto vk_image = graph.get_texture(my_texture);
    }
};
graph.add_pass(my_pass);
```

### Implementing RenderGraphPass (Class)

1. Inherit from `RenderGraphPass`.
2. Implement `name()`, `setup(RenderPassBuilder&)`, and `execute(VkCommandBuffer, RenderGraph&)`.
3. Register your pass with `graph.add_pass(*my_pass_instance)`.

---

## Resource Registration API

Use the `RenderPassBuilder` in the `setup` phase:

- `read_sampled(texture)`: Read from a texture in a shader.
- `write_color(texture, load_store_ops)`: Use as a color attachment.
- `read_vertex(buffer)`: Use as a vertex buffer.
- `write_storage(buffer)`: Write to a storage buffer (SSBO).
- `set_queue_preference(QueueKind)`: Request execution on Graphics, Compute, or Transfer queues.

---

## Debugging and Troubleshooting

The `RenderGraph` provides several tools to help diagnose issues.

### 1. Debug Description
You can print a human-readable summary of the compiled graph, including pass order, resource lifetimes, and generated barriers:

```cpp
warnln("{}", graph.debug_description());
```

### 2. Graph Validation
The graph performs extensive validation during compilation. If a graph is invalid (e.g., circular dependencies, missing producers), `graph.execute()` will return an error.

Common validation errors:
- **"read of resource version with no producer"**: You are trying to read a version of a resource that was never written to.
- **"circular dependency detected"**: Your passes form a loop (Pass A depends on B, and B depends on A).

### 3. Frame Graph Validation
In `VulkanRenderer.cpp`, there is a `validate_compiled_frame_graph` helper that checks if the resulting graph matches expected frame invariants (e.g., "does the backbuffer actually get presented?").

### 4. Barrier Visualization
The `debug_description()` output lists every `vkCmdPipelineBarrier2` call the graph will inject. If you see unexpected flickering or corruption, check if the barriers between passes are correct.

---

## Implementation Details

- **Dynamic Rendering**: The graph uses `vkCmdBeginRendering` and `vkCmdEndRendering`. It does **not** use `VkRenderPass` or `VkFramebuffer` objects.
- **Physical Allocation**: The graph performs an aliasing optimization where resources with non-overlapping lifetimes share the same physical memory.
- **Multi-Queue**: The graph supports async compute and transfer by building a `SubmissionPlan` that handles cross-queue synchronization.
