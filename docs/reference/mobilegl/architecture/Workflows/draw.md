
## Common front half

`GLImpl::DrawElements` checks that a current program exists and is linked, checks backend-sensitive primitive restrictions and then calls the function table. The state managers already contain the current VAO, element buffer, framebuffer, textures, samplers and render state.

## DirectGLES

The backend registry obtains wrappers and compares versions. VAO sync selectively applies enable/disable, attribute pointer, divisor and element-buffer calls. Program sync compiles emitted ESSL if the link version changed. Texture and sampler sync uploads changed levels/parameters. FBO sync remaps draw/read buffers and changed attachments. Driver shadows make temporary bindings reversible. The backend finally issues the GLES draw.

## DirectVulkan

`VulkanRenderer::DrawElements` creates a range payload and calls `SetupDraw`. The setup sequence is:

```text
DrawSyncScope + garbage collection
  -> framebuffer validity
  -> ProgramFactory transformed program/reflection
  -> storage-image preparation
  -> sampled texture collection and layout transitions
  -> render-pass entry
  -> vertex-input preflight
  -> PipelineFactory pipeline
  -> begin/continue compatible render pass
  -> UniformManager descriptors and dynamic offsets
  -> vertex/index buffer upload and bind
  -> viewport, blend, stencil, scissor dynamic state
  -> vkCmdDrawIndexed
```

No GPU completion is implied by the return from the GL call. The command belongs to the current `FrameContext` slot and is retired at submission/fence boundaries.
