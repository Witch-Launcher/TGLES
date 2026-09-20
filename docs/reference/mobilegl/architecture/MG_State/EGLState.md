
## Stored logical objects

`EGLState::EGLContext` stores maps for displays, configs, surfaces, contexts and sync objects. It also stores `ThreadCurrentState` records keyed by `std::thread::id`, containing the current display, draw surface, read surface and context for each thread.

The class exposes validation methods such as `ValidateDisplay`, `ValidateConfigOnDisplay`, `ValidateContextOnDisplay`, `ValidateSurfaceOnDisplay`, and `IsDisplayInitialized`. These methods are called before backend operations so EGL errors originate from the logical object graph.

## Currentness

`MakeCurrent(display, draw, read, context)` validates the tuple and updates the calling thread's `ThreadCurrentState`. It also handles the release tuple of `EGL_NO_DISPLAY/EGL_NO_SURFACE/EGL_NO_CONTEXT`. `EGLImpl` then attaches or releases the corresponding native backend context.

The two steps are intentionally separate. If native attach fails, `EGLImpl` calls `MakeCurrent` with the saved old tuple. The logical and native current contexts therefore move together or roll back together.

## Display termination

`TerminateDisplay` removes one initialized display and its logical resources. `HasAnyInitializedDisplay` and `HasAnyCurrentContext` provide the whole-library idle test used by `EGLImpl::Terminate`. Only when both are false may `MobileGL::Destroy` reset the backend and state roots.

## Backend relationship

EGLState stores the abstract handles returned to the application. The backend stores native surface/device state separately. A logical surface can be pending destruction while still current; `BackendObject::ReleaseEGLSurface` defers native release in that case and finalizes it when the current binding is released.
