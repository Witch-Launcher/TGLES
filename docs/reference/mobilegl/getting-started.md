
## Requirements

The project currently requires C++23, CMake and a supported Clang toolchain. Tests and benchmarks are Clang-only; Android disables them. Initialize submodules because glslang, SPIRV-Cross, DiligentCore, VMA and apitrace are part of the build or tools.

```sh
git clone https://github.com/MobileGL-Dev/MobileGL.git
cd MobileGL
git submodule update --init --recursive
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

For a library-only build:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMOBILEGL_BUILD_TEST=OFF \
  -DMOBILEGL_BUILD_BENCHMARK=OFF
cmake --build build --target MobileGL
```

## Build switches

| Option | Default | Purpose |
| --- | --- | --- |
| `MOBILEGL_BUILD_TEST` | `ON` | Build unit and integration tests. |
| `MOBILEGL_BUILD_BENCHMARK` | `ON` | Build benchmarks. |
| `MOBILEGL_FORCE_RELEASE_OPT` | `ON` | Keep release optimization and LTO in Debug builds. |
| `MOBILEGL_ENABLE_TRACY` | `OFF` | Enable Tracy profiling integration. |
| `MOBILEGL_BUILD_TRACE_REPLAY` | `OFF` | Build the desktop apitrace replay runner. |
| `MOBILEGL_IOS` | `OFF` | Build the Apple target as iOS instead of macOS. |

## First run

The default backend is `DirectGLES`. Select Vulkan explicitly when the platform and loader are ready:

```sh
MOBILEGL_BACKEND_TYPE=DirectVulkan ./your-opengl-application
```

On macOS, build with `MOBILEGL_BACKEND_TYPE=DirectVulkan`, set `VK_ICD_FILENAMES` to the MoltenVK ICD JSON, and inject the dylib before GLFW creates its context:

```sh
DYLD_INSERT_LIBRARIES=/absolute/path/to/libMobileGL.dylib \
MOBILEGL_BACKEND_TYPE=DirectVulkan \
VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json \
your-application
```

See [Platforms](platforms.md) for Windows, Android and Apple-specific setup, and [Configuration](configuration.md) for runtime switches.
