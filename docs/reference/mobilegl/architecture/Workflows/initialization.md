
```text
first exported EGL/WGL/CGL call
  -> MobileGL::EnsureInitialized
  -> ConfigLoader::Init
  -> MG_State::Init
  -> MG_Backend::Init
      -> construct selected BackendObject
      -> backend Initialize()
      -> install GlobalBackendFunctionsTable
  -> MG_Impl::Init
  -> glslang::InitializeProcess
```

`eglGetDisplay` creates a logical display identity. `eglInitialize` first marks that display initialized in `EGLState`, then calls the backend's `InitializeEGLDisplay`. Config selection stays in state; native device/display setup stays in the backend.

`eglCreateWindowSurface` validates display/config/window, stores the logical surface, converts the platform handle into `WindowHandle`, and asks the backend to create native surface resources. On failure it rolls back the logical object.

`eglCreateContext` creates a logical context. `eglMakeCurrent` records the per-thread tuple, attaches the backend native context, and restores the old tuple if native attachment fails. This gives every host frontend one consistent current-context invariant.
