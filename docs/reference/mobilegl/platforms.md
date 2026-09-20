
| Platform | Frontend / surface path | Backend notes |
| --- | --- | --- |
| Linux | EGL on X11, GLX, and surfaceless EGL paths | DirectGLES or DirectVulkan. |
| Windows | WGL over the common EGL implementation; Win32 WSI | Drop-in `opengl32.dll` path and ANGLE loader support. |
| macOS | CGL and NSOpenGL | DirectVulkan with MoltenVK and Metal surfaces. Use `-XstartOnFirstThread` for JVM/GLFW apps. |
| iOS | iOS build path | DirectVulkan with a Metal-backed surface; pass `MOBILEGL_IOS=ON`. |
| Android | Android plugin EGL integration | `arm64-v8a` is the default ABI; DirectGLES is the default, DirectVulkan is selectable. |

## Windows

The WGL frontend is new in the current development line. It handles context creation, pixel formats, Win32 window plumbing and both backend choices. Validate it with the manual smoke programs under `tools/wgl-smoke`; zero-area helper windows are an explicit test case.

## Android

The plugin exposes a renderer configuration for backend selection and compatibility toggles. The trace APK is a separate profile used for apitrace replay. DirectVulkan uses an `AImageReader`-backed window in the Android test harness because Android ICDs do not generally provide `VK_EXT_headless_surface`.

## Apple

The macOS path is intended to be injected before the host creates its OpenGL context. MoltenVK must be discoverable by the Vulkan loader. The iOS build shares the Vulkan translation path but uses iOS/Metal surface setup and has no desktop test/benchmark targets.
