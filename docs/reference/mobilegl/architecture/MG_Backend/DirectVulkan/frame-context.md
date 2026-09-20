## Frame slots

`FrameContext` owns a ring of frames-in-flight. A slot contains a command buffer, an image-available semaphore, an in-flight fence, recording flags, submit serials and retired command buffers. A slot is reusable only after its fence proves that all submissions associated with it have completed.

```text
wait current slot fence
  -> free retired command buffers for completed submits
  -> acquire swapchain image
  -> reset slot fence
  -> record commands
  -> close command buffer
  -> submit with slot fence
  -> present
  -> advance to next slot
```

`VK_SUBOPTIMAL_KHR` is treated as a successful acquire: an image and semaphore signal exist, so the slot's consumed flag and fence must be reset normally. Only a genuine acquire failure skips that bookkeeping.

## Recording and flushes

`BeginCommandRecording` resets and begins the slot command buffer. `EndCommandRecording` closes it and marks it recorded. A mid-frame flush cannot reset a command buffer that may already be executing. Instead, DirectVulkan retires the current command buffer and obtains a fresh one; the old command buffer is freed only after the associated submit serial is known complete.

This is used by readbacks, blocking sync operations, resource upgrades and other paths that need commands to be submitted before the current logical frame reaches `Present`.

## Present barrier

`TransitionToPresent` appends an image memory barrier to the current open recording. It must happen before closing the command buffer. A frame that rendered only to offscreen FBOs may never have opened a default-framebuffer render pass, so relying on a render-pass final layout would leave the swapchain image in its acquire layout.

## Completion is a proof

The renderer does not reclaim a buffer, image view, descriptor pool slice or retired command buffer merely because the frontend no longer references it. It waits for a fence or `vkDeviceWaitIdle`, advances the completed submit serial, then lets each manager release resources whose last-use serial is below that floor.

This is also why cache eviction is tied to frame boundaries and in-flight limits. A content-addressed pipeline that is old in the cache is safe to destroy only when its last possible command-buffer reference has completed.
