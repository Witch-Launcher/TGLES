
## Present

`eglSwapBuffers` validates the logical surface and calls the table's `Present` callback. DirectVulkan acquires an image through the current `FrameContext` slot, handles zero-area suspension and real surface-capability changes, transitions the image to `PRESENT_SRC_KHR`, submits/presents, then runs frame-boundary resource/cache drains. DirectGLES delegates to its EGL/GLES presentation path while preserving binding shadows.

`VK_SUBOPTIMAL_KHR` is treated as a successful acquire because an image and semaphore signal still exist. A rebuild is scheduled only when surface capabilities no longer match the live swapchain, avoiding per-frame rebuilds and pipeline churn.

## Terminate

`eglTerminate` removes the logical display and releases backend EGL resources. If no initialized display and no current context remain, `MobileGL::Destroy` drains live GL sync handles while the backend table is valid, resets backend/state/helper singletons and clears the table. A later EGL call can initialize a fresh instance.

Vulkan native objects are destroyed only after the relevant frame/fence completion rules. GLES native IDs are deleted after wrapper/shadow/scratch references are invalidated. This is why teardown is owned by both the host lifecycle and backend lifetime managers.
