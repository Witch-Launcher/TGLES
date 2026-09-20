
## Manager graph

```text
GLContext
├── ErrorState
├── BufferState       -> BufferObject / PipeResource
├── VertexArrayState  -> VertexArrayObject -> BufferObject
├── TextureState      -> TextureUnit -> ITextureObject / SamplerObject
├── SamplerState      -> SamplerObject
├── ProgramState      -> ShaderObject / ProgramObject
├── FramebufferState  -> FramebufferObject -> Texture/Renderbuffer attachments
├── RenderbufferState -> RenderbufferObject
└── RenderState       -> viewport, raster, blend, depth, stencil, pixel store
```

`GLContext` exposes narrow accessors used by `GLImpl` and backends. It does not flatten all state into one giant struct; this lets object-specific deletion and versioning stay with the manager that understands the rule.

## Binding relationships

Bindings hold shared pointers to semantic objects. Deleting a name updates the relevant binding slots and dependent relationships according to GL rules. VAO element-buffer state belongs to the VAO; texture units hold target-specific texture objects plus separately bound samplers; framebuffer attachment records retain their texture/renderbuffer object references.

This graph is why native cleanup cannot be attached directly to `glDeleteTextures` or `glDeleteBuffers`. A state object may remain alive through a binding or attachment even after its external name is no longer usable by the application.

## Dynamic backend parameters

Some GL limits depend on the selected driver: max texture units, samples, renderbuffer size, vertex attributes, uniform bindings and format capabilities. `GLImpl` queries `pActiveBackendObject->GetDynamicParameters()` and the format cache while validating. The state manager still owns the validation result/error; the backend only supplies capability facts.

## Error state

`ErrorState` has GL and non-GL queues. GL entry points record `InvalidEnum`, `InvalidValue`, `InvalidOperation`, `InvalidFramebufferOperation` and related errors with source/function information. Backend failures are logged and converted at the API boundary rather than silently becoming a native driver error with incompatible semantics.
