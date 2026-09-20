// The plan's GLSL->MSL triangle example is syntactically invalid MSL.
// Valid MSL carries varyings in a struct with [[user(...)]] members.

#include "test_framework.h"

#include <cstring>

#include "tgles/msl_translation.h"

TEST(MslTranslation, PlanSnippetPatternIsInvalid) {
  // Exact invalid pattern quoted from plan-01.md section III.8.
  const char* invalid =
      "vertex float4 vert_main(VertexIn in [[stage_in]],\n"
      "                        constant Uniforms& uni [[ buffer(0) ]],\n"
      "                        thread float4& outColor [[user(locn0)]]) {\n"
      "    return pos;\n"
      "}";
  EXPECT_TRUE(tgles::msl::UsesInvalidThreadUserVarying(invalid));
  EXPECT_FALSE(tgles::msl::UsesVaryingsStruct(invalid));
}

TEST(MslTranslation, CorrectedShaderUsesVaryingsStruct) {
  const char* fixed = tgles::msl::CorrectedTriangleVertexShader();
  EXPECT_NOT_NULL(fixed);
  EXPECT_FALSE(tgles::msl::UsesInvalidThreadUserVarying(fixed));
  EXPECT_TRUE(tgles::msl::UsesVaryingsStruct(fixed));
  EXPECT_TRUE(std::strstr(fixed, "[[stage_in]]") != nullptr);
  EXPECT_TRUE(std::strstr(fixed, "[[position]]") != nullptr);
  EXPECT_TRUE(std::strstr(fixed, "[[buffer(0)]]") != nullptr);
  EXPECT_TRUE(std::strstr(fixed, "[[attribute(0)]]") != nullptr);
}

TEST(MslTranslation, NullSafe) {
  EXPECT_FALSE(tgles::msl::UsesInvalidThreadUserVarying(nullptr));
  EXPECT_FALSE(tgles::msl::UsesVaryingsStruct(nullptr));
}
