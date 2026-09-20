
The tools are part of the architecture because they exercise the same public boundaries as real applications. They are not backend implementation directories, so their ownership is kept separate from `MG_Backend`.

## Native tests and benchmarks

`MG_Test` covers EGL state, queries, VAOs, programs, shader transpilation, backend loading, pipeline quirks and conversion helpers. Tests can replace entries in `gBackendFunctionsTable` with recording callbacks, isolating GL validation from native driver availability. `MG_Benchmark` measures state/translation paths and backend operations.

## CTS and Piglit

`tools/cts` provides the Android VK-GL-CTS integration and currently targets the `KHR-GL33` workload. `tools/piglit-android` patches Waffle/Piglit so the runner loads `libMobileGL.so`, creates Android-compatible windows, selects DirectGLES/DirectVulkan and compares result output. These are conformance/workload harnesses, not shortcuts around `MG_State`.

## Trace replay

`tools/trace_replay` builds a desktop apitrace runner and an Android trace profile. A trace drives the exported API in temporal order, so it tests lifecycle, shader compilation, resource updates, presentation and frame-to-frame cache behavior. Image comparison and fixture metadata live outside the backend, keeping replay useful for both DirectGLES and DirectVulkan.

## Smoke paths

`tools/wgl-smoke` covers Windows context/bootstrap and zero-area helper-window behavior. The Android plugin and POST screen provide a practical device-level view of selected backend, advertised capabilities and driver assumptions.
