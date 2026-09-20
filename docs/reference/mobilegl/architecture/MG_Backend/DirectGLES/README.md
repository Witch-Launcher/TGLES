DirectGLES is MobileGL's immediate-mode backend. It owns a real GLES context and turns the semantic objects held by `MG_State` into native GLES objects. The important design problem is not issuing `gl*` calls; it is keeping a second, driver-visible state machine consistent with the frontend state while working around the smaller and more irregular GLES feature set.

## Mental model

```text
GLImpl validates and mutates MG_State
        |
        v
DirectGLES callback
        |
        +-- obtain state object -> backend wrapper registry
        +-- compare state versions / cached values
        +-- materialize buffers, textures, FBOs and programs
        +-- repair driver bindings through shadow caches
        v
native GLES calls (usually immediate)
```

The frontend object is authoritative. A `Backend*` wrapper is only a materialized representation of that object in the current GLES context. Most wrappers are created lazily, and synchronization is incremental: a state version is compared with the version last applied by that wrapper, so an unchanged object does not cause a full replay.

## Pages

- [Object ownership and registries](object-registry.md)
- [State synchronization and draw preparation](state-synchronization.md)
- [Textures, framebuffers and readback](textures-fbo-readback.md)
- [Shader translation and program linking](shader-and-essl.md)
- [Frame boundaries and driver lifetime](presentation.md)

## Source map

| Source | Responsibility |
| --- | --- |
| `DirectGLES.cpp` | Native draw, clear, copy, readback and presentation paths |
| `Managers.h/.cpp` | Backend wrappers, versioned synchronization and binding shadows |
| `BackendObject_DirectGLES.*` | Context initialization, capability probes and function table |
| `Utils.cpp` | Pixel-store handling, packed readback and format helpers |

## What “immediate” does and does not mean

DirectGLES generally records no command graph of its own. Once a callback has synchronized the required objects, it calls the GLES driver and returns. This does not make the backend stateless: GLES has mutable bindings, and other MobileGL paths such as scratch FBOs can temporarily change them. The binding shadows and RAII restoration paths are therefore part of correctness, not just an optimization.
