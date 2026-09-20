
## Why one GL call can outlive its object

OpenGL returns from a draw before the GPU necessarily finishes it. Vulkan makes this explicit, but DirectGLES also has driver work and temporary resources whose use extends past a wrapper's semantic mutation. Therefore state deletion, backend cache eviction and native destruction are separate events.

## `FrameContext`

Each frames-in-flight slot contains a command buffer, image-available semaphore, in-flight fence, recording flags, acquired-image bookkeeping and a list of retired command buffers tagged by submit index.

```text
BeginCommandRecording
  -> record commands
EndCommandRecording
  -> submit with slot fence
AdvanceToNext
  -> next slot only after its fence is waited
WaitAndAcquireNextImage
  -> wait fence
  -> free retired command buffers for completed submits
  -> acquire next swapchain image
```

A mid-frame flush cannot reset the current command buffer because submitted commands may still execute. `RetireCurrentCommandBuffer` installs a fresh command buffer, and completion polling/fence waits later free the retired one.

## Deferred resources and cache eviction

Program modules/layouts, graphics/compute pipelines, descriptor sets, vertex-input state, samplers, render passes and texture/renderbuffer resources are cached or deferred independently. Age-based sweeps run at frame boundaries; active entries and entries referenced by in-flight submissions are protected. Render-pass eviction notifies pipeline caches, and program/layout eviction purges dependent descriptor/pipeline entries.

Present-less workloads still need retirement. Readback waits, blocking sync waits, suspended presentation and flush completion polls call the same completion/drain paths when all submissions are proven complete. This prevents offscreen or minimized applications from accumulating transient allocations indefinitely.

## DirectGLES contrast

DirectGLES uses immediate driver deletion but still has semantic wrapper registries and scratch resources. It invalidates binding shadows and scratch FBO attachments when native texture/buffer IDs are deleted. Its safety problem is stale mutable driver state; DirectVulkan's safety problem is in-flight object lifetime. The common `MG_State` contract supports both without conflating them.
