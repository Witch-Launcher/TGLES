## The two object layers

Every GL object exists in two layers:

| Layer | Example | Meaning |
| --- | --- | --- |
| Semantic state | `GLState::TextureObject` | The object as required by the GL API, including contents, parameters and relationships |
| Native wrapper | `BackendTextureObject` | The GLES name, native allocation and the versions already applied to it |

`StateBackendObjectRegistry<StateObject, BackendObject>` connects the layers by the address of the semantic object. `GetOrCreate` retains a weak reference to the state object and creates the backend wrapper on demand. When the state object dies, the registry can remove the wrapper without confusing a later object that happens to reuse a native GL name or heap address.

There is one registry for each relevant object family: buffers, VAOs, textures, FBOs, programs, samplers and renderbuffers. The registry is not the GL name table. It is a lifetime bridge from a C++ state object to a context-local representation.

## Lazy materialization

Creation and synchronization are intentionally separate. A frontend `glGen*`-style operation can create a semantic object without immediately allocating a driver object. The first operation that needs the resource calls a helper such as `EnsureBufferResource`, `SyncTextureObjectToBackend` or a wrapper's `SyncToBackend` method.

This has three consequences:

1. An object can be validated and changed while it has no native name.
2. The wrapper can replay the current state rather than replaying the complete history of API calls.
3. A backend context reset can invalidate native names while semantic state remains available for rematerialization.

## Version snapshots

Wrappers store the narrowest snapshot that lets them skip work. A VAO stores one `VertexAttributeVersion` per location and one index-buffer version. A texture stores basic shape/format information plus sampler, texture-parameter, content and mipmap versions. A program stores the frontend link version and all link-derived locations.

The pattern is:

```text
if state.version == wrapper.syncedVersion:
    skip this category
else:
    issue only the native operations for the changed category
    wrapper.syncedVersion = state.version
```

The wrapper updates its snapshot only after the corresponding native operation succeeds or after the resource has been put into a known fallback state. This matters for recoverable failures: falsely marking an unsynchronized attachment as current would make every later draw silently reuse stale native state.

## Context-local identity

Native GLES names are meaningful only in the context that created them. `DestroyEGLContext` invalidates binding and pixel-store shadows, retires buffer pools, and increments the texture context generation. A wrapper destroyed after the context has gone away must not call `glDeleteTextures` on a name that may already belong to a new context.

This is why the wrapper carries context-generation information and why deleting a semantic object is not treated as permission to blindly delete a native name. The semantic lifetime and native-context lifetime are related, but they are not identical.

## Registry garbage collection

Registries keep weak state references and periodically prune expired keys. Native resource pools have their own reclamation rules. A registry sweep only removes the C++ bridge; it does not replace the frame-fence logic used for transient buffer storage or the context-generation checks used for native names.
