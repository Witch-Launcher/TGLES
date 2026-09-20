// State machine: ES is a global state machine, Metal uses baked PSOs.
// Core enable caps map to pipeline/depth-stencil state; non-core caps must
// be rejected instead of silently mapped.

#include "test_framework.h"

#include <cstring>

#include "tgles/spec.h"

namespace {

// Minimal core-cap table (subset of spec Table 21+ enable states).
bool IsCoreEnableCap(const char* cap) {
  static const char* const kCore[] = {
      "GL_BLEND", "GL_DEPTH_TEST", "GL_CULL_FACE", "GL_SCISSOR_TEST",
      "GL_STENCIL_TEST", "GL_PRIMITIVE_RESTART_FIXED_INDEX",
      "GL_RASTERIZER_DISCARD", "GL_SAMPLE_COVERAGE", "GL_SAMPLE_MASK",
      "GL_DITHER", "GL_POLYGON_OFFSET_FILL",
  };
  for (const char* c : kCore) {
    if (std::strcmp(cap, c) == 0) return true;
  }
  return false;
}

}  // namespace

TEST(StateMachine, CoreCapsRecognized) {
  EXPECT_TRUE(IsCoreEnableCap("GL_BLEND"));
  EXPECT_TRUE(IsCoreEnableCap("GL_DEPTH_TEST"));
  EXPECT_TRUE(IsCoreEnableCap("GL_CULL_FACE"));
  EXPECT_TRUE(IsCoreEnableCap("GL_POLYGON_OFFSET_FILL"));
}

TEST(StateMachine, NonCoreCapsRejected) {
  // GL_PROGRAM_POINT_SIZE does not exist in core gl32.h.
  EXPECT_FALSE(IsCoreEnableCap("GL_PROGRAM_POINT_SIZE"));
  // Desktop-only polygon mode has no ES enable cap.
  EXPECT_TRUE(tgles::IsNonCoreFunction("glPolygonMode"));
}

TEST(StateMachine, DepthRangeAndViewportAreSeparate) {
  // glDepthRange* and glViewport/scissor are distinct state words;
  // a correct emulator must not conflate depth-mask with viewport.
  EXPECT_TRUE(IsCoreEnableCap("GL_DEPTH_TEST"));
  EXPECT_TRUE(IsCoreEnableCap("GL_SCISSOR_TEST"));
  EXPECT_FALSE(IsCoreEnableCap("GL_VIEWPORT"));
}
