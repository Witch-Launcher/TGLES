
## Logging

The logging layer initializes before configuration and keeps WARN/ERROR messages visible in normal release builds. This matters for shader compilation failures, unsupported format fallback and backend capability skips, which can otherwise look like silent rendering errors.

## Driver POST

`MG_Util/SelfTest/DriverPost.cpp` probes the actual backend driver, then reports both native facts and what MobileGL advertises to applications. GLES probes cover version/extensions, timer queries, interpolation and format behavior. Vulkan probes cover loader/API/device bring-up, surface extensions, required features and format capabilities.

The Android plugin renders these checks in grouped, expandable rows and includes format capability tables. A WARN can mean an emulated path is correct; FAIL means a required assumption or probe did not hold.

## Debugging by boundary

| Observation | Inspect first |
| --- | --- |
| wrong GL error/binding | `MG_Impl/GLImpl` validator and `MG_State` manager |
| shader compile/link error | `MG_Util/ShaderTranspiler`, backend transform flags, native log |
| wrong GLES readback | PACK/UNPACK shadows, scratch FBO and format processor |
| Vulkan validation error | image layout, descriptor reflection, render-pass/pipeline payload |
| crash after long uptime | `FrameContext`, cache eviction and deferred resource drains |
| resize/minimize failure | host `WindowHandle`, swapchain capabilities and present state |
