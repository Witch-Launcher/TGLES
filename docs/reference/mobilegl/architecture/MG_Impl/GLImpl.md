
## The GL context facade

`GLImpl` is not itself the state store. It is the façade that turns C/OpenGL arguments into operations on `MG_State::GLState::GLContext`. `GLContext` aggregates managers for buffers, VAOs, textures, samplers, programs, framebuffers, renderbuffers, render state and errors.

Each API family follows this shape:

```text
GL entry point
  -> current GLContext
  -> argument/relationship validation
  -> state manager mutation
  -> optional immediate BufferBackendOps operation
  -> backend table operation for draw/copy/readback/query
```

## Errors are semantic

`ErrorState` stores GL errors separately from non-GL diagnostic errors. Validators create `GenericErrorInfo` containing the implementation subsystem, function and reason, then record the corresponding GL error. A bad state is rejected before the native backend sees it; this prevents GLES/Vulkan acceptance rules from leaking into desktop GL behavior.

## Object APIs

### Buffers

`GL_Buffer` validates target and range, calls `BufferObject::Respecify`, `UploadSubData`, mapping or copy operations, and updates binding slots. Buffer storage can notify the active backend immediately through `BufferBackendOps`, but the backend resource itself can still be created lazily later.

### Textures and samplers

`GL_Texture` validates target-specific dimensions, internal/client format, mip level and pixel-store state. `TextureState` stores per-unit target bindings and per-target default objects. Sampler state is separate from image content, matching desktop GL's texture/sampler split.

### Programs

`GL_Program` owns shader attach/detach/link and uniform/block API behavior. `ProgramObject` keeps GL-visible link status and reflection metadata. Native ESSL/Vulkan compilation may be lazy and is not equivalent to the GL link operation.

### Framebuffers

`GL_Framebuffer` updates attachment records and draw/read buffer state. Backend format capability queries are used for validation, but native attachment views/FBOs are materialized by the selected backend only when an operation needs them.

## Draw dispatch

`GL_Drawing.cpp` performs common execution checks, then uses `gBackendFunctionsTable.GL`. It supports arrays, indexed, instanced, base-vertex, multi-draw and indirect families. Payload construction is deliberately thin; state-dependent preparation happens in DirectGLES managers or DirectVulkan's `VulkanRenderer::SetupDraw`.
