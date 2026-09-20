## Render-pass derivation

`VkRenderPassManager::GetOrCreateRenderPass` derives attachment descriptions from the semantic draw FBO and the acquired swapchain image when the default framebuffer is involved. It resolves color/depth/stencil formats, sample counts, load/store behavior and extent, then caches a render-pass/framebuffer entry.

An active pass can continue only when its entry is compatible with the next draw. Otherwise `SetupDraw` ends it and begins a new pass. This boundary is also used for pending clears, sampled feedback and resource upgrades, not only for FBO changes.

## Deferred clears

GL clear calls can target a texture attached to an FBO and then detach it before any draw. `VkClearManager` records the clear semantically instead of forcing an immediate Vulkan command. When that texture is later sampled or used as an attachment, `SetupDraw` materializes the clear in a legal command context, then transitions the image for its next use.

## Present sequence

`VulkanRenderer::Present` performs the WSI and frame-boundary work:

1. Recreate or suspend presentation for a zero-area surface.
2. End the active render pass.
3. Append the swapchain image barrier to `PRESENT_SRC_KHR`.
4. End the command buffer and submit it with the current frame fence.
5. Call `vkQueuePresentKHR`.
6. Treat `VK_SUBOPTIMAL_KHR` as usable and rebuild only after an authoritative surface-size/transform comparison.
7. Advance the frame slot, wait/acquire the next image and reset per-frame allocators after the fence.

This avoids rebuilding every frame on drivers that report suboptimal surfaces routinely, while still responding to real resize and orientation changes.

## Swapchain recreation

Recreation waits for device idle, completes submit serials, invalidates pending timer records, shuts down old render-pass/framebuffer state, creates the new swapchain and semaphores, reinitializes render-pass state, destroys pipelines that reference old passes, and recreates transient buffer arenas. A zero-sized surface returns to a suspended state instead of submitting against an unsignaled acquire semaphore.

## Cache and resource aging

At a real frame boundary, program, pipeline, vertex-input, sampler and render-pass managers age their entries. Entries recently used by a frame remain protected by the in-flight window; old content-addressed entries can be destroyed once the renderer proves no submitted command buffer can reference them. Present-less paths use flush/readback completion drains so offscreen workloads do not grow transient state forever.
