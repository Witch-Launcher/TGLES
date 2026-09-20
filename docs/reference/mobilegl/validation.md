
## Unit and integration tests

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure
```

The tree contains tests for EGL state, queries, VAOs, programs, shader transpilation, backend loading, pipeline quirks, readback conversion and more. Android disables the C++ test/benchmark targets.

## Driver POST

The self-test probes the active GLES or Vulkan driver and then reports the MobileGL-facing identity, version, extensions and format capabilities. It checks loader/device bring-up, required surface support, timer queries, shader behavior, interpolation, readback and other assumptions. A warning can mean a tested emulation path is active; a failure means the backend assumption did not hold.

## VK-GL-CTS and Piglit on Android

`tools/cts` provides a standalone VK-GL-CTS runner and currently measures the `KHR-GL33` target on device. `tools/piglit-android` patches Waffle/Piglit for Android, drives `libMobileGL.so` directly, supports `AImageReader` Vulkan windows and compares result output. These harnesses are the right place to verify a suspected conformance gap instead of relying on a single application.

## Trace replay

The trace runner supports desktop and Android apitrace workflows. It is used for Minecraft startup, shader-pack, in-world, indirect and transparency fixtures, with DirectGLES/DirectVulkan selection and image comparison. Build it with:

```sh
cmake -S . -B build-trace -G Ninja \
  -DMOBILEGL_BUILD_TEST=ON \
  -DMOBILEGL_BUILD_TRACE_REPLAY=ON
cmake --build build-trace --target MobileGL mobilegl_trace_replay
```

## Reading failures

First record backend, platform, driver/ICD, surface size and the POST output. Then reduce to a unit/CTS/Piglit case or replay trace. DirectGLES logs often point to shader compilation, unsupported ESSL forms or driver-state restoration; DirectVulkan logs often point to device features, descriptor/pipeline creation, image layout, swapchain or deferred lifetime handling.
