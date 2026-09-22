// GLSL ES language rules (GLSL_ES_Specification_3.20.pdf v3.20.8).
// ES 3.2 implementations accept 4 language versions; 3.20 needs
// "#version 320 es" and __VERSION__ == 320.

#include "test_framework.h"

#include "tgles/base/spec.h"

TEST(ShaderLanguage, FourVersionsSupported) {
  EXPECT_EQ(tgles::kSupportedGlslVersionCount, static_cast<std::size_t>(4));
  EXPECT_EQ(tgles::kSupportedGlslVersions[0], 100);
  EXPECT_EQ(tgles::kSupportedGlslVersions[1], 300);
  EXPECT_EQ(tgles::kSupportedGlslVersions[2], 310);
  EXPECT_EQ(tgles::kSupportedGlslVersions[3], 320);
}

TEST(ShaderLanguage, VersionDirectives) {
  EXPECT_STREQ(tgles::GlslVersionDirective(100), "#version 100 es");
  EXPECT_STREQ(tgles::GlslVersionDirective(300), "#version 300 es");
  EXPECT_STREQ(tgles::GlslVersionDirective(310), "#version 310 es");
  EXPECT_STREQ(tgles::GlslVersionDirective(320), "#version 320 es");
  EXPECT_TRUE(tgles::GlslVersionDirective(999) == nullptr);
}

TEST(ShaderLanguage, VersionNumberIs320) {
  EXPECT_EQ(tgles::kGlsl320VersionNumber, 320);
}

TEST(ShaderLanguage, SixShaderStages) {
  EXPECT_EQ(tgles::kShaderStageCount, static_cast<std::size_t>(6));
  EXPECT_STREQ(tgles::ShaderStageName(tgles::ShaderStage::kVertex), "vertex");
  EXPECT_STREQ(tgles::ShaderStageName(tgles::ShaderStage::kTessControl),
               "tess_control");
  EXPECT_STREQ(tgles::ShaderStageName(tgles::ShaderStage::kTessEvaluation),
               "tess_evaluation");
  EXPECT_STREQ(tgles::ShaderStageName(tgles::ShaderStage::kGeometry),
               "geometry");
  EXPECT_STREQ(tgles::ShaderStageName(tgles::ShaderStage::kFragment),
               "fragment");
  EXPECT_STREQ(tgles::ShaderStageName(tgles::ShaderStage::kCompute),
               "compute");
}
