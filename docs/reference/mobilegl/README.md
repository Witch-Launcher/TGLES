
**Current development documentation | Source snapshot: 2026-07-29**

MobileGL is an open-source desktop OpenGL implementation and translation layer. It exposes desktop OpenGL/EGL entry points, tracks GL and EGL state, translates shaders and resources, and executes them through an OpenGL ES or Vulkan backend.

This documentation follows the `MobileGL-cpp` development tree. It describes a build-from-source project, not a stable release: the current core version is `26.7.0-dev`, the short-term API target is OpenGL 3.3 Core, and compatibility is still being validated.

## Start here

| Goal | Page |
| --- | --- |
| Build the library or understand supported hosts | [Getting Started](getting-started.md) |
| Understand the call and ownership boundaries | [Architecture](architecture/README.md) |
| Choose DirectGLES or DirectVulkan | [Backends](backends.md) |
| Run MobileGL on Linux, macOS, Windows, Android or iOS | [Platforms](platforms.md) |
| Select a backend or work around a driver issue | [Configuration](configuration.md) |
| Check the implementation surface and known limits | [Capabilities](capabilities.md) |
| Run unit tests, CTS, Piglit, POST or trace replay | [Validation & Diagnostics](validation.md) |
| Read the development history | [Development Updates](updates.md) |

## What MobileGL contains

1. **`MG_Impl`** exposes desktop API and host-context frontends such as EGL, GLX, CGL/NSOpenGL and WGL.
2. **`MG_State`** owns context-local GL/EGL objects, bindings, validation and error semantics.
3. **`MG_Backend`** translates the state and commands to independent DirectGLES and DirectVulkan implementations.
4. **`MG_Util`** provides shader processing, format/readback conversion, logging, loaders and self-tests.
5. **Tools and Android integration** provide driver POST reports, trace replay, Piglit and VK-GL-CTS harnesses.

The normal flow is `application -> host frontend -> MG_State -> selected backend -> GLES/Vulkan driver`. See [Architecture](architecture/README.md) for the detailed lifecycle.

## Project status

The project is still a development build. It includes Windows WGL, macOS CGL/NSOpenGL and Metal-surface paths, iOS build support, persistent mapping, compute and indirect draws, broad format/readback coverage, driver POST checks, Android Piglit/CTS runners, and extensive Vulkan resource lifetime handling.

Those features do not imply complete OpenGL 3.3 conformance on every driver. Use the validation tools and the backend-specific notes before treating a result as portable.

## License

MobileGL is distributed under the [GNU LGPL v3.0](https://github.com/MobileGL-Dev/MobileGL/blob/dev-es/LICENSE). Bundled third-party components retain their upstream licenses.
