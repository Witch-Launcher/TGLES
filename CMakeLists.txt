cmake_minimum_required(VERSION 3.20)

# TGL — an OpenGL ES 3.2 implementation that executes on Metal.
# Layout: src/<layer>/ mirrors include/tgles/<layer>/ — base, state, pipeline,
# gpu, egl, facade, host, support. See docs/architecture.md.
project(TGLES VERSION 0.2.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Official specifications + Khronos headers used as ground truth by tests.
set(TGLES_REFERENCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/docs/reference")

# File name (without suffix) of the host EGL/GLES shared library. The built
# file is libtgles.dylib; hosts that dlopen by name keep working through
# their own lookup (VK-GL-CTS surfaceless dlopens "libEGL.so", wired via a
# symlink in tools/cts/build_es32.sh; MobileGL on iOS dlopens
# "libtinygl4angle.dylib").
set(TGLES_HOST_LIBRARY_NAME "libtgles"
    CACHE STRING "Host EGL/GLES shared library file name (no suffix)")

# ---------------------------------------------------------------------------
# Knowledge tables: spec ground truth, verified by tests against docs/reference.
# ---------------------------------------------------------------------------
add_library(tgles_spec
  src/base/spec.cpp
  src/gpu/metal_mapping.cpp
  src/gpu/msl_translation.cpp
  src/gpu/metal_translate.cpp
  src/gpu/glsl_to_msl.cpp
)
target_include_directories(tgles_spec PUBLIC include)
target_compile_definitions(tgles_spec PUBLIC
  TGLES_REFERENCE_DIR="${TGLES_REFERENCE_DIR}"
)
if(APPLE)
  target_compile_options(tgles_spec PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Portable ES 3.2 runtime core. Pure C++17 with no platform headers, so the
# same translation units cross-compile to iphoneos / iphonesimulator as-is.
add_library(tgles_core
  # base
  src/base/error.cpp
  # state (GLES objects + server state)
  src/state/context.cpp
  src/state/buffer.cpp
  src/state/vertex_array.cpp
  src/state/shader.cpp
  src/state/program.cpp
  src/state/texture.cpp
  src/state/sampler.cpp
  src/state/framebuffer.cpp
  src/state/image_units.cpp
  src/state/pixels.cpp
  src/state/transform_feedback.cpp
  src/state/tessellation.cpp
  src/state/geometry.cpp
  src/state/compute.cpp
  src/state/sync.cpp
  src/state/query.cpp
  src/state/debug.cpp
  # pipeline (draw commands + rasterization state)
  src/pipeline/draw.cpp
  src/pipeline/raster.cpp
  # gpu (pure C++ planning + bridge contract; MTL calls live in the .mm)
  src/gpu/backend.cpp
  src/gpu/metal_bridge.cpp
  # egl
  src/egl/egl.cpp
  # facade
  src/facade/gles.cpp
  # host
  src/host/host_runtime.cpp
  # support
  src/support/mobilegl_support.cpp
)
target_include_directories(tgles_core PUBLIC include)
target_link_libraries(tgles_core PUBLIC tgles_spec)
if(APPLE)
  target_compile_options(tgles_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Real Metal execution + CAMetalLayer present bridge (ObjC++ only).
# tgles_core stays pure C++; this target holds every <Metal/*>/<QuartzCore/*>
# import (src/gpu/apple/metal_bridge_apple.mm). Host tests use MockMetalBridge
# so CI without a GPU stays deterministic.
if(APPLE)
  add_library(tgles_metal_bridge
    src/gpu/apple/metal_bridge_apple.mm
  )
  target_include_directories(tgles_metal_bridge PUBLIC include)
  target_link_libraries(tgles_metal_bridge PUBLIC tgles_core
    "-framework Metal"
    "-framework QuartzCore"
    "-framework Foundation"
  )
  target_compile_options(tgles_metal_bridge PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Host C ABI shared library: the artifact a GLES host loads with dlopen
# (VK-GL-CTS, MobileGL/DirectGLES). It must export REAL entry points for the
# whole contract in tools/mobilegl/host_contract.json — never placeholder
# addresses (tests/host/test_abi_contract.cpp proves that with dlsym +
# eglGetProcAddress agreement checks; see plan/plan-02-cts-es32.md Block B).
#
# Sources: host_c_api.cpp owns the trial-subset real GL/EGL bodies,
# abi/egl_api.cpp owns the remaining 38 EGL bodies, abi/gl_gap.cpp owns the
# 344 declared GLES gaps (each records itself in the ledger), abi/gl_real.cpp
# will own real GLES bodies as Block A lands, and abi/dispatch.cpp owns the
# single eglGetProcAddress table so dlsym and proc-address can never diverge.
add_library(tgles_host SHARED
  src/host/host_c_api.cpp
  src/host/abi/egl_api.cpp
  src/host/abi/abi_ledger.cpp
  src/host/abi/gl_gap.cpp
  src/host/abi/gl_real.cpp
  src/host/abi/dispatch.cpp
  src/host/abi/present_abi.cpp
)
target_include_directories(tgles_host PRIVATE include)
target_link_libraries(tgles_host PRIVATE tgles_core)
# Present ABI owns the in-dylib Apple bridge (CAMetalLayer present through
# the BUILT file, the path the game window + MobileGL-style loaders use).
if(APPLE)
  target_link_libraries(tgles_host PRIVATE tgles_metal_bridge)
endif()
set_target_properties(tgles_host PROPERTIES
  OUTPUT_NAME "${TGLES_HOST_LIBRARY_NAME}"
  PREFIX ""
  CXX_VISIBILITY_PRESET default
)
if(APPLE)
  set_target_properties(tgles_host PROPERTIES
    SUFFIX ".dylib"
    INSTALL_NAME_DIR "@rpath"
  )
  target_compile_options(tgles_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

# Single test runner; translation units are grouped by layer, mirroring src/.
# NOTE: tests/gpu/test_metal_device.mm needs a real MTLDevice at RUNTIME; it
# compiles wherever OBJCXX exists and skips cleanly without a GPU.
# test_real_execution_matrix.mm is the same: real Apple-bridge pixel proofs +
# fail-closed proofs, skip without a GPU, deterministic on CI.
if(APPLE)
  set(TGLES_APPLE_TEST_SOURCES
    tests/gpu/test_metal_device.mm
    tests/gpu/test_real_execution_matrix.mm
    tests/gpu/test_texture_sampling_real.mm
    tests/gpu/test_lit_textured_scene.mm
    tests/gpu/test_compute_real.mm
    tests/gpu/test_blend_depth_real.mm
    tests/gpu/test_translated_shader_real.mm
    tests/gpu/test_mrt_msaa_real.mm
    tests/gpu/test_sampler_kinds_real.mm
    tests/gpu/test_raster_exec_real.mm
    tests/gpu/test_phase4_real.mm
    tests/host/test_dylib_present.mm)
else()
  set(TGLES_APPLE_TEST_SOURCES "")
endif()
add_executable(tgles_tests
  tests/test_main.cpp
  # base
  tests/base/test_spec_coverage.cpp
  tests/base/test_negative_api.cpp
  tests/base/test_state_machine.cpp
  tests/base/test_step01_foundation.cpp
  # state
  tests/state/test_step02_buffers_vao.cpp
  tests/state/test_step03_shaders_programs.cpp
  tests/state/test_step04_textures_samplers.cpp
  tests/state/test_step05_framebuffers.cpp
  tests/state/test_step06_stages_compute.cpp
  tests/state/test_step07_sync_query_debug.cpp
  tests/state/test_texture_formats.cpp
  tests/state/test_texture_upload.cpp
  tests/state/test_debug_filter.cpp
  tests/state/test_query_targets.cpp
  tests/state/test_shader_language.cpp
  tests/state/test_copy_image_3d.cpp
  tests/state/test_border_clamp.cpp
  tests/state/test_pipeline_stages.cpp
  tests/state/test_texture_view.cpp
  tests/state/test_compute_execution.cpp
  tests/state/test_batch_gaps_state.cpp
  tests/state/test_final23_state.cpp
  # pipeline
  tests/pipeline/test_step08_draw_raster.cpp
  tests/pipeline/test_depth_buffer.cpp
  # gpu
  tests/gpu/test_step09_backend.cpp
  tests/gpu/test_metal_mapping.cpp
  tests/gpu/test_metal_feature_matrix.cpp
  tests/gpu/test_msl_translation.cpp
  tests/gpu/test_glsl_to_msl.cpp
  tests/gpu/test_metal_translation.cpp
  tests/gpu/test_metal_bridge.cpp
  tests/gpu/test_phase4_gaps.cpp
  ${TGLES_APPLE_TEST_SOURCES}
  # facade
  tests/facade/test_step10_facade_conformance.cpp
  tests/facade/test_multidraw_baseinstance.cpp
  # host: EGL layer, C ABI subset, and the upstream host contract
  tests/host/test_egl_layer.cpp
  tests/host/test_host_c_api.cpp
  tests/host/test_abi_contract.cpp
  tests/host/test_abi_state_path.cpp
  tests/host/test_abi_uniform_path.cpp
  tests/host/test_batch_gaps_abi.cpp
  tests/host/test_final23_abi.cpp
  tests/host/test_ext_entry_points.cpp
  # MobileGL host gates
  tests/mobilegl/test_mobilegl_support.cpp
  tests/mobilegl/test_mobilegl_gaps.cpp
  tests/mobilegl/test_mobilegl_host_requirements.cpp
  # Khronos CTS-derived conformance subset
  tests/cts/test_cts_subset.cpp
  tests/cts/test_es32_total.cpp
  tests/cts/test_official_coverage.cpp
  # app-level proofs (cube math, lit renderer, RenderFrame wiring)
  tests/apps/test_cube_model.cpp
  tests/apps/test_cube_render.cpp
  tests/apps/test_render_frame.cpp
  # repository self-checks (docs/plan claims must stay true)
  tests/support/test_plan_consistency.cpp
)
target_include_directories(tgles_tests PRIVATE include tests apps)
# Link the host ABI too: test_host_c_api.cpp calls gl*/egl* directly, and the
# contract test needs the same symbols + the built library on disk to dlopen.
# NOTE (singleton identity): HostRuntime::Instance() must exist in exactly one
# binary. tgles_host (the dlopened library) already contains tgles_core, so the
# test executable resolves core symbols from tgles_host instead of linking a
# second static copy — otherwise the test would flip switches on its own
# HostRuntime while the gl* calls read the library's copy (stale-tree symptom:
# HostCApi.GlCallsNeedCurrentContext / DrawTriangleThroughMock failing).
target_link_libraries(tgles_tests PRIVATE tgles_spec tgles_host)
if(APPLE)
  target_link_libraries(tgles_tests PRIVATE tgles_metal_bridge)
endif()
target_compile_definitions(tgles_tests PRIVATE
  TGLES_REFERENCE_DIR="${TGLES_REFERENCE_DIR}"
  # Built host shared library, dlopen()ed by the host-contract test.
  TGLES_HOST_LIBRARY_PATH="$<TARGET_FILE:tgles_host>"
  TGLES_MOBILEGL_HOST_CONTRACT_JSON="${CMAKE_CURRENT_SOURCE_DIR}/tools/mobilegl/host_contract.json"
)
add_dependencies(tgles_tests tgles_host)

add_test(NAME tgles_tests COMMAND tgles_tests)

# macOS 3D render trial (Darwin-only CLI): rotating cube through the full
# TGL path, writing cube_frameN.ppm. iOS builds skip it (same core, but the
# trial is a macOS host proof, not a shipped app).
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  add_executable(tgl_cube_trial
    apps/tgl_cube_trial.mm
  )
  target_include_directories(tgl_cube_trial PRIVATE include apps)
  target_link_libraries(tgl_cube_trial PRIVATE tgles_spec tgles_core
                        tgles_metal_bridge)

  # Complex scene trial (Darwin-only CLI): rotating LIT + TEXTURED cube with
  # real depth through RenderFrame + Apple bridge, writing lit_tex_frameN.ppm.
  # This is the MobileGL-relevant combination (lighting x sampling x depth);
  # failures print the stage + frame + GL/bridge error (never silent).
  add_executable(tgl_lit_textured_trial
    apps/tgl_lit_textured_trial.mm
  )
  target_include_directories(tgl_lit_textured_trial PRIVATE include apps)
  target_link_libraries(tgl_lit_textured_trial PRIVATE tgles_spec tgles_core
                        tgles_metal_bridge)

  # Interactive viewer (Darwin-only Cocoa window): GLES 3.2 street of tall
  # lit bars + spinner through RenderFrame + Metal present, with FPS HUD,
  # frame graph and orbit/pan/zoom camera. Links Cocoa for NSWindow.
  add_executable(tgl_viewer
    apps/tgl_viewer.mm
  )
  target_include_directories(tgl_viewer PRIVATE include apps)
  target_link_libraries(tgl_viewer PRIVATE tgles_spec tgles_core
                        tgles_metal_bridge
                        "-framework Cocoa"
                        "-framework Metal"
                        "-framework QuartzCore"
                        "-framework Foundation")

  # Game window (Darwin-only Cocoa window): a REAL playable GLES 3.2 game
  # (NEON DODGE) whose only driver is the BUILT libtgles.dylib via dlopen +
  # eglGetProcAddress (MobileGL loader route). Links NO tgles_* library —
  # only Cocoa/Metal/QuartzCore/Foundation — proving the built file executes.
  add_executable(tgl_game_window
    apps/tgl_game_window.mm
  )
  target_include_directories(tgl_game_window PRIVATE include apps)
  target_link_libraries(tgl_game_window PRIVATE
                        "-framework Cocoa"
                        "-framework Metal"
                        "-framework QuartzCore"
                        "-framework Foundation")

  # ES 3.2 API window (Darwin-only Cocoa window): visible sweep that renders
  # for real while touching all 358 standard entry points, with a phase log
  # panel and a final coverage audit. Links tgles_host too: a few entry
  # points only exist in the C ABI layer and are driven on a pbuffer context.
  add_executable(tgl_api_window
    apps/tgl_api_window.mm
  )
  target_include_directories(tgl_api_window PRIVATE include apps)
  target_link_libraries(tgl_api_window PRIVATE tgles_spec tgles_core
                        tgles_host tgles_metal_bridge
                        "-framework Cocoa"
                        "-framework Metal"
                        "-framework QuartzCore"
                        "-framework Foundation")
endif()
