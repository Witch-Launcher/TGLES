## Texture identity and resource

`VkTextureManager` keys resources by a texture identity that includes object lifetime, not only the frontend numeric name. This handles deleted-but-attached textures and name reuse without returning an old `VkImage` for a new semantic object. The tracked `TextureResource` contains the image, allocation, format, aspect, mip/layer shape, image views, usage flags, current layout and content/version snapshots.

## `SyncTextureAndGetDescriptor`

The operation is a materialization boundary:

1. Resolve the semantic texture's complete mip shape and upload target.
2. Create or recreate the `VkImage` if format, extent, mip count, samples or required usage changed.
3. Preserve old contents when a compatible image is recreated.
4. Upload dirty mip levels from the frontend shadow.
5. Create sampled, attachment, storage or texel-buffer views as required.
6. Return the tracked resource for descriptor resolution.

Within one `SetupDraw`, `DrawSyncScope` records successfully synchronized texture identities. Repeated calls for layout probes, transitions and descriptor writes return the same resource without repeating full mip completeness and dirty-content scans.

## Layout tracking

The manager tracks the layout associated with the whole image and emits barriers with source/destination stage and access masks. Sampling normally targets `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`; color/depth attachment writes use attachment layouts; storage images use `VK_IMAGE_LAYOUT_GENERAL`.

When a mip level is written as an attachment, the manager handles the other mip ranges as needed so the image's tracked state remains conservative. A later sampling transition uses the tracked previous access/layout rather than assuming `UNDEFINED`.

## Storage-image preparation

An image bound through a GL image unit needs `VK_IMAGE_USAGE_STORAGE_BIT`. The renderer marks such textures before committing the render pass. If the live image predates that mark, the manager reports an upgrade requirement; the renderer flushes or ends the pass, recreates the image with storage usage, copies contents forward, and only then continues recording.

## Format fallback

Vulkan storage and sampled image view formats are checked against the semantic numeric domain. When a native format cannot express the requested GL format, the manager uses a canonical shadow format and resolves compatible sampled/storage views where legal. The same decision affects image usage, view aspect masks and shader descriptor types, so it cannot be postponed until descriptor write time.

## Deferred destruction

Replacing an image or view does not immediately destroy the old handle. The old resource is put on a frame-indexed deferred-release list and is collected after that frame slot's fence is waited. Texture GC also prunes dead semantic aliases, but GPU completion remains the final destruction condition.
