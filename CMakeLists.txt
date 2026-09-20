cmake_minimum_required(VERSION 3.20)

# TGL: OpenGL ES 3.2 -> Metal transpiler scaffold (test-first).
# All code and comments are English. User communication is Vietnamese.
project(TGLES VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Reference directory with downloaded official specs and headers.
set(TGLES_REFERENCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/docs/reference")

# Knowledge tables (spec ground truth, verified by tests).
add_library(tgles_spec
  src/spec.cpp
  src/metal_mapping.cpp
  src/msl_translation.cpp
)
target_include_directories(tgles_spec PUBLIC include)
target_compile_definitions(tgles_spec PUBLIC
  TGLES_REFERENCE_DIR="${TGLES_REFERENCE_DIR}"
)
if(APPLE)
  target_compile_options(tgles_spec PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Step 1+: portable runtime core (first real ES implementation modules).
# Pure C++ with no platform APIs, so it cross-compiles to iOS as-is.
add_library(tgles_core
  src/error.cpp
  src/context.cpp
  src/buffer.cpp
  src/vertex_array.cpp
  src/shader.cpp
  src/program.cpp
  src/texture.cpp
  src/sampler.cpp
  src/framebuffer.cpp
  src/stage_config.cpp
  src/sync.cpp
  src/query.cpp
  src/debug.cpp
  src/draw.cpp
  src/raster.cpp
  src/backend.cpp
  src/gles.cpp
  src/egl.cpp
  src/mobilegl_support.cpp
  src/transform_feedback.cpp
  src/pixels.cpp
  src/image_units.cpp
  src/metal_bridge.cpp
  src/host_runtime.cpp
  src/host_c_api.cpp
)
target_include_directories(tgles_core PUBLIC include)
if(APPLE)
  target_compile_options(tgles_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Real Metal execution + CAMetalLayer present bridge (ObjC++ only).
# tgles_core stays pure C++; this target holds ALL <Metal/*>/<QuartzCore/*>
# imports (src/metal_bridge_apple.mm). Host tests use MockMetalBridge so CI
# without a GPU stays deterministic; iOS targets link this for real pixels.
if(APPLE)
  add_library(tgles_metal_bridge
    src/metal_bridge_apple.mm
  )
  target_include_directories(tgles_metal_bridge PUBLIC include)
  target_link_libraries(tgles_metal_bridge PUBLIC tgles_core
    "-framework Metal"
    "-framework QuartzCore"
    "-framework Foundation"
  )
  target_compile_options(tgles_metal_bridge PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

# Single test runner that includes all test translation units.
# NOTE: test_metal_device.mm needs a real MTLDevice at RUNTIME, but it
# compiles everywhere OBJCXX exists and skips cleanly without a GPU.
if(APPLE)
  set(TGLES_APPLE_TEST_SOURCES tests/test_metal_device.mm)
else()
  set(TGLES_APPLE_TEST_SOURCES "")
endif()
add_executable(tgles_tests
  tests/test_main.cpp
  tests/test_spec_coverage.cpp
  tests/test_negative_api.cpp
  tests/test_query_targets.cpp
  tests/test_shader_language.cpp
  tests/test_pipeline_stages.cpp
  tests/test_state_machine.cpp
  tests/test_texture_formats.cpp
  tests/test_metal_mapping.cpp
  tests/test_metal_feature_matrix.cpp
  tests/test_msl_translation.cpp
  tests/test_plan_consistency.cpp
  tests/test_step01_foundation.cpp
  tests/test_step02_buffers_vao.cpp
  tests/test_step03_shaders_programs.cpp
  tests/test_step04_textures_samplers.cpp
  tests/test_step05_framebuffers.cpp
  tests/test_step06_stages_compute.cpp
  tests/test_step07_sync_query_debug.cpp
  tests/test_step08_draw_raster.cpp
  tests/test_step09_backend.cpp
  tests/test_step10_facade_conformance.cpp
  tests/test_mobilegl_host_requirements.cpp
  tests/test_mobilegl_support.cpp
  tests/test_mobilegl_gaps.cpp
  tests/test_host_c_api.cpp
  tests/test_cts_subset.cpp
  tests/test_border_clamp.cpp
  tests/test_copy_image_3d.cpp
  tests/test_cube_model.cpp
  tests/test_cube_render.cpp
  tests/test_render_frame.cpp
  tests/test_depth_buffer.cpp
  tests/test_metal_bridge.cpp
  ${TGLES_APPLE_TEST_SOURCES}
  tests/test_egl_layer.cpp
)
target_include_directories(tgles_tests PRIVATE include tests apps)
target_link_libraries(tgles_tests PRIVATE tgles_spec tgles_core)
if(APPLE)
  target_link_libraries(tgles_tests PRIVATE tgles_metal_bridge)
endif()
target_compile_definitions(tgles_tests PRIVATE
  TGLES_REFERENCE_DIR="${TGLES_REFERENCE_DIR}"
)

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
endif()
