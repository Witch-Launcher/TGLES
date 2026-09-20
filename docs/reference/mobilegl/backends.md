
## DirectGLES

DirectGLES translates desktop GL operations to OpenGL ES. It supports system GLES and an optional ANGLE loader path (`MOBILEGL_USE_ANGLE=1`). Recent work made the backend substantially more defensive around driver state:

- shadow-backed RAII guards for FBO, PACK/UNPACK PBO and pixel-store state;
- scratch-FBO cleanup across color/depth aspect changes;
- packed pixel readback, 3D image layout and legacy format handling;
- compute, persistent mapping, multisample textures, indirect draws and dual-source blending;
- shader passes for `noperspective`, robust access, Qualcomm clip-distance/compiler issues and 1D texture emulation.

DirectGLES is the default backend and is usually the first path to try on Android and GLES-capable desktop systems.

## DirectVulkan

DirectVulkan translates the same state into Vulkan 1.1-era concepts. It includes:

- dynamic Vulkan loading, physical-device selection, WSI and swapchain management;
- per-frame command/fence resources, transient buffer arenas and deferred destruction;
- program and pipeline factories, reflection-driven descriptors and UBO arrays;
- render passes, framebuffer attachments, MSAA, mipmaps, depth/stencil aspects and readback;
- compute, indirect/draw-parameter support, timer queries and robust buffer access;
- desktop WSI, Android surfaces and macOS Metal/MoltenVK surfaces.

The backend now suspends presentation for zero-area windows, follows real surface resizes without rebuilding on every `VK_SUBOPTIMAL_KHR`, and ages caches/resources so long-running workloads remain bounded. Adreno-specific paths include explicit-LOD single-level sampling, image mutability preservation, precision relaxation and ESSL/SPIR-V workarounds.

## Choosing a backend

```sh
MOBILEGL_BACKEND_TYPE=DirectGLES   # default
MOBILEGL_BACKEND_TYPE=DirectVulkan
```

Backend selection is read during initialization. Reproduce a problem on both backends when possible: a failure in DirectGLES may be an ESSL compiler issue, while a DirectVulkan failure may involve resource transitions, descriptors, pipeline state or the ICD.
