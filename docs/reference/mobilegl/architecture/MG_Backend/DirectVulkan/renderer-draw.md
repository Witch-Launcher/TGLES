## One draw from GL call to command buffer

`DrawArrays`, `DrawElements` and their multi-draw variants compute their frontend parameters and funnel into `VulkanRenderer::SetupDraw`. The setup sequence is intentionally staged:

```text
Draw* parameters
  -> DrawSyncScope + texture GC
  -> resolve draw FBO, VAO and semantic program
  -> get transformed/reflected program
  -> begin command recording if needed
  -> prepare storage-image usage
  -> collect sampled textures
  -> stop active pass on feedback or incompatible layout
  -> materialize clears and transition sampled images
  -> resolve render pass
  -> validate vertex input
  -> resolve graphics pipeline
  -> begin/continue render pass
  -> bind uniforms, vertices and optional indices
  -> set dynamic state
  -> return to Draw* for vkCmdDraw[v]Indexed
```

The final draw command is intentionally late. A failure in vertex format mapping, descriptor preparation or render-pass creation must not record a draw with partially valid state.

## Program and transform selection

The swapchain's pre-transform contributes shader compile flags, so a rotated surface can use a compatible position fixup without changing the frontend projection. The program cache key also includes the explicit-LOD-0 option. If all samplers read a single mip level, the renderer can select that variant to avoid implicit-LOD feedback faults on affected devices.

The sampled texture set is memoized by program lifetime/version, transform flags and texture bind generation. This skips repeated frontend uniform walks while still running layout and feedback checks for every draw.

## Feedback and active render passes

If a sampled texture is one of the attachments used by the active render pass, the pass ends before descriptor preparation. The same happens if a sampled image needs a layout transition or a pending clear must be materialized. This is required by Vulkan's usage/layout rules: an image cannot remain a color attachment write target and simultaneously be sampled in a later command sequence without an explicit pass boundary and barrier.

## Vertex input preflight

`VertexInputStateFactory` converts enabled GL attributes into Vulkan formats and binding descriptions. Before pipeline creation, `SetupDraw` checks that every shader-read attribute has a usable format. It also verifies that disabled attributes can be synthesized from their current generic values. This prevents an invalid or incomplete vertex description from being baked into a cached pipeline.

## Binding and dynamic state

After `vkCmdBindPipeline`, `UniformManager` binds reflected descriptor resources and dynamic UBO offsets. Vertex and index streams are then bound from resident buffers or frame-local upload slices. Viewport, scissor, blend constants, polygon offset, line width and stencil references are dynamic state because they change more often than the pipeline's static state.

## Compute follows the same ownership model

Compute dispatches reuse `ProgramFactory`, `UniformManager`, `VkTextureManager` and `FrameContext`, but use a compute pipeline and storage-image/storage-buffer transitions rather than a graphics render pass. A compute-to-graphics or graphics-to-compute dependency is handled by the tracked resource layout and command-buffer barriers.
