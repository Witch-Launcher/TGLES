// MobileGL-DirectGLES host requirements test: proves TGL qualifies as the
// host GLES 3.2 driver under MobileGL (desktop GL -> DirectGLES -> host
// GLES -> Metal), e.g. for a Minecraft Java launcher on iOS. It also pins
// the two known gaps so nobody can claim end-to-end readiness early.

#include "test_framework.h"

#include <string>

#include "tgles/gles.h"

TEST(MobileGlHost, Gles32CoreSatisfied) {
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  // DirectGLES/MobileGlues wants host GLES 3.x, best 3.2.
  tgles::GLint major = 0, minor = 0;
  ctx.foundation().GetIntegerv(tgles::kGlMajorVersion, &major);
  ctx.foundation().GetIntegerv(tgles::kGlMinorVersion, &minor);
  EXPECT_EQ(major, 3);
  EXPECT_EQ(minor, 2);
  EXPECT_TRUE(ctx.ConformanceChecklist().empty());
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(MobileGlHost, TimerQueryStanceMatchesFlag) {
  // MobileGL exposes MOBILEGL_DISABLE_TIMERQUERY; TGL treats timer queries
  // as EXT-only (never core), so both settings stay consistent.
  EXPECT_TRUE(tgles::IsNonCoreQueryEnum("GL_TIME_ELAPSED"));
  EXPECT_TRUE(tgles::IsNonCoreQueryEnum("GL_TIMESTAMP"));
  EXPECT_FALSE(tgles::IsNonCoreQueryEnum("GL_ANY_SAMPLES_PASSED"));
}

TEST(MobileGlHost, AdvancedStagesPresent) {
  // Sodium/Iris-style paths need geometry, tessellation and compute.
  tgles::GlesContext ctx = tgles::GlesContext::Create(false);
  EXPECT_NE(ctx.shaders().CreateShader(tgles::kGlGeometryShader), 0u);
  EXPECT_NE(ctx.shaders().CreateShader(tgles::kGlTessControlShader), 0u);
  EXPECT_NE(ctx.shaders().CreateShader(tgles::kGlTessEvaluationShader), 0u);
  EXPECT_NE(ctx.shaders().CreateShader(tgles::kGlComputeShader), 0u);
  EXPECT_TRUE(tgles::kMaxPatchVertices >= 32);
  EXPECT_TRUE(tgles::kMaxComputeWorkGroupInvocations >= 128);
  EXPECT_EQ(ctx.GetError(), tgles::kGlNoError);
}

TEST(MobileGlHost, IosBaselineFamiliesKnown) {
  // iOS 14 baseline is Apple3 (A9); BC-gated content needs Apple9+.
  EXPECT_TRUE(tgles::metal::FindGpuFamily("Apple3") != nullptr);
  EXPECT_TRUE(tgles::metal::FindGpuFamily("Apple10") != nullptr);
  EXPECT_TRUE(tgles::metal::FindGpuFamily("Apple9")->supports_bc_compression);
  EXPECT_FALSE(
      tgles::metal::FindGpuFamily("Apple8")->supports_bc_compression);
}

TEST(MobileGlHost, KnownGapsAreExactlyTwo) {
  // Living TODO gate: host state + bridge + wiring are done and tested;
  // end-to-end launcher runs need broader draw coverage and an on-device
  // iOS run.
  std::vector<std::string> gaps = tgles::GlesContext::HostIntegrationGaps();
  EXPECT_EQ(gaps.size(), 2u);
  EXPECT_TRUE(gaps[0].find("indexed") != std::string::npos);
  EXPECT_TRUE(gaps[1].find("iOS") != std::string::npos);
}
