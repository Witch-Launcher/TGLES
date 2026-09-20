# Long plan: MobileGL (macOS) + DirectGLES render trial

Status: RESEARCHED Sep 2026, Phase 1 (TGL-side 3D proof) DONE.
Evidence below is verified on this machine, not guessed.

## 0. What already works (Phase 1, done)

`apps/tgl_cube_trial` (Darwin-only `tgl_cube_trial` target): 36-vertex
colored cube + perspective/rotation MVP through GlesContext::RenderFrame +
Apple bridge + headless CAMetalLayer present. 8 frames to
`build/cube_frameN.ppm`, rotation proven by checksums + cycling face
colors. Run: `cmake --build build --target tgl_cube_trial && ./build/tgl_cube_trial`
(CWD must be `build/`). CPU math is unit-tested in
`tests/test_cube_model.cpp`. Known simplification: no depth attachment yet,
so overlapping faces resolve by draw order.

## 1. Research findings (blockers for MobileGL-driven rendering)

1. **Stock MobileGL cannot DirectGLES on macOS.** Loader
   (`MG_Util/BackendLoaders/OpenGL/Loader.cpp` OpenLib/ProcAddress):
   `#elif !defined(__APPLE__) || defined(MOBILEGL_IOS)` — on macOS non-iOS
   both compile to no-ops returning nullptr, so `AcquireEGLFunctions`
   fatals and the whole GLES table stays null. macOS MobileGL is
   DirectVulkan+MoltenVK by design (`platforms.md`). DirectGLES hosts are
   Linux/X11, Android, Windows/ANGLE, iOS/tinygl4angle.
2. **Host contract is dlopen + procaddr, not linkage.** DirectGLES takes
   `m_EGLFunctions.eglGetProcAddress` from the dlopened `libEGL.so.1`
   (`BackendObject_DirectGLES.cpp:846`) and resolves 369 required GLES
   entry points through it (`AcquireGLESFunctions`, Loader.cpp:130).
   TGL has zero C exports today (`host.c-abi` is NotRequired at API level,
   but a loadable image is still required for deployment).
3. **MobileGL source needs submodules + full tree.** Verified 2026-09-20:
   9 submodules shallow-clone fine (glslang, SPIRV-Cross, Vulkan-Headers,
   VMA, Vulkan-Utility-Libraries, SPIRV-Reflect, xxHash, asio, ska).
   Configure blockers found: (a) `/tmp/mobilegl` here is a SPARSE checkout
   (DirectGLES+EGLImpl+EGLState+BackendLoaders+include only), so
   `MG_Util/Miscellany/MGGitHash.h.in` is absent — needs full checkout;
   (b) glslang `ENABLE_OPT` needs SPIRV-Tools or `-DENABLE_OPT=0`.
   Full checkout was ABORTED deliberately: partial-clone + missing
   `git-lfs` + remote hangups make it fragile, and the tree was restored
   to its sparse state afterwards (it is shared evidence for docs).
4. **Vulkan SDK exists here** (`/usr/local/lib/libvulkan.dylib` 1.4.357,
   found by MobileGL configure), so the DirectVulkan macOS path is
   plausible later; not needed for the DirectGLES trial.

## 2. Phases

- **Phase A — TGL host EGL dylib (C ABI).** New `tgles_host_egl`
  shared target exporting the 46 EGL entry points + `eglGetProcAddress`
  resolving 358 core + `glBufferStorageEXT`, dispatching to process-wide
  EglState/GlesContext singletons (thread-local current context, mirroring
  MobileGL `ThreadCurrentState`). Pure expansion of already-tested
  managers; test with `dlopen` + procaddr coverage tests.
- **Phase B — macOS loader hook.** Upstream has no macOS host-EGL path;
  options in order of preference: (1) `MOBILEGL_MACOS_HOST_EGL` env hook
  resolving a user-supplied dylib path in `OpenLib` (tiny upstreamable
  patch); (2) test-only harness driving DirectGLES tables with TGL
  addresses directly. Then: MobileGL desktop-GL triangle → DirectGLES →
  TGL dylib → Metal, pixels asserted like `tgl_cube_trial`.
- **Phase C — full MobileGL build.** Full (non-sparse) checkout with LFS,
  `-DENABLE_OPT=0`, tests/benchmarks OFF; validates (1) macOS
  DirectVulkan still builds beside the hook, (2) DirectGLES unit tests
  pass against the TGL dylib.

## 3. Non-goals / honest limits

- Depth attachment in the bridge is DONE (NEVER..ALWAYS funcs + write mask +
  clear value, Depth32Float target, device-verified overlap test); stencil
  testing and MRT targets ride with FBO-attachment wiring.
- MobileGL EGLImages/pixmaps on macOS stay out of scope until Phase B.
- JVM/LWJGL launcher stack is a separate epic (see minecraft.md).
