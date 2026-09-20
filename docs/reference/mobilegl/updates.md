This page records the development history month by month, based on the MobileGL source repository's commit history. The entries summarize the main engineering themes for each month; the source history remains the authoritative record for individual fixes and experiments.

## How to read workload

Each month has a relative workload label. It estimates engineering scope rather than person-days: how many architectural layers changed, how much cross-module integration was required, and whether the work affected multiple platforms or the test system. A high commit count alone does not make a month large; a small number of foundational commits can still have a large impact.

| Label | Meaning |
| --- | --- |
| **Small** | Focused fixes, maintenance or one subsystem |
| **Medium** | Several related changes within one or two subsystems |
| **Large** | A substantial subsystem, cross-module integration or platform expansion |
| **Very large** | Multiple backend/architecture areas plus broad validation or delivery work |

## Workload at a glance

| Month | Commits | Workload | Main scope |
| --- | ---: | --- | --- |
| 2025-03 | 4 | **Medium** | Project and EGL foundation |
| 2025-04 | 24 | **Large** | GL state and first GLES backend |
| 2025-05 | 76 | **Very large** | Core objects and shader translation |
| 2025-06 | 120 | **Large** | DiligentEngine rendering path |
| 2025-07 | 97 | **Medium** | State breadth and build infrastructure |
| 2025-08 | 153 | **Small** | Portability and CI maintenance |
| 2025-09 | 9 | **Medium** | Render state and OpenGL target |
| 2025-10 | 66 | **Large** | Framebuffer model and DirectGLES integration |
| 2025-11 | 171 | **Large** | Texture parameters and draw entry points |
| 2025-12 | 55 | **Medium** | Resource cleanup and versioning |
| 2026-01 | 96 | **Large** | DirectVulkan bring-up |
| 2026-02 | 293 | **Very large** | Vulkan setup and versioned synchronization |
| 2026-03 | 65 | **Large** | Vulkan resources and descriptor reflection |
| 2026-04 | 6 | **Medium** | Reflection and pipeline correctness |
| 2026-05 | 45 | **Large** | Vulkan textures and Android delivery |
| 2026-06 | 181 | **Very large** | Compute, hosts and application workflows |
| 2026-07 | 373 | **Very large** | Compatibility, testing and lifetime hardening |

## March 2025 - project foundation

**Workload: Medium | 4 commits** - project bootstrap and the first API-to-EGL boundary.

MobileGL was established with the initial project structure, logging, documentation and the first EGL emulation work. `glClear` and `glClearColor` were brought into the emulation path, creating the first usable boundary between the exported GL API and the internal implementation.

## April 2025 - GL state and the first GLES backend

**Workload: Large | 24 commits** - global state, texture state, error handling and a backend were established together.

The project added global OpenGL state, texture state and error tracking, then introduced the GLES backend. Build structure and platform-facing EGL files were reorganized, basic getters and enum conversion were improved, and the first drawing tests began exercising the backend rather than only stubs.

## May 2025 - object state and shader translation

**Workload: Very large | 76 commits** - the core buffer/VAO/program/FBO object graph and shader translation path were introduced.

Buffers, vertex arrays, programs, uniforms and framebuffers became explicit state objects. The project added automatic uniform management, `glBlitFramebuffer`, SPIR-V-to-ESSL conversion, buffer/texture dirty tracking and early draw synchronization. This month established the state-object model that later backends would consume.

## June 2025 - DiligentEngine rendering path

**Workload: Large | 120 commits** - a complete rendering path crossed resources, shaders, pipelines, framebuffers and platform initialization.

The DiligentEngine path grew from initialization into actual rendering: textures, framebuffers, render targets, shader resources, pipelines and dynamic buffers were connected to draw calls. Buffer naming and resource binding bugs were fixed while the initialization and teardown code was made more portable across platforms.

## July 2025 - state breadth and build infrastructure

**Workload: Medium | 97 commits** - state coverage, shader utilities, tests and build workflows expanded in parallel.

Vertex-array state, integer attributes, UBO generation and SPIRV-Cross integration were expanded. Tests and benchmarks became more systematic, CMake and CI were hardened for Windows and macOS, and the project continued separating frontend state from backend implementation details.

## August 2025 - portability and workflow hardening

**Workload: Small | 153 commits** - mostly focused build, CI and symbol-loading corrections.

This month focused on build and workflow reliability: dynamic symbol lookup, MSVC exception settings, position-independent code, external shader-tool sources and CI execution paths were corrected. The shader/program utilities also continued to mature around uniform and UBO handling.

## September 2025 - render state and OpenGL target

**Workload: Medium | 9 commits** - render-state coverage and backend target changes touched several shared utility and state modules.

`RenderState` and the buffer benchmark were added, vector utilities were strengthened, and SPIRV-Cross was updated. The backend target was raised to OpenGL 3.3, while texture mipmaps were made dirty by default so newly created storage could not be mistaken for initialized content.

## October 2025 - framebuffer model and DirectGLES integration

**Workload: Large | 66 commits** - framebuffer semantics, synchronization, DirectGLES selection and several draw-state paths converged.

Framebuffer objects, attachments and framebuffer enum conversion became first-class state. Default VAO and texture-name-zero behavior were corrected, essential framebuffer functions and synchronization objects were implemented, and DirectGLES became the selected backend path. Scissor state, mapped-buffer ranges, sampler parameters and framebuffer blits also moved closer to complete behavior.

