
## Upload

GL calls update semantic storage and pixel-store state. Buffers use `BufferBackendOps` for respecify/subdata/flush or create a resource lazily from the complete shadow. Textures validate client layout, normalize formats and mark content/mipmap versions dirty. DirectGLES uploads through its texture manager; DirectVulkan uses staging/canonical formats and image layout transitions.

## Readback

The backend binds or resolves the requested source, synchronizes PACK PBO/pixel-store state, obtains wide/canonical rows, converts to the requested client `(format, type)`, and applies alignment/row/image skips. DirectGLES restores every temporary FBO/PBO binding through shadows and guards. DirectVulkan waits only where necessary, then writes back PBO/client data after command completion.

## Why state remains involved

The conversion routine alone cannot determine row layout or whether `pixels` is an offset into a PBO. Those facts belong to `RenderState` and buffer bindings. Transfer operations therefore cross `MG_Impl`, `MG_State`, `MG_Util` and the selected backend rather than living in one utility function.
