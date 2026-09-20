
## EGL is the common host path

`EGLImpl` owns the common display/config/surface/context/sync entry points. `EGLState::EGLContext` first records logical objects; the active `BackendObject` then creates the native display/surface/context resources. This split allows X11, Win32, Android and Apple handles to reach the same GL state machine.

`EGLImpl::CreateWindowSurface` performs display/config/native-window checks, creates the logical surface, converts the native handle to `WindowHandle`, and calls `BackendObject::CreateEGLWindowSurface`. If native creation fails it destroys the logical surface and returns `EGL_BAD_NATIVE_WINDOW`.

`EGLImpl::MakeCurrent` is state-first but rollback-safe: it saves the old thread tuple, calls `EGLState::MakeCurrent`, then calls `BackendObject::MakeEGLCurrent`; if the backend rejects the attach, the old tuple is restored and `EGL_BAD_ACCESS` is recorded.

## GLX

`GLXImpl` provides the Linux/X11 entry surface and lookup behavior expected by desktop applications. It does not create a second GL state system. GLX context/display operations are adapted to the common EGL/GL implementation, while the backend sees an X11 `WindowHandle` and owns the native GLES/Vulkan WSI details.

## WGL

`WGLImpl` maps `HDC`/`HGLRC` and Win32 windows to MobileGL EGL objects. Its surface-size synchronization follows window changes and its `MakeCurrent` path calls the common EGL implementation after resolving the WGL context record. The Windows build can export a drop-in `opengl32.dll`; both DirectGLES and DirectVulkan share the same WGL semantic layer.

## CGL and NSOpenGL

The macOS frontends translate Cocoa context calls and interpose the entry points used by applications such as GLFW/LWJGL. A lightweight dyld hook may install dispatch interception, but full MobileGL initialization still begins through `EnsureInitialized`. Native surfaces are represented as Metal-backed backend surfaces for DirectVulkan/MoltenVK.

## `GetProcAddress`

The function resolver is part of the ABI boundary. It must return addresses for the implemented core and extension functions without exposing backend-private methods. Optional backend operations are represented by nullable entries in `GLFunctionsTable`; frontend code defines the GL fallback/error behavior when an entry is unavailable.
