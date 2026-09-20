
## Three format decisions

MobileGL must distinguish:

1. the GL internal format requested by the application;
2. the native image/buffer format the active backend can create and use;
3. the client `(format, type)` layout requested for upload/readback.

`FormatCapabilityCache` reports creation, sampling, filtering, mipmap, attachment, multisample and texture-buffer capability per internal format/target. `TextureFormatProcessor` and backend conversion helpers supply canonical shadow layouts when native support is absent or unsafe.

## Upload

The frontend validates dimensions, target, internal/client format and pixel-store state. `TextureFormatProcessor` can normalize desktop-only channel layouts, packed types, low-bit formats and depth-stencil input. DirectGLES uploads the converted data with GLES format/type combinations; DirectVulkan stages per-aspect data and copies into a compatible `VkImage`.

## Readback

Readback must restore exact GL semantics, not merely return native bytes. The path accounts for PACK alignment, row length, image height, skipped rows/images, PBO bindings, 3D slices/array layers, sRGB encoded values and packed encodings such as 5-6-5, 10-10-10-2, shared exponent and float-packed types.

DirectGLES uses temporary FBO/readback paths and restores driver state. DirectVulkan reads wide canonical rows, decodes the native format, then writes the requested client format/type. This conversion boundary is shared by `ReadPixels`, `GetTexImage` and texture-image queries.

## Utility boundary

Format processors do not choose a backend or mutate GL bindings. They receive structured formats, ranges and rows. The backend owns staging resources and synchronization; `MG_State::RenderState` owns the pixel-store parameters that determine the final client layout.
