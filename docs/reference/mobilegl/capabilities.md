
## API target

The short-term target is desktop OpenGL 3.3 Core. The advertised surface is assembled from the active backend and its detected capabilities; it is not a promise that every extension behaves identically across drivers.

## Implemented areas

- GL/EGL state tracking with stricter object, profile, framebuffer and texture validation.
- Persistent buffer mapping, client-side buffers, multisample textures, compute shaders, indirect draws, primitive restart and dual-source blending.
- `glMultiDrawArrays`, base-vertex draws, `glGetBufferSubData`, color masks, polygon mode and broader framebuffer attachment queries.
- Desktop single-channel and packed client formats, depth-stencil uploads, 3D/array readback, sRGB readback and legacy low-bit formats.
- Default texture objects per target, texture buffers, texture mipmaps and more complete sampler/LOD behavior.
- Shader version and `#line` preservation, comment-safe preprocessing, noperspective handling, robustness passes and backend-specific compiler workarounds.
- DirectVulkan UBO arrays, renderbuffer color attachments, MSAA/cubemap/3D attachment targets, cache eviction and deferred resource destruction.

## Still treat as development work

Coverage depends on backend, driver, surface type and application usage. In particular, Vulkan device features/extensions, GLES compiler behavior, WSI resize/minimize behavior and less common format combinations require validation on the target machine. The authoritative practical answer is the result of the relevant unit test, CTS/Piglit case, POST probe or trace replay.

## Capability reporting

The Android POST screen now groups checks and includes format capability tables. It reports both the native driver facts and the strings/features MobileGL advertises to applications, which makes a mismatch easier to diagnose than reading only `glGetString` output.
