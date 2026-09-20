## Frame boundary

DirectGLES does not have Vulkan-style command-buffer submission, but GPU work can outlive the return from `Present`. The backend inserts a `GLsync` fence before `eglSwapBuffers`, then polls fences from prior frames to advance a completed-frame serial. Buffer pools and UBO ring stores use that serial to decide when a grown-away allocation is no longer referenced by the GPU.

```text
record GL work
  -> glFenceSync(frame serial)
  -> eglSwapBuffers
  -> poll older fences
  -> completed serial advances
  -> retire UBO stores / trim buffer pools
```

Fence slots are tagged with the GLES context generation. A context loss or explicit context destruction invalidates old syncs and prevents a later context from inheriting stale completion state.

## Context destruction

`DestroyEGLContext` performs backend-specific cleanup before the native context disappears:

- drain or invalidate buffer-pool ownership;
- invalidate scratch-FBO attachments and FBO binding shadows;
- invalidate PACK/UNPACK caches;
- advance the texture context generation.

Wrappers can remain reachable through semantic objects after this point, but their old native IDs are no longer assumed valid. The next context can rematerialize them from semantic state.

## Common failure modes

When debugging DirectGLES, distinguish these cases:

| Symptom | Likely boundary |
| --- | --- |
| Correct object has stale contents | content/mipmap version or upload path |
| Draw uses the wrong vertex stream | VAO buffer/format version or binding shadow |
| Readback returns another attachment | READ FBO/read-buffer synchronization |
| Works until a scratch operation | driver shadow restoration |
| Works for one format but not another | capability probe/canonical format path |
| First draw after context recreation fails | context generation or cache invalidation |

The backend is easiest to reason about by checking both the semantic version and the driver shadow at the point where the native call should have happened.
