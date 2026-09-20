
## Names, identity and lifetime

External indexes are stored in the state managers; native IDs are backend-private. Texture and program objects carry monotonic lifetime IDs, while buffers expose a change serial. These identities prevent a cache keyed by a recycled GL name or heap address from returning a stale native object.

## BufferObject and PipeResource

`BufferObject` stores size, usage, mapping access, immutable-storage flags, mapped range and a `PipeResource`. `PipeResource` supports two byte-authority modes:

```text
shadow mode:
  application write -> CPU vector -> BufferBackendOps.SubData/Respecify

persistent coherent mode:
  backend seeds host-visible mapped GPU memory
  -> PipeResource::AdoptPersistentMap
  -> application reads/writes mapped GPU memory directly
```

`BufferBackendOps` contains `Respecify`, `SubData`, `FlushMappedRange`, `AcquirePersistentMap` and `OnDestroy`. DirectGLES and DirectVulkan install their operation tables during backend initialization. A null table is valid for state-only tests; later lazy resource creation uploads the complete shadow.

## TextureObject

`ITextureObject` is the backend-neutral interface. `TextureObjectBase` stores target, internal format, sampler, swizzles, border colors, base/max level, immutable levels, samples, fixed-sample state, parameter version, content version and lifetime ID. Concrete mipmap/1D/2D/cube/3D/buffer types store target-specific storage.

Content version means image data or defined mip levels changed. Parameter version means state that feeds sampler/view decisions changed. A backend can therefore skip upload while still rebuilding a sampler, or upload new content while reusing sampler state.

## ProgramObject and VertexArrayObject

`ProgramObject` stores attached shader lists, deferred-detach state, link status/info log, uniform locations, block metadata, attribute/output locations and backend hash/version data. `VertexArrayObject` stores resolved `VertexAttribute` records and element-buffer binding. The separate vertex-binding API is resolved into those records before the backend sees it.

## Backend registry

DirectGLES uses `StateBackendObjectRegistry<StateObject, BackendObject>`. Its map is keyed by the state pointer and paired with a weak state reference. `GetOrCreate` creates the wrapper lazily; garbage collection removes wrappers whose semantic state has expired. DirectVulkan uses renderer resource managers instead, because native resources must also obey command-buffer completion and cache dependency rules.