## November 2025 - complete texture parameters and draw entry points

**Workload: Large | 171 commits** - texture semantics, getters, draw entry points, pixel storage and DirectGLES synchronization expanded together.

Texture parameter tracking, swizzles, pixel-store behavior and additional internal formats were implemented across the frontend and DirectGLES. `glDrawArrays`, scissor handling, getter coverage and UBO alignment queries were added, while shader compilation and format tests exposed more of the real compatibility surface.

## December 2025 - backend cleanup and calendar versioning

**Workload: Medium | 55 commits** - resource ownership was refactored and DirectGLES gained important draw/resource coverage.

The project adopted calendar versioning and introduced a clearer public project description. Texture objects and mipmap storage were decoupled, DirectGLES gained more draw operations, instancing divisors and texture-buffer synchronization, and renderbuffer backend caching began. This was the transition from a growing prototype to a more explicit resource architecture.

## January 2026 - DirectVulkan bring-up

**Workload: Large | 96 commits** - a second backend crossed from setup code into a basic renderer and platform-integrated execution path.

Version `26.01` and a more reproducible build configuration were introduced. DirectVulkan moved from an experimental backend into a basic renderer with EGL integration, device/surface setup and initial CI coverage. Android-specific Vulkan surface handling and Linux compilation paths were separated so the renderer could be used through an abstracted host layer.

## February 2026 - Vulkan render setup and versioned synchronization

**Workload: Very large | 293 commits** - Vulkan render-pass/resource infrastructure and DirectGLES's versioned synchronization model were developed in the same period.

DirectVulkan developed its render-pass, swapchain, texture and clear managers, including mipmap completeness checks and explicit image-layout transitions. The backend began resolving default-framebuffer attachments, scissor and pipeline state through Vulkan-native objects. In parallel, DirectGLES replaced broad dirty-state replay with resource versions and targeted synchronization for buffers, textures, samplers and VAOs, substantially reducing repeated work.

## March 2026 - Vulkan resources and descriptor reflection

**Workload: Large | 65 commits** - buffer residency, transient arenas, descriptor reflection and index-format support formed a new resource layer.

The Vulkan renderer gained the `VkBufferManager`, unified transient buffer arenas, resident/transient upload heuristics, buffer slices and uint8 index-buffer support. Uniform reflection moved into `ProgramFactory`, backed by SPIRV-Reflect, so descriptor layouts and shader resources could be derived from the transformed SPIR-V rather than maintained as a separate approximation.

## April 2026 - reflection performance and pipeline correctness

**Workload: Medium | 6 commits** - the month focused on tightening and accelerating the Vulkan reflection/pipeline contract.

SPIRV-Reflect was used to avoid unnecessary full AST parsing during shader reflection. Program reflection was refactored around the Vulkan backend's actual descriptor contract, vertex input formats were tightened to avoid invalid scaled formats, and proxy texture handling and the DirectVulkan integration branch were stabilized.

## May 2026 - Vulkan texture operations and Android delivery

**Workload: Large | 45 commits** - texture capabilities, blits, platform packaging and the Android delivery pipeline changed together.

DirectVulkan added texture copy paths, 3D texture handling, sampler and depth-sampler fixes, more flexible sampler descriptor resolution and the initial mipmap-generation work. RGB attachment fallbacks and rotation-aware blits addressed device-specific limitations. The Android plugin became a more complete delivery target with signed builds, Espryt/Magma variants and DirectVulkan as the default CI backend.

## June 2026 - compute, desktop hosts and application workflows

**Workload: Very large | 181 commits** - both backends, several desktop/mobile hosts and trace-based application validation expanded at once.

Both backends gained compute-shader support, and DirectVulkan added min/max LOD behavior and broader blend/render-state handling. Linux X11 + EGL, macOS CGL/NSOpenGL, iOS builds and Metal-backed surfaces expanded the host matrix. Client-side vertex buffers, legacy GLSL syntax handling, packed upload normalization and surface-resize behavior were fixed, while trace replay added normal-world and per-mod application fixtures.

## July 2026 - compatibility, testing and lifetime hardening

**Workload: Very large | 373 commits** - broad backend compatibility work was paired with cache/lifetime redesign, multiple test harnesses and real application fixtures.

The current month concentrated on making both backends survive real applications and adversarial tests. DirectGLES gained shadow-backed RAII for FBO, pixel-store and PBO state, packed and layered readback, noperspective and Qualcomm clip-distance workarounds, format fallbacks and safer context teardown. DirectVulkan added legacy-format conversion, general readback, UBO descriptor arrays, color renderbuffers, explicit-LOD sampling, surface-resize-aware presentation and Adreno-specific workarounds. Both backends received bounded cache eviction, deferred destruction and present-less drain paths. Android Piglit, VK-GL-CTS, WGL smoke tests, macOS fixes and Minecraft 26.2/26.3 trace fixtures made regression coverage part of the development loop.

## Reading the history

The month labels describe when work was committed, not a release boundary. Some features span several months and some commits are experiments or reversions. For implementation details, use the architecture pages alongside the current `MobileGL-cpp` source and the test/workflow documentation.
