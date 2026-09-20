## Synchronization at a draw

The direct draw path is a sequence of targeted synchronizations:

```text
current semantic VAO / program / FBO
  -> materialize buffers needed by the VAO and uniforms
  -> bind and synchronize the draw FBO
  -> synchronize program and link-derived resources
  -> synchronize VAO attributes and element buffer
  -> synchronize textures, samplers and image bindings
  -> apply emulated draw parameters
  -> issue glDraw*
```

The exact entry point varies for indexed, instanced and indirect draws, but the invariant is that the driver sees the required state immediately before the native draw.

## VAO synchronization

`BackendVertexArrayObject::SyncToBackend` first binds the backend VAO. For each attribute location it compares three independent versions:

- `SwitchVersion` controls `glEnableVertexAttribArray` and `glDisableVertexAttribArray`.
- `FormatVersion` controls type, component count, normalization/integer mode, stride, offset and divisor.
- `BufferVersion` controls the source buffer binding.

For a format or buffer change it resolves the semantic buffer to a native buffer, binds it as `GL_ARRAY_BUFFER`, then calls either `glVertexAttribPointer` or `glVertexAttribIPointer`. The divisor is replayed when the format version changes. The element array buffer is tracked separately because it is VAO state, not context-global state.

This split is important for repeated draws that change only one stream: toggling an attribute does not force all pointer descriptions to be resent, and changing a buffer does not require rebuilding the entire VAO.

## Client arrays

Desktop GL permits client-side vertex arrays in compatibility paths; GLES requires a buffer. When an enabled attribute has no semantic buffer, `SyncClientSideAttributesForDrawArrays` computes the requested byte range from `first`, `count`, stride and element size, uploads that range into a per-attribute stream buffer, and changes the pointer to that temporary storage for the draw. The wrapper keeps those temporary buffer IDs and restores the driver-visible state after the operation.

Indexed draws do not use this path because their index fetch and vertex range semantics require a buffer-backed representation. Invalid indirect pointers are also checked against the bound draw-indirect buffer before native dispatch.

## Buffer binding points

Uniform, shader-storage, atomic and transform-feedback binding points are synchronized separately from the ordinary target binding. `SyncBufferBindingPoints` walks only the high-water mark of points touched by the application. Full-object ranges use `glBindBufferBase`; subranges use `glBindBufferRange` with clamped offsets and sizes.

The backend keeps a UBO ring for translated global state and grows/retire stores at frame boundaries. A native buffer can therefore outlive the semantic update that caused it to be replaced; the frame fence determines when the old store can be reclaimed.

## Driver binding shadows

MobileGL cannot rely on `glGetIntegerv` before every bind. It maintains shadows for buffer targets, texture units, samplers, FBO draw/read bindings and pixel-store state. A bind helper compares the requested native ID with the shadow and skips redundant calls. On a cold or invalidated cache, it performs one driver query and pins the result.

Every raw helper that temporarily binds a texture or FBO must save the shadowed binding, perform its work, and restore both the native binding and the shadow. Otherwise the next semantic operation could incorrectly believe that the required object is already bound.

## Why versioning and shadows are both needed

Versions answer “did the semantic object change?” Shadows answer “what does the driver currently have bound?” A temporary readback can invalidate the second answer without changing the first. Conversely, an application can change a frontend attachment while the same FBO remains bound, invalidating the first answer without changing the binding slot. Correct synchronization needs both dimensions.
