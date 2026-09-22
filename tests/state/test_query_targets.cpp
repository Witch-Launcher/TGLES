// Async query targets: spec Table 4.2 (p.39) allows exactly 4 core targets.
// Timer queries and stream-overflow are EXT-only, not core.

#include "test_framework.h"

#include <fstream>
#include <string>

#include "tgles/base/spec.h"

TEST(QueryTargets, ExactlyFourCoreTargets) {
  EXPECT_EQ(tgles::kCoreQueryTargetCount, static_cast<std::size_t>(4));
  EXPECT_STREQ(tgles::QueryTargetName(tgles::QueryTarget::kPrimitivesGenerated),
               "GL_PRIMITIVES_GENERATED");
  EXPECT_STREQ(tgles::QueryTargetName(
                   tgles::QueryTarget::kTransformFeedbackPrimitivesWritten),
               "GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN");
  EXPECT_STREQ(tgles::QueryTargetName(tgles::QueryTarget::kAnySamplesPassed),
               "GL_ANY_SAMPLES_PASSED");
  EXPECT_STREQ(tgles::QueryTargetName(
                   tgles::QueryTarget::kAnySamplesPassedConservative),
               "GL_ANY_SAMPLES_PASSED_CONSERVATIVE");
}

TEST(QueryTargets, EnumValuesMatchSpec) {
  EXPECT_EQ(tgles::QueryTargetEnum(
                tgles::QueryTarget::kPrimitivesGenerated),
            0x8C87u);
  EXPECT_EQ(tgles::QueryTargetEnum(
                tgles::QueryTarget::kTransformFeedbackPrimitivesWritten),
            0x8C88u);
  EXPECT_EQ(tgles::QueryTargetEnum(tgles::QueryTarget::kAnySamplesPassed),
            0x8C2Fu);
  EXPECT_EQ(tgles::QueryTargetEnum(
                tgles::QueryTarget::kAnySamplesPassedConservative),
            0x8D6Au);
}

TEST(QueryTargets, TimerQueriesAreNotCore) {
  EXPECT_TRUE(tgles::IsNonCoreQueryEnum("GL_TIME_ELAPSED"));
  EXPECT_TRUE(tgles::IsNonCoreQueryEnum("GL_TIMESTAMP"));
  EXPECT_TRUE(
      tgles::IsNonCoreQueryEnum("GL_TRANSFORM_FEEDBACK_STREAM_OVERFLOW"));
  EXPECT_FALSE(tgles::IsNonCoreQueryEnum("GL_ANY_SAMPLES_PASSED"));
  EXPECT_FALSE(tgles::IsNonCoreQueryEnum(nullptr));
}

TEST(QueryTargets, CoreHeaderHasNoTimerEnums) {
  const char* dir = TGLES_REFERENCE_DIR;
  std::string core = std::string(dir) + "/gl32.h";
  std::ifstream probe(core);
  if (!probe) return;
  std::string content((std::istreambuf_iterator<char>(probe)),
                      std::istreambuf_iterator<char>());
  EXPECT_TRUE(content.find("GL_ANY_SAMPLES_PASSED") != std::string::npos);
  EXPECT_TRUE(content.find("GL_PRIMITIVES_GENERATED") != std::string::npos);
  EXPECT_TRUE(content.find("GL_TIME_ELAPSED") == std::string::npos);
}
