
`MG_State` is the semantic core. It stores what the application requested, not what a particular GLES driver or Vulkan ICD happens to accept. Its objects are the source of truth for GL queries, validation and backend synchronization.

## Directory layout

```text
MG_State/
├── EGLState/Core.*
└── GLState/
    ├── Core.*
    ├── ErrorState/
    ├── BufferState/                  BufferObject + PipeResource
    ├── VertexArrayState/
    ├── TextureState/                 textures, mipmaps, units
    ├── SamplerState/
    ├── ProgramState/                 shaders, programs, reflection
    ├── FramebufferState/
    ├── RenderbufferState/
    └── RenderState/                  raster, blend, depth, pixel store
```

## The two roots

`EGLState::EGLContext` tracks displays, configs, surfaces, contexts, syncs and a map of per-thread current tuples. `GLState::GLContext` holds the GL managers and the current program/VAO/framebuffer relationships. The process keeps pointers to these roots during an initialized MobileGL lifetime.

The state roots do not own native GLES/Vulkan handles. A state object can be referenced by another state object, for example a texture held by an FBO attachment or a buffer held by a VAO, without knowing whether its backend payload is a GLES ID, a `VkBuffer`, a staging allocation or a deferred resource.

## State changes and versions

State managers bump focused versions rather than forcing a full backend update. VAO attributes have switch/format/buffer versions; textures separate content and parameter versions; programs have link/backend state versions; framebuffer attachments have per-slot versions; buffers have a monotonic change serial. Backends compare their snapshots with these values at synchronization time.

## Detailed pages

- [EGLState](EGLState.md): displays, current thread state and lifecycle rules.
- [GLState managers](GLState.md): manager graph and validation responsibilities.
- [Objects and resources](objects.md): names, deletion, buffer storage and object identity.
