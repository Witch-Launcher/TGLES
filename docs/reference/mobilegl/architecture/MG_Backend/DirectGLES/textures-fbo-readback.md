## Texture representation

`BackendTextureObject` maps the frontend texture target and format to a GLES representation. GLES has no native 1D target, so 1D textures are stored as 2D images with height one, and 1D arrays as 2D arrays with height one and layers in depth. The shader translator emits matching sampler types and coordinate padding.

Texture synchronization has separate phases:

1. Determine whether the basic shape, format, mip count or sample count requires native storage recreation.
2. Upload dirty mip levels and array/cube faces.
3. Synchronize the built-in sampler when sampler state changed.
4. Synchronize texture parameters such as swizzle and LOD range.
5. Bind the texture to the requested unit or temporary unit.

Some frontend formats are normalized to a GLES-compatible storage format. The format capability cache and driver probes decide whether a native format is usable; fallback paths preserve the semantic format through conversion on upload or readback.

## Framebuffer synchronization

`BackendFramebufferObject::SyncToBackend` binds the native FBO as either `GL_DRAW_FRAMEBUFFER` or `GL_READ_FRAMEBUFFER`. It then:

- translates frontend draw-buffer tokens into the consecutive attachment form accepted by GLES;
- applies `glDrawBuffers` only to the draw binding;
- applies `glReadBuffer` only to the read binding;
- compares each attachment version;
- synchronizes the attached texture or renderbuffer and attaches it when changed.

The frontend may legally use sparse or reordered color attachments. The wrapper retains the original frontend mapping while building the stricter native draw-buffer array, so shader output and readback decisions still use frontend semantics.

The backend tracks both the FBO binding version and the FBO object's own attachment version. Reattaching a texture or changing draw buffers while the same semantic FBO remains bound must not be skipped merely because the binding slot did not change.

## Read/draw asymmetry

`GL_FRAMEBUFFER` updates both bindings, but `glDrawBuffers` mutates only DRAW and `glReadBuffer` mutates only READ. This is the source of many subtle readback and blit bugs. MobileGL therefore has separate driver shadows and a dedicated `SyncReadBufferToBackend` path for the case where one semantic FBO is simultaneously the draw and read object.

## Scratch FBOs

`ScratchFBOImpl` provides a temporary native framebuffer for operations that cannot be expressed directly against the frontend attachments. Typical uses are:

- reading a depth or stencil attachment with a compatible format;
- copying color/depth data between incompatible storage formats;
- implementing `glBlitFramebuffer` and named framebuffer copies;
- packing pixels for desktop-compatible readback formats.

The scratch path is not an alternate state store. It borrows native texture/renderbuffer objects, changes the driver bindings, performs the operation, then restores the previous draw/read FBO, texture unit and pixel-store state. If the native format cannot be read directly, the backend uses a canonical intermediate and conversion logic in `Utils.cpp`.

## Pixel transfer

Readback honors PACK state, including alignment, row length, image height, skip fields and PBO offsets. Upload honors the corresponding UNPACK state. Packed types, depth/stencil combinations, 3D images and array layers are handled through the pixel-store processor rather than by treating the destination as a flat tightly packed array.

This is why a readback can require more work than one `glReadPixels`: the backend must first make the attachment readable, select the correct read buffer, configure pixel-store state, invoke GLES, and restore all observable driver state.
