DirectVulkan maps the same semantic `MG_State` model onto Vulkan's explicit command, resource and pipeline model. Unlike DirectGLES, a Vulkan callback usually records work into a command buffer and returns before the GPU executes it. The renderer therefore owns synchronization, per-frame allocators, descriptor resolution, render-pass compatibility and deferred destruction.

## Mental model

```text
GL call mutates MG_State
        |
        v
VulkanRenderer callback
        |
        +-- resolve semantic program / textures / FBO / VAO
        +-- create barriers and uploads outside render pass
        +-- resolve render pass + vertex input + pipeline
        +-- bind descriptors and transient buffer slices
        +-- record vkCmd*
        v
FrameContext submits later at Present or an explicit flush
```

`BackendObject_DirectVulkan` installs the common backend function table and forwards operations to `VulkanRenderer`. The renderer is deliberately composed of managers because each manager owns a different Vulkan lifetime or cache domain.

## Renderer composition

| Manager | Technical responsibility |
| --- | --- |
| `FrameContext` | Command buffers, fences, acquire/present semaphores and in-flight slots |
| `VkBufferManager` / `BufferArena` | Resident buffers, transient uploads, persistent maps and deferred releases |
| `VkTextureManager` | `VkImage`, views, content uploads, layouts, storage usage and texture GC |
| `ProgramFactory` | SPIR-V transforms, shader modules, reflection and descriptor/pipeline layouts |
| `PipelineFactory` | Content-addressed graphics pipeline creation, memoization and eviction |
| `VertexInputStateFactory` | VAO-to-Vulkan vertex binding/attribute descriptions |
| `VkRenderPassManager` | Attachment descriptions, render passes, framebuffers and compatibility |
| `UniformManager` | Dynamic UBOs, sampled/storage descriptors and transient descriptor sets |
| `VkClearManager` | Deferred GL clears and materialization before use |
| `SwapchainObject` | WSI images, extent, surface transform and swapchain image layout |

## Pages

- [The renderer and one draw](renderer-draw.md)
- [FrameContext and submission safety](frame-context.md)
- [Buffers, uploads and transient memory](buffers-and-memory.md)
- [Textures, views and image layouts](textures-and-layouts.md)
- [SPIR-V, descriptors and graphics pipelines](shaders-descriptors-pipelines.md)
- [Render passes, swapchain and presentation](render-pass-present.md)

## The key ordering rule

Vulkan resource creation, upload and layout transitions cannot be inserted arbitrarily inside a render pass. `SetupDraw` therefore prepares storage images and sampled textures first, ends an incompatible active pass when necessary, records barriers and only then commits the render pass and pipeline. This ordering is the central invariant of DirectVulkan.
